// 见 ChatController.h：事件流 -> 消息模型。
#include "common/session/ChatController.h"
#include "common/session/MessageListModel.h"
#include "common/Agent/AgentSessionClient.h"
#include "common/session/TraceModel.h"

#include "network/DshApiClient.h"
#include "common/Agent/AgentEventParser.h"
#include "network/SessionCommands.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QTemporaryFile>
#include <QUuid>
#include <QUrl>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <QVariantMap>

#include <algorithm>

namespace
{
	constexpr int kStreamFlushMs = 33;
	constexpr int kTurnWatchdogMs = 30000;        // 这么久没有本会话事件就认为后端没在干活（没配模型 / resume 失败）
	constexpr int kCancelRepeatGuardMs = 1500;    // 停止键连点去重窗口：只认第一次，期间靠 cancelling 表态
	constexpr int kCancelConfirmMs = 8000;        // 发出 cancel 后等不到 turn/end 就本地强制收尾（服务端可能没实现中断）

	// 隐藏的上下文注入（运行时快照/系统说明/目录）不是用户真正发的消息。
	// 另外两类同样要藏 —— 子代理回传给父会话的话（kind=agent-message）与"后台子代理结束"通知（kind=subagent-settled）：
	// 两者都是以 role=user 记进账本的 inbox 注入，画成用户气泡会让人以为是自己在说话。
	QJsonObject userMessageSource(const QJsonObject& event)
	{
		const QJsonObject data = event.value(QStringLiteral("data")).toObject();
		QJsonObject message = data.value(QStringLiteral("message")).toObject();
		if (message.isEmpty())
			message = data;
		return message.value(QStringLiteral("source")).toObject();
	}

	bool isPeerRelayUserMessage(const QJsonObject& event)
	{
		const QString kind = userMessageSource(event).value(QStringLiteral("kind")).toString();
		return kind == QStringLiteral("agent-message")
			|| kind == QStringLiteral("subagent-settled");
	}

	bool isHiddenContextUserMessage(const QJsonObject& event)
	{
		const QJsonObject source = userMessageSource(event);
		const QString form = source.value(QStringLiteral("form")).toString();
		if (form == QStringLiteral("snapshot")
			|| form == QStringLiteral("instructions")
			|| form == QStringLiteral("catalog"))
			return true;
		return isPeerRelayUserMessage(event);
	}

	// 历史记录可能包一层 { event: {...} }
	QJsonObject unwrapRecord(const QJsonValue& value)
	{
		QJsonObject event = value.toObject();
		if (event.value(QStringLiteral("event")).isObject())
			event = event.value(QStringLiteral("event")).toObject();
		return event;
	}

	int recordSeq(const QJsonValue& value)
	{
		const QJsonObject record = value.toObject();
		int seq = record.value(QStringLiteral("seq")).toInt();
		if (seq <= 0 && record.value(QStringLiteral("event")).isObject())
			seq = record.value(QStringLiteral("event")).toObject().value(QStringLiteral("seq")).toInt();
		return seq;
	}

	QString prettyArguments(const QJsonObject& arguments)
	{
		return QString::fromUtf8(QJsonDocument(arguments).toJson(QJsonDocument::Indented));
	}

	// 表示"回合真的在推进"的事件类型（看门狗只认这些续期）；刻意不含会话生命周期/投影/标题/命令变更 —— 它们会周期性推来。
	bool isTurnProgressEvent(const QString& type)
	{
		return type.startsWith(QLatin1String("assistant/"))
			|| type == QLatin1String("text-chunks")
			|| type == QLatin1String("reasoning-chunks")
			|| type == QLatin1String("tool/call")
			|| type == QLatin1String("tool/result")
			|| type == QLatin1String("step/start")
			|| type == QLatin1String("step/end")
			|| type == QLatin1String("turn/start");
	}

	// ---- 统计行的四个格式化函数 ----

	QString formatDuration(qint64 ms)
	{
		if (ms <= 0)
			return QString();

		if (ms < 60000)
			return QString::number(ms / 1000.0, 'f', 1) + QStringLiteral("s");

		const qint64 seconds = ms / 1000;
		return QStringLiteral("%1m%2s").arg(seconds / 60).arg(seconds % 60);
	}

	QString formatTokens(qint64 value)
	{
		if (value < 1000)
			return QString::number(value);
		if (value < 1000000)
			return QString::number(value / 1000.0, 'f', value >= 100000 ? 0 : 1) + QStringLiteral("K");
		return QString::number(value / 1000000.0, 'f', value >= 100000000 ? 0 : 1) + QStringLiteral("M");
	}

	QString formatThroughput(double value)
	{
		return QString::number(value, 'f', value >= 10.0 ? 0 : 1);
	}

	QString formatCacheHitPercent(qint64 billedInput, qint64 cacheRead)
	{
		if (billedInput <= 0)
			return QString();
		if (cacheRead >= billedInput)
			return QStringLiteral("100");

		const double ratio = static_cast<double>(cacheRead) / static_cast<double>(billedInput) * 100.0;
		const int rounded = qRound(ratio);
		if (rounded < 100)
			return QString::number(rounded);

		// 四舍五入到 100 但并未真的 100：依次给 1–4 位小数，兜底 99.99
		for (int digits = 1; digits <= 4; ++digits) {
			const QString text = QString::number(ratio, 'f', digits);
			if (text != QLatin1String("100") && text != QLatin1String("100.0")
				&& text != QLatin1String("100.00") && text != QLatin1String("100.000")
				&& text != QLatin1String("100.0000")) {
				return text;
			}
		}
		return QStringLiteral("99.99");
	}
}

ChatController::ChatController(DshApiClient* api, AgentSessionClient* agentSession, QObject* parent)
	: QObject(parent)
	, m_api(api)
	, m_agentSession(agentSession)
	, m_messages(new MessageListModel(this))
	, m_trace(new TraceModel(this))
{
	// 附件临时物统一落在 <exe>/temp/ulinhub-attachments：启动清一次，避免长期使用后无限堆积（缺失图片按需重拉）
	QDir(QCoreApplication::applicationDirPath() + QStringLiteral("/temp/ulinhub-attachments"))
		.removeRecursively();
	connect(m_messages, &QAbstractItemModel::modelReset, this, &ChatController::messagesChanged);
	connect(m_messages, &QAbstractItemModel::rowsInserted, this, &ChatController::messagesChanged);

	m_flushTimer = new QTimer(this);
	m_flushTimer->setInterval(kStreamFlushMs);
	connect(m_flushTimer, &QTimer::timeout, this, &ChatController::flushPending);

	m_sessionLoadingTimer = new QTimer(this);
	m_sessionLoadingTimer->setSingleShot(true);
	m_sessionLoadingTimer->setInterval(5000);
	connect(m_sessionLoadingTimer, &QTimer::timeout, this, [this]() {
		if (!m_sessionLoading)
			return;
		m_sessionLoading = false;
		emit sessionLoadingChanged();
	});

	m_turnWatchdog = new QTimer(this);
	m_turnWatchdog->setSingleShot(true);
	m_turnWatchdog->setInterval(kTurnWatchdogMs);
	connect(m_turnWatchdog, &QTimer::timeout, this, [this]() {
		if (!m_turnActive)
			return;
		qWarning().noquote() << "[Chat] 回合看门狗超时：本会话" << kTurnWatchdogMs
			<< "ms 内没有任何事件（多半是后端没配模型/会话 resume 失败）";
		flushPending();
		setStreaming(false);
		finishTurn();
	});

	// 中止确认看门狗：cancel() 之后等 turn/end 的兜底。与 m_turnWatchdog 分工不同 —— 那个看"回合有没有输出"，
	// 这个看"中止有没有被服务端确认"。
	m_cancelWatchdog = new QTimer(this);
	m_cancelWatchdog->setSingleShot(true);
	m_cancelWatchdog->setInterval(kCancelConfirmMs);
	connect(m_cancelWatchdog, &QTimer::timeout, this, [this]() {
		if (!m_cancelPending)
			return;
		qWarning().noquote() << "[Chat] 中止未在" << kCancelConfirmMs
			<< "ms 内收到 turn/end，本地强制收尾（服务端多半没有响应 session/cancel）";
		flushPending();
		setStreaming(false);
		m_turnHadOutput = true;   // 用户主动掐断、不是"后端没干活" ⇒ 别让 finishTurn 再叠一条空回复提示
		if (m_turnActive)
			finishTurn();         // finishTurn 内部会 clearCancelPending
		else
			clearCancelPending();
		m_messages->appendSystem(qtTrId("chat_cancel_timeout_hint"));
	});
}

// ------------------------------------------------------------------
// 发送一轮提示：发送策略与自愈状态机都在这里（接线层只转调）
// ------------------------------------------------------------------

// ---- 图片附件 ----

namespace {
// 记录里的图片块：event.data.message.content[] = { type:"image", attachment:{attachmentId, mediaType} }
QVector<MessageListModel::ImageRef> imagesFromContent(const QJsonObject& event)
{
	QVector<MessageListModel::ImageRef> images;
	const QJsonObject data = event.value(QStringLiteral("data")).toObject();
	QJsonObject message = data.value(QStringLiteral("message")).toObject();
	if (message.isEmpty())
		message = data;
	const QJsonArray blocks = message.value(QStringLiteral("content")).toArray();
	for (const QJsonValue& value : blocks) {
		const QJsonObject block = value.toObject();
		if (block.value(QStringLiteral("type")).toString() != QLatin1String("image"))
			continue;
		const QJsonObject attachment = block.value(QStringLiteral("attachment")).toObject();
		MessageListModel::ImageRef ref;
		ref.attachmentId = attachment.value(QStringLiteral("attachmentId")).toString();
		ref.mediaType = attachment.value(QStringLiteral("mediaType")).toString();
		if (!ref.attachmentId.isEmpty())
			images.append(ref);
	}
	return images;
}
} // namespace

QVector<MessageListModel::ImageRef> ChatController::extractEventImages(const QJsonObject& event)
{
	return imagesFromContent(event);
}

QVector<MessageListModel::DeliverableRef> ChatController::extractEventDeliverables(const QJsonObject& event)
{
	QVector<MessageListModel::DeliverableRef> files;
	const QJsonObject data = event.value(QStringLiteral("data")).toObject();
	const QJsonArray array = data.value(QStringLiteral("files")).toArray();
	for (const QJsonValue& value : array) {
		const QJsonObject object = value.toObject();
		MessageListModel::DeliverableRef file;
		file.path = object.value(QStringLiteral("path")).toString().trimmed();
		file.description = object.value(QStringLiteral("description")).toString().trimmed();
		if (!file.path.isEmpty())
			files.append(file);
	}
	return files;
}

void ChatController::applyDeliverablesEvent(const QJsonObject& event)
{
	const QVector<MessageListModel::DeliverableRef> files = extractEventDeliverables(event);
	if (files.isEmpty() || !m_messages)
		return;
	m_messages->appendDeliverables(agentRowForTurn(), files);
}

namespace {
QString iconForSuffix(const QString& suffix)
{
	if (suffix == QLatin1String("pdf"))
		return QStringLiteral("file-pdf");
	if (suffix == QLatin1String("doc") || suffix == QLatin1String("docx")
		|| suffix == QLatin1String("rtf") || suffix == QLatin1String("odt"))
		return QStringLiteral("file-doc");
	if (suffix == QLatin1String("xls") || suffix == QLatin1String("xlsx")
		|| suffix == QLatin1String("csv") || suffix == QLatin1String("ods"))
		return QStringLiteral("file-sheet");
	if (suffix == QLatin1String("ppt") || suffix == QLatin1String("pptx")
		|| suffix == QLatin1String("odp"))
		return QStringLiteral("file-slide");
	if (suffix == QLatin1String("zip") || suffix == QLatin1String("7z")
		|| suffix == QLatin1String("rar") || suffix == QLatin1String("tar")
		|| suffix == QLatin1String("gz"))
		return QStringLiteral("file-archive");
	if (suffix == QLatin1String("png") || suffix == QLatin1String("jpg")
		|| suffix == QLatin1String("jpeg") || suffix == QLatin1String("webp")
		|| suffix == QLatin1String("gif") || suffix == QLatin1String("svg"))
		return QStringLiteral("file-image");
	if (suffix == QLatin1String("c") || suffix == QLatin1String("cc")
		|| suffix == QLatin1String("cpp") || suffix == QLatin1String("h")
		|| suffix == QLatin1String("hpp") || suffix == QLatin1String("js")
		|| suffix == QLatin1String("ts") || suffix == QLatin1String("tsx")
		|| suffix == QLatin1String("jsx") || suffix == QLatin1String("py")
		|| suffix == QLatin1String("rs") || suffix == QLatin1String("go")
		|| suffix == QLatin1String("java") || suffix == QLatin1String("json")
		|| suffix == QLatin1String("yaml") || suffix == QLatin1String("yml")
		|| suffix == QLatin1String("xml") || suffix == QLatin1String("html")
		|| suffix == QLatin1String("css") || suffix == QLatin1String("qml")
		|| suffix == QLatin1String("sh") || suffix == QLatin1String("ps1"))
		return QStringLiteral("file-code");
	return QStringLiteral("file-generic");
}

QString sizeLabelFor(qint64 bytes)
{
	if (bytes >= 1024LL * 1024LL)
		return QString::number(bytes / (1024.0 * 1024.0), 'f', 1) + QStringLiteral(" MB");
	if (bytes >= 1024)
		return QString::number(bytes / 1024.0, 'f', 1) + QStringLiteral(" KB");
	return QString::number(bytes) + QStringLiteral(" B");
}
} // namespace

void ChatController::addImageFiles(const QVariantList& urls)
{
	addFiles(urls);   // 旧入口保留：图片继续内联，普通文件也一起接住
}

void ChatController::addFiles(const QVariantList& urls)
{
	int added = 0;
	for (const QVariant& value : urls) {
		const QString raw = value.toString();
		const QUrl url = raw.startsWith(QLatin1String("file:")) ? QUrl(raw) : QUrl::fromLocalFile(raw);
		const QString path = url.toLocalFile();
		const QFileInfo info(path);
		if (path.isEmpty() || !info.isFile())
			continue;
		QFile file(path);
		if (!file.open(QIODevice::ReadOnly))
			continue;

		const QString suffix = info.suffix().toLower();
		QString mediaType;
		if (suffix == QLatin1String("png"))
			mediaType = QStringLiteral("image/png");
		else if (suffix == QLatin1String("jpg") || suffix == QLatin1String("jpeg"))
			mediaType = QStringLiteral("image/jpeg");
		else if (suffix == QLatin1String("webp"))
			mediaType = QStringLiteral("image/webp");
		else if (suffix == QLatin1String("gif"))
			mediaType = QStringLiteral("image/gif");

		const bool isImage = !mediaType.isEmpty();
		QVariantMap entry;
		entry.insert(QStringLiteral("kind"), isImage ? QStringLiteral("image") : QStringLiteral("file"));
		entry.insert(QStringLiteral("name"), info.fileName());
		entry.insert(QStringLiteral("path"), path);
		entry.insert(QStringLiteral("suffix"), suffix);
		entry.insert(QStringLiteral("size"), info.size());
		entry.insert(QStringLiteral("sizeLabel"), sizeLabelFor(info.size()));
		entry.insert(QStringLiteral("icon"), iconForSuffix(suffix));

		if (isImage) {
			const QByteArray bytes = file.readAll();
			if (bytes.isEmpty())
				continue;
			entry.insert(QStringLiteral("mediaType"), mediaType);
			entry.insert(QStringLiteral("data"), QString::fromLatin1(bytes.toBase64()));
			const QString previewDir =
				QCoreApplication::applicationDirPath() + QStringLiteral("/temp/ulinhub-attachments/previews");
			QDir().mkpath(previewDir);
			const QString previewPath = previewDir
				+ QStringLiteral("/preview-")
				+ QUuid::createUuid().toString(QUuid::WithoutBraces)
				+ QLatin1Char('.')
				+ (mediaType.endsWith(QLatin1String("jpeg")) ? QStringLiteral("jpg") : suffix);
			QFile previewFile(previewPath);
			QString preview;
			if (previewFile.open(QIODevice::WriteOnly)) {
				previewFile.write(bytes);
				previewFile.close();
				preview = QUrl::fromLocalFile(previewPath).toString();
			}
			entry.insert(QStringLiteral("preview"), preview);
		}
		m_pendingImages.append(entry);
		++added;
	}
	if (added > 0) {
		qInfo().noquote() << "[Chat] pending attachments:" << m_pendingImages.size();
		emit pendingAttachmentsChanged();
	}
}

void ChatController::removePendingAttachment(int index)
{
	if (index < 0 || index >= m_pendingImages.size())
		return;
	m_pendingImages.removeAt(index);
	emit pendingAttachmentsChanged();
}

void ChatController::clearPendingAttachments()
{
	if (m_pendingImages.isEmpty())
		return;
	m_pendingImages.clear();
	emit pendingAttachmentsChanged();
}

QString ChatController::attachmentSource(const QString& attachmentId)
{
	if (attachmentId.isEmpty())
		return QString();
	const auto cached = m_attachmentSources.constFind(attachmentId);
	if (cached != m_attachmentSources.constEnd())
		return cached.value();
	if (m_attachmentInFlight.contains(attachmentId) || m_sessionId.isEmpty() || !m_api)
		return QString();

	m_attachmentInFlight.insert(attachmentId);
	m_api->callMethod(QStringLiteral("session/attachment"),
		SessionCommands::sessionAttachment(m_sessionId, attachmentId),
		[this, attachmentId](const QJsonObject& result) {
			m_attachmentInFlight.remove(attachmentId);
			const QJsonObject attachment = result.value(QStringLiteral("attachment")).toObject();
			const QString mediaType = attachment.value(QStringLiteral("mediaType")).toString();
			QByteArray bytes = QByteArray::fromBase64(result.value(QStringLiteral("data")).toString().toLatin1());
			if (bytes.isEmpty())
				return;
			QString suffix = mediaType.section(QLatin1Char('/'), -1);
			if (suffix == QLatin1String("jpeg"))
				suffix = QStringLiteral("jpg");
			const QString dir =
				QCoreApplication::applicationDirPath() + QStringLiteral("/temp/ulinhub-attachments/attachments");
			QDir().mkpath(dir);
			const QString path = dir + QStringLiteral("/attach-") + attachmentId + QLatin1Char('.') + suffix;
			QFile file(path);
			if (!file.open(QIODevice::WriteOnly))
				return;
			file.write(bytes);
			file.close();
			m_attachmentSources.insert(attachmentId, QUrl::fromLocalFile(path).toString());
			emit attachmentSourceReady(attachmentId);
		},
		[this, attachmentId](const DshApiClient::RpcError& error) {
			m_attachmentInFlight.remove(attachmentId);
			qWarning().noquote() << "[Chat] session/attachment failed:" << error.code << error.message;
		});
	return QString();
}

void ChatController::sendPrompt(const QString& sessionId, const QString& text,
	const std::function<void()>& restartServer,
	const std::function<void(const QString&)>& setStatusLine)
{
	const QString trimmed = text.trimmed();
	if ((trimmed.isEmpty() && m_pendingImages.isEmpty()) || sessionId.isEmpty() || !m_api)
		return;

	// 会话 id 是入参，本类的 m_sessionId 也得跟上：迟到的帧都按它过滤，补发也按它发。
	// 正常情况下宿主先 selectSession() 再 sendPrompt()，这里只是兜底（同一会话时 setSession 会早退）。
	if (m_sessionId != sessionId)
		setSession(sessionId);

	// 服务端明确没有可用模型时拦下来并说明（chat_no_model_hint）；目录未加载（未知）不拦。
	if (modelCatalogMissing()) {
		if (m_messages)
			m_messages->appendSystem(qtTrId("chat_no_model_hint"));
		return;
	}

	// 图片走 image 部件；普通文件先附在正文里（没有 upload receipt 时协议不接受 file 部件）。
	QJsonArray imageParts;
	QVector<MessageListModel::ImageRef> imageRefs;
	QStringList fileNotes;
	for (const QVariant& value : m_pendingImages) {
		const QVariantMap entry = value.toMap();
		if (entry.value(QStringLiteral("kind")).toString() == QLatin1String("image")) {
			QJsonObject part;
			part.insert(QStringLiteral("type"), QStringLiteral("image"));
			part.insert(QStringLiteral("mediaType"), entry.value(QStringLiteral("mediaType")).toString());
			part.insert(QStringLiteral("data"), entry.value(QStringLiteral("data")).toString());
			if (!entry.value(QStringLiteral("name")).toString().isEmpty())
				part.insert(QStringLiteral("name"), entry.value(QStringLiteral("name")).toString());
			imageParts.append(part);
			MessageListModel::ImageRef ref;
			ref.attachmentId = entry.value(QStringLiteral("id")).toString();
			ref.mediaType = entry.value(QStringLiteral("mediaType")).toString();
			ref.previewUrl = entry.value(QStringLiteral("preview")).toString();
			imageRefs.append(ref);
			continue;
		}
		const QString path = entry.value(QStringLiteral("path")).toString();
		if (!path.isEmpty())
			fileNotes.append(QStringLiteral("- %1 (%2)").arg(entry.value(QStringLiteral("name")).toString(), path));
	}
	if (!m_pendingImages.isEmpty()) {
		m_pendingImages.clear();
		emit pendingAttachmentsChanged();
	}
	QString promptText = trimmed;
	if (!fileNotes.isEmpty()) {
		if (!promptText.isEmpty())
			promptText += QStringLiteral("\n\n");
		promptText += QStringLiteral("附件：\n") + fileNotes.join(QLatin1Char('\n'));
	}

	beginTurn();
	if (m_messages)
		m_messages->appendUserWithImages(promptText, imageRefs);

	if (!qEnvironmentVariableIsEmpty("DSH_ULIN_ATTACH_PROBE")) {
		const QJsonObject payload = SessionCommands::sessionPrompt(sessionId, promptText, imageParts);
		const QByteArray json = QJsonDocument(payload).toJson(QJsonDocument::Compact);
		qInfo().noquote() << "[Chat] prompt payload bytes:" << json.size()
			<< "images:" << imageParts.size()
			<< "preview:" << QString::fromUtf8(json).left(240);
	}
	m_api->callMethod(QStringLiteral("session/prompt"),
		SessionCommands::sessionPrompt(sessionId, promptText, imageParts),
		[](const QJsonObject&) {},
		[this, promptText, sessionId, imageParts, restartServer, setStatusLine](const DshApiClient::RpcError& error) {
			// 失败文案用 chat_send_failed_fmt；同时记一行，便于看清服务端拒绝的原因
			qWarning().noquote() << "[Chat] session/prompt failed:" << error.code << error.message;

			// 会话写句柄还挂在**已经退出的旧进程**上（服务端不会因为客户端消失就立刻释放）：
			// 重启内置服务端清掉占用，随后把这条消息补发一次，否则永远发不出消息。
			const bool busy = error.message.contains(QLatin1String("already owned"))
				|| error.message.contains(QLatin1String("resume failed"));
			if (busy && !m_pendingRetry && restartServer) {
				m_pendingRetry = true;
				m_retryText = promptText;
				m_retrySessionId = sessionId;
				m_retryImages = imageParts;
				if (m_messages) {
					m_messages->appendSystem(qtTrId("chat_session_busy_restarting"));
				}
				if (setStatusLine)
					setStatusLine(qtTrId("chat_session_busy_status"));
				restartServer();
				return;
			}

			const QString text = qtTrId("chat_send_failed_fmt").arg(error.code, error.message);
			if (setStatusLine)
				setStatusLine(text);
			if (m_messages)
				m_messages->appendSystem(text);
		});
}

void ChatController::sendPromptAgain()
{
	if (!m_pendingRetry)   // 没攒着待补发的（宿主每次重连都会问一下）
		return;
	m_pendingRetry = false;
	const QString text = m_retryText;
	const QString sessionId = m_retrySessionId;
	const QJsonArray images = m_retryImages;
	m_retryText.clear();
	m_retrySessionId.clear();
	m_retryImages = QJsonArray();

	// 补发时不重复画用户气泡（首次已上屏）—— beginTurn 仍要调，重置"本轮有无可见输出"
	const QString trimmed = text.trimmed();
	if (trimmed.isEmpty() || sessionId.isEmpty() || !m_api)
		return;

	beginTurn();
	m_api->callMethod(QStringLiteral("session/prompt"),
		SessionCommands::sessionPrompt(sessionId, trimmed, images),
		[](const QJsonObject&) {},
		[this](const DshApiClient::RpcError& error) {
			qWarning().noquote() << "[Chat] session/prompt (retry) failed:" << error.code << error.message;
			if (m_messages)
				m_messages->appendSystem(qtTrId("chat_send_failed_fmt").arg(error.code, error.message));
		});
}

void ChatController::cancel()
{
	if (m_sessionId.isEmpty() || !m_api)
		return;

	// ① 回合已经不在跑了：界面上还能点"停止"只可能是残留态，这时发 RPC 只会打空。
	//    直接把本地流式态收干净，按钮立刻变回"发送"。
	if (!m_turnActive) {
		qInfo().noquote() << "[Chat] cancel ignored: 没有进行中的回合（顺带清理残留的流式态）";
		flushPending();
		setStreaming(false);
		clearCancelPending();
		return;
	}

	// ② 连点去重：同一回合窗口期内只发一次（界面上已经有"停止中"）。
	const qint64 now = QDateTime::currentMSecsSinceEpoch();
	if (m_cancelPending && now - m_cancelSentAtMs < kCancelRepeatGuardMs)
		return;

	// 先置"正在中止"再发请求：回包可能极快，先发再置会漏掉状态。
	m_cancelPending = true;
	m_cancelSentAtMs = now;
	emit cancellingChanged();
	m_cancelWatchdog->start();
	qInfo().noquote() << "[Chat] cancel -> session/cancel sessionId=" << m_sessionId;

	m_api->callMethod(QStringLiteral("session/cancel"), SessionCommands::sessionCancel(m_sessionId),
		[](const QJsonObject&) {
			// 回包只代表"服务端受理了"，不代表回合结束；真正的收尾点是 turn/end（或看门狗超时）。
		},
		[this](const DshApiClient::RpcError& error) {
			qWarning().noquote() << "[Chat] session/cancel failed:" << error.code << error.message;
			// 请求没送达 ⇒ 服务端不会因此收尾，别让按钮一直挂着"停止中"
			clearCancelPending();
			m_messages->appendSystem(qtTrId("chat_cancel_failed_fmt").arg(error.code, error.message));
		});
}

// 换会话：清空消息区并记住当前会话（其它会话的事件一律丢弃）
void ChatController::setSession(const QString& sessionId)
{
	if (m_sessionId == sessionId)
		return;

	flushPending();
	m_sessionId = sessionId;
	m_sessionLoading = !sessionId.isEmpty();
	if (m_sessionLoading)
		m_sessionLoadingTimer->start();
	else
		m_sessionLoadingTimer->stop();
	emit sessionLoadingChanged();
	m_streaming = false;
	m_turnActive = false;
	m_turnRow = -1;   // 行号属于旧会话，必须作废
	clearCancelPending();   // 中止状态也是会话级的：旧会话的"停止中"不能跟着进新会话
	m_loggedEventTypes.clear();
	m_messages->clear();
	m_trace->clear();
	// 翻页账本也是会话级的：不清的话，新会话的首帧空快照会被"空帧守卫"当噪声丢掉，
	// 而且 m_cursor/hasMore 还是上一个会话的。
	m_historyRecords = QJsonArray();
	m_cursor = 0;
	m_hasOlder = false;
	m_loadingOlder = false;
	emit olderChanged();
	// 名册是会话级的：不先清掉的话，切到新会话的头几百毫秒里顶栏还挂着上一个会话的任务
	if (!m_jobs.isEmpty()) {
		m_jobs.clear();
		emit jobsChanged();
	}
	emit streamingChanged();
	emit messagesChanged();
}

int ChatController::messageCount() const
{
	return m_messages ? m_messages->rowCount() : 0;
}

// ------------------------------------------------------------------
// 后台任务名册：活的任务在前按开始时间升序，已结束的按结束时间倒序；
// 这里只摊平字段，状态文案/配色都在 QML。
// ------------------------------------------------------------------
void ChatController::onJobRoster(const QJsonArray& jobs)
{
	struct Row
	{
		bool live = false;
		qint64 startedAt = 0;
		qint64 finishedAt = 0;
		QVariantMap map;
	};

	QVector<Row> rows;
	rows.reserve(jobs.size());
	for (const QJsonValue& value : jobs) {
		const QJsonObject job = value.toObject();
		const QString id = job.value(QStringLiteral("id")).toString();
		if (id.isEmpty())
			continue;   // 没有 id 的行无处可点、也没法停，直接丢

		const QString status = job.value(QStringLiteral("status")).toString();

		Row row;
		row.live = (status == QLatin1String("running") || status == QLatin1String("stopping"));
		row.startedAt = static_cast<qint64>(job.value(QStringLiteral("startedAt")).toDouble());
		row.finishedAt = static_cast<qint64>(job.value(QStringLiteral("finishedAt")).toDouble(row.startedAt));

		row.map.insert(QStringLiteral("id"), id);
		row.map.insert(QStringLiteral("kind"), job.value(QStringLiteral("kind")).toString());
		row.map.insert(QStringLiteral("label"), job.value(QStringLiteral("label")).toString());
		row.map.insert(QStringLiteral("status"), status);
		row.map.insert(QStringLiteral("progress"), job.value(QStringLiteral("progress")).toString());
		row.map.insert(QStringLiteral("detail"), job.value(QStringLiteral("detail")).toString());
		row.map.insert(QStringLiteral("live"), row.live);
		row.map.insert(QStringLiteral("startedAt"), row.startedAt);
		row.map.insert(QStringLiteral("finishedAt"), row.finishedAt);
		rows.append(row);
	}

	std::sort(rows.begin(), rows.end(), [](const Row& left, const Row& right) {
		if (left.live != right.live)
			return left.live;                       // 活的排在已结束的前面
		if (left.live)
			return left.startedAt < right.startedAt; // 都是活的：先开始的在前
		return left.finishedAt > right.finishedAt;   // 都结束了：最近结束的在前
	});

	QVariantList list;
	list.reserve(rows.size());
	for (const Row& row : rows)
		list.append(row.map);

	m_jobs = list;
	emit jobsChanged();
}

bool ChatController::jobRunning() const
{
	for (const QVariant& value : m_jobs) {
		const QString status = value.toMap().value(QStringLiteral("status")).toString();
		if (status == QLatin1String("running") || status == QLatin1String("stopping"))
			return true;
	}
	return false;
}

void ChatController::connectApi()
{
	if (!m_api || m_connected)
		return;
	m_connected = true;

	// follow 首帧快照（session/follow）与 mux 帧：本类的事件入口
	connect(m_agentSession, &AgentSessionClient::sessionSnapshotReady, this, &ChatController::onFollowSnapshot);
	connect(m_agentSession, &AgentSessionClient::muxFrameReceived, this, &ChatController::onMuxFrame);
	// 后台任务名册（job/list 流，随 follow 会话开关；见 AgentSessionClient）
	connect(m_agentSession, &AgentSessionClient::jobRosterReady, this, &ChatController::onJobRoster);

	// 快照里带一份全量投影（含 sessionStats / tokenUsage）——小灰字统计行的数据源；
	// 只认当前会话（迟到的帧直接丢）。
	connect(m_api, &DshApiClient::sessionProjectionsReady, this,
		[this](const QString& sessionId, int asOfSeq, const QJsonObject& values) {
			if (sessionId == m_sessionId)
				applyProjections(asOfSeq, values);
		});
}

void ChatController::onFollowSnapshot(const QString& sessionId, int cursor, const QJsonArray& records, bool hasMore)
{
	if (sessionId != m_sessionId)
		return;   // 迟到的帧：只画当前会话

	// 首帧已经到达（即使内容为空）⇒ 不再是“加载中”，QML 可以决定显示空状态还是消息。
	if (m_sessionLoading) {
		m_sessionLoading = false;
		m_sessionLoadingTimer->stop();
		emit sessionLoadingChanged();
	}

	// ---- 两条守卫：挡掉"重建风暴"（每帧重建整棵 QML 委托会打满一核并让窗口"未响应"）。
	// 空快照来自 `session/page` 的应答（没东西可给就回空），换会话的清空由 setSession() 负责
	// ⇒ 当前会话上的空 records 一律当噪声丢掉，不能拿它把消息区打空。
	if (records.isEmpty() && !m_historyRecords.isEmpty())
		return;

	// 去重：与上次完全一样就什么都不用做（cursor/hasMore 一起比，免得漏掉翻页状态更新）。
	// ⚠️ 必须排在 flushPending() 之前 —— 重复帧不该引发任何副作用。
	if (records == m_historyRecords && cursor == m_cursor && hasMore == m_hasOlder)
		return;

	flushPending();
	m_cursor = cursor;  // 记住翻页状态：throughSeq(cursor) / hasMore / 原始记录（旧页拼它前面）
	m_hasOlder = hasMore;
	m_historyRecords = records;
	emit olderChanged();

	// 轨迹视图吃的是同一份账本：快照换了它就跟着重建（两条守卫已经挡掉空帧与重复帧）
	m_trace->setRecords(records);

	rebuildFromRecords(records);
}

// 翻页：与 DshApiClient 里 follow 的兜底路径同路（session/page + beforeSeq），
// 记录同样是 {type:"event", event:{…}}，用 SessionCommands::legacyRecords 解包。
void ChatController::loadOlder()
{
	if (!m_api || m_loadingOlder || !m_hasOlder || m_sessionId.isEmpty())
		return;

	int beforeSeq = 0;  // beforeSeq = 当前内容里最早一条的 seq（会话内 seq 单调递增）
	for (const QJsonValue& value : m_historyRecords) {
		const int seq = unwrapRecord(value).value(QStringLiteral("seq")).toInt();
		if (seq > 0) {
			beforeSeq = seq;
			break;
		}
	}
	if (beforeSeq <= 0)
		return;

	m_loadingOlder = true;
	emit olderChanged();

	m_agentSession->pageSession(m_sessionId, m_cursor, 200, beforeSeq,
		[this](const QJsonObject& page) {
			m_loadingOlder = false;

			const QJsonArray older = SessionCommands::legacyRecords(
				page.value(QStringLiteral("records")).toArray());
			if (older.isEmpty()) {
				m_hasOlder = false;
				emit olderChanged();
				return;
			}

			// 服务端分页在边界上可能回带已经存在的记录；按 seq 去重后再前置。
			// 不去重的话 rebuildFromRecords 会把同一条消息画两遍，而且越上翻重复越多。
			QSet<int> seenSeqs;
			for (const QJsonValue& value : m_historyRecords) {
				const int seq = recordSeq(value);
				if (seq > 0)
					seenSeqs.insert(seq);
			}
			QJsonArray merged;  // 旧页拼到头部（服务端按 seq 升序给，直接前置即可）
			for (const QJsonValue& value : older) {
				const int seq = recordSeq(value);
				if (seq > 0 && seenSeqs.contains(seq))
					continue;
				if (seq > 0)
					seenSeqs.insert(seq);
				merged.append(value);
			}
			for (const QJsonValue& value : m_historyRecords)
				merged.append(value);
			m_historyRecords = merged;
			m_hasOlder = page.value(QStringLiteral("hasMore")).toBool();
			// 同样把整份账本交给轨迹视图（旧页已拼到头部，行序不变）
			m_trace->setRecords(m_historyRecords);

			qInfo().noquote() << "[Chat] 上翻一页 records=" << older.size() << "hasMore=" << m_hasOlder;

			emit olderPrepended();  // 先通知 QML 记下"插入前的内容高"，再重建（重建会换掉所有行）
			rebuildFromRecords(m_historyRecords);
			emit olderChanged();
		},
		[this](const DshApiClient::RpcError& error) {
			m_loadingOlder = false;
			qWarning().noquote() << "[Chat] session/page 失败:" << error.code << error.message;
			emit olderChanged();
		});
}

void ChatController::rebuildFromRecords(const QJsonArray& records)
{
	m_messages->clear();
	m_turnRow = -1;   // 整表重建：行号全失效，本轮归属也要重来

	QStringList types;  // 诊断用：首帧快照里有哪些事件类型（"空白"是没消息还是解析漏了，一眼可辨）
	QSet<int> seenSeqs;
	for (const QJsonValue& value : records) {
		const int seq = recordSeq(value);
		if (seq > 0) {
			if (seenSeqs.contains(seq))
				continue;   // 重复记录（分页/重连快照重叠）只画一次
			seenSeqs.insert(seq);
		}
		const QJsonObject event = unwrapRecord(value);
		types << event.value(QStringLiteral("type")).toString();
		applyHistoryEvent(event);
	}
	qInfo().noquote() << "[Chat] follow snapshot records=" << records.size()
		<< "types=" << types.join(QLatin1Char(','));

	// 重建之后这一轮还在跑（流式中途刷新）：后续 chunk 必须续到**重建出来的最后一条 agent 行**，
	// 否则会再新开一个气泡，把刚合并好的一轮又拆开。
	if (m_turnActive && m_messages->lastAgentRow() >= 0)
		m_turnRow = m_messages->lastAgentRow();

	m_streaming = false;
	emit streamingChanged();
	emit messagesChanged();
}

void ChatController::applyHistoryEvent(const QJsonObject& event)
{
	const QString type = event.value(QStringLiteral("type")).toString();

	if (type == QStringLiteral("user/message")) {
		if (isHiddenContextUserMessage(event))
			return;
		const QString text = extractEventText(event).trimmed();
		const QVector<MessageListModel::ImageRef> images = extractEventImages(event);
		if (!text.isEmpty() || !images.isEmpty()) {
			// 会话快照里同一 prompt 偶尔会出现两条 user/message（重连/队列重放）；
			// 历史重建时只保留连续相同的一条，避免用户消息上屏两次。
			if (!m_messages->lastRowIsUser(text, images))
				m_messages->appendUserWithImages(text, images);
			m_turnRow = -1;   // 用户开口 ⇒ 上一轮到此为止（turn/start 缺失时的兜底）
		}
		return;
	}

	if (type == QStringLiteral("turn/start") || type == QStringLiteral("turn/end")) {
		// 回合边界：下一个 assistant/message 开新气泡（一轮的典型事件序：turn/start → user → steps → turn/end）
		m_turnRow = -1;
		return;
	}

	if (type == QStringLiteral("assistant/message")) {
		// 历史回放：**一轮（turn）合成一个气泡**。引擎在一轮里每次工具调用前后都会各发一条
		// assistant/message，同一 turn 的多条续写到 m_turnRow 指向的那一行，段顺序不变。
		const QString thinking = extractThinking(event);
		const QString reply = extractReply(event);
		if (thinking.isEmpty() && reply.isEmpty())
			return;

		const int row = agentRowForTurn();
		if (!thinking.isEmpty())
			m_messages->appendChunk(row, QStringLiteral("thinking"), thinking);
		if (!reply.isEmpty())
			m_messages->appendChunk(row, QStringLiteral("reply"), reply);
		return;
	}

	if (type == QStringLiteral("tool/call")) {
		const ToolCallInfo tool = extractToolCall(event);
		if (tool.valid)
			m_messages->appendChunk(agentRowForTurn(), QStringLiteral("toolCall"),
				prettyArguments(tool.arguments), tool.name);
		return;
	}

	if (type == QStringLiteral("tool/result")) {
		const ToolResultInfo result = extractToolResult(event);
		if (result.valid)
			m_messages->appendToolResult(agentRowForTurn(), result.message);
		return;
	}

	// 斜杠命令的结果。命令**不进模型历史**：引擎只在会话日志里追加 command/run + command/done，
	// 这里把它渲染成一条系统提示行。
	if (type == QStringLiteral("deliverables/presented")) {
		applyDeliverablesEvent(event);
		return;
	}

	if (type == QStringLiteral("command/done")) {
		const QJsonObject data = event.value(QStringLiteral("data")).toObject();
		const QString text = data.value(QStringLiteral("text")).toString();
		const bool error = data.value(QStringLiteral("kind")).toString() == QLatin1String("error");
		if (!text.isEmpty())
			m_messages->appendSystem(error ? qtTrId("chat_command_failed_fmt").arg(text) : text);
		return;
	}
}

void ChatController::onMuxFrame(const QJsonObject& frame)
{
	const QJsonObject payload = frame.value(QStringLiteral("payload")).toObject();
	const QString frameType = payload.value(QStringLiteral("type")).toString();

	// 交互面板帧走单独处理器，不进消息区
	if (frameType == QStringLiteral("question/requested")) {
		handleQuestionFrame(frame);
		return;
	}
	if (frameType == QStringLiteral("approval/requested")) {
		handleApprovalFrame(frame);
		return;
	}
	if (frameType != QStringLiteral("session/event"))
		return;   // 其余帧类型不归消息区管

	const QString frameSessionId = payload.value(QStringLiteral("sessionId")).toString();
	if (!frameSessionId.isEmpty() && frameSessionId != m_sessionId)
		return;   // 只渲染当前会话，避免 A 会话的输出串到 B 会话

	const QJsonObject event = payload.value(QStringLiteral("event")).toObject();
	// 只有"回合在干活"的事件才给看门狗续期：生命周期/状态/投影类事件会不时推来，
	// 拿它们当存活信号会让看门狗永远不响。
	if (m_turnActive && isTurnProgressEvent(event.value(QStringLiteral("type")).toString()))
		m_turnWatchdog->start();

	// 轨迹视图：账本事件逐条追加（assistant/chunk 这类增量分片由 TraceModel 自己筛掉）
	m_trace->appendEvent(event);

	applyStreamEvent(event);
}

void ChatController::beginTurn()
{
	m_turnRow = -1;   // 新一轮 = 新气泡（上一轮那行不再被续写）
	m_turnHadOutput = false;
	m_turnActive = true;
	clearCancelPending();   // 新一轮开始 ⇒ 上一轮的中止状态作废（否则按钮一直停在"停止中"）
	m_turnWatchdog->start();
}

// ---- 交互面板：提问 / 审批的字段与应答结构 ----

void ChatController::handleQuestionFrame(const QJsonObject& frame)
{
	const QJsonObject payload = frame.value(QStringLiteral("payload")).toObject();
	const QString rpcId = frame.value(QStringLiteral("rpcId")).toString();
	const QString sessionId = payload.value(QStringLiteral("sessionId")).toString();
	const QJsonArray questions = payload.value(QStringLiteral("questions")).toArray();

	if (rpcId.isEmpty() || sessionId.isEmpty() || questions.isEmpty()) {
		qWarning().noquote() << "[Chat] question/requested 帧无效，已忽略";
		return;
	}

	QVariantList rows;
	for (const QJsonValue& questionValue : questions) {
		const QJsonObject question = questionValue.toObject();
		QVariantMap row;
		row.insert(QStringLiteral("id"), question.value(QStringLiteral("id")).toString());
		row.insert(QStringLiteral("header"), question.value(QStringLiteral("header")).toString());
		row.insert(QStringLiteral("question"), question.value(QStringLiteral("question")).toString());
		row.insert(QStringLiteral("multiSelect"), question.value(QStringLiteral("multiSelect")).toBool());

		QVariantList options;
		for (const QJsonValue& optionValue : question.value(QStringLiteral("options")).toArray()) {
			const QJsonObject option = optionValue.toObject();
			QVariantMap item;
			item.insert(QStringLiteral("label"), option.value(QStringLiteral("label")).toString());
			item.insert(QStringLiteral("description"), option.value(QStringLiteral("description")).toString());
			options.append(item);
		}
		row.insert(QStringLiteral("options"), options);
		rows.append(row);
	}

	m_questionRpcId = rpcId;
	m_questionSessionId = sessionId;
	m_questions = rows;
	resetAnswerSelections();
	qInfo().noquote() << "[Chat] question/requested 提问数=" << rows.size() << " session=" << sessionId;
	emit interactionChanged();
}

void ChatController::handleApprovalFrame(const QJsonObject& frame)
{
	const QJsonObject payload = frame.value(QStringLiteral("payload")).toObject();
	const QString rpcId = frame.value(QStringLiteral("rpcId")).toString();
	const QString sessionId = payload.value(QStringLiteral("sessionId")).toString();
	const QString approvalId = payload.value(QStringLiteral("approvalId")).toString();

	// approval/request 不带 approvalId，只要求 eventId + sessionId —— 不能拿 approvalId 非空做有效性门槛，
	// 否则每一条审批都会被静默丢掉。
	if (rpcId.isEmpty() || sessionId.isEmpty()) {
		qWarning().noquote() << "[Chat] approval/requested 帧无效，已忽略";
		return;
	}

	m_approvalRpcId = rpcId;
	m_approvalSessionId = sessionId;
	m_approvalId = approvalId;
	m_approvalToolName = payload.value(QStringLiteral("toolName")).toString();
	m_approvalReason = payload.value(QStringLiteral("reason")).toString();
	qInfo().noquote() << "[Chat] approval/requested 工具=" << m_approvalToolName;
	emit interactionChanged();
}

void ChatController::resetAnswerSelections()
{
	QVariantList selections;
	for (const QVariant& value : m_questions) {
		const QVariantMap question = value.toMap();
		QVariantList selected;
		const QVariantList options = question.value(QStringLiteral("options")).toList();
		// 与旧 QML 行为一致：单选且只有一个选项时默认选中。
		if (options.size() == 1 && !question.value(QStringLiteral("multiSelect")).toBool())
			selected.append(options.first().toMap().value(QStringLiteral("label")).toString());

		QVariantMap row;
		row.insert(QStringLiteral("id"), question.value(QStringLiteral("id")).toString());
		row.insert(QStringLiteral("selected"), selected);
		row.insert(QStringLiteral("custom"), QString());
		selections.append(row);
	}
	m_answerSelections = selections;
	emit answerSelectionsChanged();
}

void ChatController::toggleAnswerOption(int questionIndex, const QString& label)
{
	if (questionIndex < 0 || questionIndex >= m_questions.size()
		|| questionIndex >= m_answerSelections.size())
		return;

	const bool multi = m_questions.at(questionIndex).toMap()
		.value(QStringLiteral("multiSelect")).toBool();
	QVariantMap row = m_answerSelections.at(questionIndex).toMap();
	QVariantList selected = row.value(QStringLiteral("selected")).toList();

	const int at = selected.indexOf(label);
	if (multi) {
		if (at >= 0)
			selected.removeAt(at);
		else
			selected.append(label);
	} else {
		selected.clear();
		// 再点一次已选中的唯一选项 = 取消选择，保持旧面板行为。
		if (at < 0)
			selected.append(label);
	}
	row.insert(QStringLiteral("selected"), selected);
	m_answerSelections[questionIndex] = row;
	emit answerSelectionsChanged();
}

void ChatController::setAnswerCustom(int questionIndex, const QString& text)
{
	if (questionIndex < 0 || questionIndex >= m_answerSelections.size())
		return;
	QVariantMap row = m_answerSelections.at(questionIndex).toMap();
	row.insert(QStringLiteral("custom"), text);
	m_answerSelections[questionIndex] = row;
	emit answerSelectionsChanged();
}

void ChatController::submitAnswerSelections()
{
	submitAnswers(m_answerSelections);
}

void ChatController::submitAnswers(const QVariantList& answers)
{
	if (m_questionRpcId.isEmpty() || !m_api)
		return;

	QJsonArray answerArray;
	for (const QVariant& value : answers) {
		const QVariantMap row = value.toMap();
		QJsonObject answer;
		answer.insert(QStringLiteral("id"), row.value(QStringLiteral("id")).toString());

		QJsonArray selected;
		for (const QVariant& label : row.value(QStringLiteral("selected")).toList())
			selected.append(label.toString());
		answer.insert(QStringLiteral("selected"), selected);

		const QString custom = row.value(QStringLiteral("custom")).toString().trimmed();
		if (!custom.isEmpty())
			answer.insert(QStringLiteral("custom"), custom);

		answerArray.append(answer);
	}

	QJsonObject answer;
	answer.insert(QStringLiteral("answers"), answerArray);

	// result 就是 { answers:[…]}，没有外层 { sessionId, answer } 信封。
	m_api->respond(m_questionRpcId, answer);

	const QString rpcId = m_questionRpcId;  // 答完即收起（服务端不会就同一帧再问一次）
	m_questionRpcId.clear();
	m_questionSessionId.clear();
	m_questions.clear();
	m_answerSelections.clear();
	emit answerSelectionsChanged();
	qInfo().noquote() << "[Chat] question 已应答 rpcId=" << rpcId;
	emit interactionChanged();
}

void ChatController::respondApproval(bool allow)
{
	if (m_approvalRpcId.isEmpty() || !m_api)
		return;

	// approval waterfall 的 result 直接是 outcome 字符串，不是对象。
	m_api->respondValue(m_approvalRpcId,
		QJsonValue(allow ? QStringLiteral("allowed-once") : QStringLiteral("rejected")));

	const QString rpcId = m_approvalRpcId;
	m_approvalRpcId.clear();
	m_approvalSessionId.clear();
	m_approvalId.clear();
	m_approvalToolName.clear();
	m_approvalReason.clear();
	qInfo().noquote() << "[Chat] approval 已应答 rpcId=" << rpcId << " allow=" << allow;
	emit interactionChanged();
}

void ChatController::applyStreamEvent(const QJsonObject& event)
{
	const QString type = event.value(QStringLiteral("type")).toString();

	if (type == QStringLiteral("turn/start")) {
		// 新的一轮：下一条内容开新气泡，上一轮那行不再被续写。
		m_turnRow = -1;
		// 回合可能已被看门狗/中止在本地提前收尾，而服务端又开了一轮（取消没生效、或工具调用后接续）：
		// 必须重新激活，否则这一轮的输出点不亮按钮（setStreaming 的守卫会挡掉）也永远不会收尾。
		if (!m_turnActive)
			beginTurn();
		return;
	}

	if (type == QStringLiteral("turn/end")) {
		// 回合真正的收尾点。
		m_turnRow = -1;
		flushPending();
		// 无条件关流式态：哪怕这一轮早已被本地判死（残留态），turn/end 也是官方给的"可以收手了"信号。
		setStreaming(false);
		if (m_turnActive)
			finishTurn();
		else
			clearCancelPending();   // 中止请求到此闭环（活跃态那条由 finishTurn 负责）
		return;
	}

	if (type == QStringLiteral("assistant/message")) {
		// 收尾：已流式显示过就不重复追加，只把最后一段封存
		const QString thinking = extractThinking(event);
		const QString reply = extractReply(event);
		if (!thinking.isEmpty() || !reply.isEmpty()) {
			// ⚠️ 用 agentRowForTurn()（本轮那一行）：一轮里会来**多条** assistant/message
			//    （每次工具调用前后各一条），它们属于同一个气泡。
			const int row = agentRowForTurn();
			bufferChunk(row, QStringLiteral("thinking"), thinking);
			bufferChunk(row, QStringLiteral("reply"), reply);
			m_turnHadOutput = true;
		}
		flushPending();
		// ⚠️ 这里**刻意不** setStreaming(false) + finishTurn()：一条 assistant/message 只代表"这一段说完了"，
		//    一轮里还有下一次工具调用与后续回复；回合结束统一由 turn/end（或看门狗）负责。
		return;
	}

	if (type == QStringLiteral("assistant/chunk")) {
		const QString chunk = extractEventText(event);
		const QString chunkType = extractChunkType(event);
		if (chunk.isEmpty())
			return;

		// chunk.type 缺失时按正文处理；已知类型之外的显式类型才丢弃
		const bool thinking = chunkType == QStringLiteral("reasoning-delta");
		if (!chunkType.isEmpty() && !thinking && chunkType != QStringLiteral("text-delta"))
			return;

		bufferChunk(agentRowForTurn(), thinking ? QStringLiteral("thinking") : QStringLiteral("reply"), chunk);
		setStreaming(true);
		return;
	}

	if (type == QStringLiteral("text-chunks") || type == QStringLiteral("reasoning-chunks")) {
		const QJsonArray texts = event.value(QStringLiteral("data")).toObject()
			.value(QStringLiteral("texts")).toArray();
		const bool thinking = type == QStringLiteral("reasoning-chunks");

		const int row = agentRowForTurn();
		bool appended = false;
		for (const QJsonValue& value : texts) {
			const QString chunk = value.toString();
			if (chunk.isEmpty())
				continue;
			bufferChunk(row, thinking ? QStringLiteral("thinking") : QStringLiteral("reply"), chunk);
			appended = true;
		}
		if (appended)
			setStreaming(true);
		return;
	}

	if (type == QStringLiteral("tool/call")) {
		const ToolCallInfo tool = extractToolCall(event);
		if (tool.valid) {
			bufferChunk(agentRowForTurn(), QStringLiteral("toolCall"),
				prettyArguments(tool.arguments), tool.name);
			m_turnHadOutput = true;
			setStreaming(true);
		}
		return;
	}

	if (type == QStringLiteral("tool/result")) {
		const ToolResultInfo result = extractToolResult(event);
		if (result.valid) {
			flushPending();  // 工具结果是整段落的，直接落盘（并先把缓冲刷掉，保证顺序）
			m_messages->appendToolResult(agentRowForTurn(), result.message);
			m_turnHadOutput = true;
			setStreaming(true);
		}
		return;
	}

	if (type == QStringLiteral("deliverables/presented")) {
		applyDeliverablesEvent(event);
		return;
	}

	// 斜杠命令的结果（新追加的那条）：实时渲染成系统提示行，理由同 applyHistoryEvent
	if (type == QStringLiteral("command/done")) {
		const QJsonObject data = event.value(QStringLiteral("data")).toObject();
		const QString text = data.value(QStringLiteral("text")).toString();
		const bool error = data.value(QStringLiteral("kind")).toString() == QLatin1String("error");
		if (!text.isEmpty()) {
			flushPending();   // 系统行是整段落的，先落盘保证顺序
			m_messages->appendSystem(error ? qtTrId("chat_command_failed_fmt").arg(text) : text);
		}
		return;
	}

	// 命令注册表变了（插件运行期注册/注销命令）⇒ 让宿主重拉一次命令表：这是**通知**不是事件，只翻一个 signal。
	if (type == QStringLiteral("commands/change")) {
		emit commandRegistryChanged();
		return;
	}

	// 后端会话异常：**当前客户端收不到这条负载**（走 DshApiClient 的 `$events` type=="emit" 分支，
	// 那里只记事件名），真正兜底的是上面的回合看门狗；这条分支留着防御。
	if (type == QStringLiteral("api-session/error")) {
		qWarning().noquote() << "[Chat] backend session error:" << event.value(QStringLiteral("data"));
		flushPending();
		setStreaming(false);
		finishTurn();
		return;
	}

	// 实时流里的用户态消息：自己发的那条发送时已上屏（见 sendPrompt），这里只关心子代理回传/结束通知
	// —— 它们不画，但到达就意味着运行态变了。
	if (type == QStringLiteral("user/message")) {
		if (isPeerRelayUserMessage(event))
			emit peerActivitySeen();
		return;
	}

	// 子代理"有事发生"：目录新增、回传给父会话、结束通知 —— 都不改消息区，但要通知上层
	// **轻量重拉一次 session.list**：运行态（running）只有那条 RPC 会给，投影里没有。
	if (type == QStringLiteral("subagent/catalog")
		|| type == QStringLiteral("agent/inbox/spliced")) {
		emit peerActivitySeen();
		return;
	}

	// 其余事件类型不参与渲染。**每类只记一次**（commands/change、投影类事件量大，逐帧打日志会淹掉排障通道）
	if (!m_loggedEventTypes.contains(type)) {
		m_loggedEventTypes.insert(type);
		qInfo().noquote() << "[Chat] 忽略的事件类型（每类只记一次）:" << type;
	}
}

void ChatController::finishTurn()
{
	m_turnActive = false;
	m_turnWatchdog->stop();
	clearCancelPending();   // 回合收尾 ⇒ 中止请求到此闭环（无论有没有点过停止）
	m_lastTurnHadOutput = m_turnHadOutput;

	// 回合收尾仍无可见输出时补一条系统提示
	if (!m_turnHadOutput) {
		m_messages->appendSystem(qtTrId("chat_empty_reply_hint"));
		qInfo().noquote() << "[Chat] turn finished without visible output -> hint shown";
	}

	m_turnHadOutput = false;
	emit turnFinished();
}

int ChatController::agentRowForTurn()
{
	if (m_turnRow >= 0 && m_turnRow < m_messages->rowCount())
		return m_turnRow;

	m_turnRow = m_messages->appendAgent();
	return m_turnRow;
}

void ChatController::bufferChunk(int row, const QString& type, const QString& text, const QString& toolName)
{
	if (row < 0 || text.isEmpty())
		return;

	if (!m_pending.isEmpty()) {
		Pending& last = m_pending.last();
		if (last.row == row && last.type == type && last.toolName == toolName) {
			last.text += text;
			if (!m_flushTimer->isActive())
				m_flushTimer->start();
			return;
		}
	}

	Pending entry;
	entry.row = row;
	entry.type = type;
	entry.text = text;
	entry.toolName = toolName;
	m_pending.append(entry);

	if (!m_flushTimer->isActive())
		m_flushTimer->start();
}

void ChatController::flushPending()
{
	m_flushTimer->stop();
	if (m_pending.isEmpty())
		return;

	const QVector<Pending> pending = m_pending;
	m_pending.clear();
	for (const Pending& entry : pending)
		m_messages->appendChunk(entry.row, entry.type, entry.text, entry.toolName);
}

void ChatController::setStreaming(bool streaming)
{
	// 不变量：回合不活跃时**不允许**点亮"停止"按钮。看门狗把回合判死之后，服务端迟到的 chunk
	// 仍会走到 setStreaming(true)，按钮又变回"停止"；而 turn/end 与看门狗续期都以 m_turnActive
	// 为前置条件 ⇒ 再没有任何路径能关回 false。迟到的内容照常追加，只是不改"这一轮还活着"的判断。
	if (streaming && !m_turnActive)
		return;

	if (m_streaming == streaming)
		return;

	m_streaming = streaming;
	// ⚠️ 同时把**行级**标志写上：气泡靠它决定"整轮折叠行显不显示 / 摘要取首行还是最近完成段落"，
	//    不写就会在还在输出时显示"已完成"。
	if (m_messages) {
		// 优先"本轮那一行"（一轮一个气泡）；没有就退回最后一条 agent 行
		const int row = (m_turnRow >= 0 && m_turnRow < m_messages->rowCount())
			? m_turnRow
			: m_messages->lastAgentRow();
		if (row >= 0)
			m_messages->setStreaming(row, streaming);
	}
	emit streamingChanged();
}

// 清掉"正在中止"：turn/end 到达、中止看门狗超时、RPC 失败、换会话时都走这里。
// 单独成函数是为了不漏 emit —— 漏了会让 QML 的按钮永久停在灰态。
void ChatController::clearCancelPending()
{
	m_cancelWatchdog->stop();
	if (!m_cancelPending)
		return;

	m_cancelPending = false;
	m_cancelSentAtMs = 0;
	emit cancellingChanged();
}

// ---------------------------------------------------------------------------
// 小灰字统计行：只合并 sessionStats / tokenUsage 两块投影，按固定分组规则拼一行
// ---------------------------------------------------------------------------

void ChatController::applyProjections(int asOfSeq, const QJsonObject& values)
{
	// higher-seq-wins：seq 更小的帧丢弃
	if (asOfSeq < m_projectionSeq)
		return;
	m_projectionSeq = asOfSeq;

	const QJsonObject stats = values.value(QStringLiteral("sessionStats")).toObject();
	if (!stats.isEmpty()) {
		m_usage.turns = stats.value(QStringLiteral("turns")).toInt(m_usage.turns);
		m_usage.steps = stats.value(QStringLiteral("steps")).toInt(m_usage.steps);
		m_usage.llmMs = static_cast<qint64>(stats.value(QStringLiteral("llmMs")).toDouble(m_usage.llmMs));
		m_usage.toolMs = static_cast<qint64>(stats.value(QStringLiteral("toolMs")).toDouble(m_usage.toolMs));
		m_usage.ttftMs = static_cast<qint64>(stats.value(QStringLiteral("ttftMs")).toDouble(m_usage.ttftMs));
		m_usage.ttftSteps = stats.value(QStringLiteral("ttftSteps")).toInt(m_usage.ttftSteps);
		m_usage.decodeMs = static_cast<qint64>(stats.value(QStringLiteral("decodeMs")).toDouble(m_usage.decodeMs));
		m_usage.decodeTokens = static_cast<qint64>(stats.value(QStringLiteral("decodeTokens")).toDouble(m_usage.decodeTokens));
	}

	const QJsonObject usage = values.value(QStringLiteral("tokenUsage")).toObject();
	if (!usage.isEmpty()) {
		m_usage.uncachedInputTokens = static_cast<qint64>(usage.value(QStringLiteral("uncachedInputTokens")).toDouble(0));
		m_usage.cacheReadTokens = static_cast<qint64>(usage.value(QStringLiteral("cacheReadTokens")).toDouble(0));
		m_usage.cacheWriteTokens = static_cast<qint64>(usage.value(QStringLiteral("cacheWriteTokens")).toDouble(0));
		m_usage.outputTokens = static_cast<qint64>(usage.value(QStringLiteral("outputTokens")).toDouble(0));
	}

	// 组间 " | "，组内 " · "
	const qint64 billedInput = m_usage.uncachedInputTokens + m_usage.cacheReadTokens + m_usage.cacheWriteTokens;

	QStringList groups;
	groups << qtTrId("chat_turn_step_fmt").arg(m_usage.turns).arg(m_usage.steps);

	QStringList durations;
	if (m_usage.llmMs > 0)
		durations << qtTrId("chat_llm_label_fmt").arg(formatDuration(m_usage.llmMs));
	if (m_usage.toolMs > 0)
		durations << qtTrId("chat_tool_call_count_fmt").arg(formatDuration(m_usage.toolMs));
	if (!durations.isEmpty())
		groups << durations.join(QStringLiteral(" · "));

	QStringList speeds;
	if (m_usage.ttftSteps > 0) {
		const qint64 average = m_usage.ttftMs / m_usage.ttftSteps;
		speeds << qtTrId("chat_first_token_avg_fmt").arg(formatDuration(average));
	}
	if (m_usage.decodeMs > 0) {
		const double rate = static_cast<double>(m_usage.decodeTokens) / (static_cast<double>(m_usage.decodeMs) / 1000.0);
		speeds << qtTrId("chat_tok_per_sec_fmt").arg(formatThroughput(rate));
	}
	if (!speeds.isEmpty())
		groups << speeds.join(QStringLiteral(" · "));

	if (billedInput > 0) {
		const QString percent = formatCacheHitPercent(billedInput, m_usage.cacheReadTokens);
		if (!percent.isEmpty())
			groups << qtTrId("chat_cache_hit_fmt").arg(percent);
	}

	groups << qtTrId("chat_token_usage_fmt").arg(formatTokens(billedInput)).arg(formatTokens(m_usage.outputTokens));

	const QString line = groups.join(QStringLiteral(" | "));
	if (m_statsLine == line)
		return;

	m_statsLine = line;
	emit statsLineChanged();
}

// ---------------------------------------------------------------------------
// 模型 / 思考档位
// ---------------------------------------------------------------------------

void ChatController::refreshModelCatalog()
{
	if (!m_api || m_sessionId.isEmpty())
		return;

	ModelSelectionService::fetch(m_api, m_sessionId,
		[this](const SessionModelDirectory& directory) {
			m_directory = directory;
			// 会话记录过的选择优先（provider/model 为空 = 服务端无记录，保持目录默认）
			if (!m_overrideProvider.isEmpty() && !m_overrideModel.isEmpty()) {
				if (m_directory.findModel(m_overrideProvider, m_overrideModel)) {
					m_directory.current.provider = m_overrideProvider;
					m_directory.current.model = m_overrideModel;
					if (!m_overrideEffort.isEmpty())
						m_directory.current.reasoningEffort = m_overrideEffort;
				}
				else {
					qInfo().noquote() << "[Chat] 会话记录的模型不在目录里:" << m_overrideProvider << m_overrideModel;
				}
			}
			m_catalogLoaded = true;
			rebuildCatalog();
		},
		[this](const DshApiClient::RpcError& error) {
			// 失败不拦发送（未知，不是"没有模型"）；只清空目录让 chip 隐藏
			qWarning().noquote() << "[Chat] session/modelCatalog failed:" << error.code << error.message;
			m_directory = SessionModelDirectory();
			m_catalogLoaded = false;
			rebuildCatalog();
		});
}

void ChatController::rebuildCatalog()
{
	QVariantList groups;
	for (const ModelProviderGroup& group : m_directory.groups) {
		QVariantMap groupRow;
		groupRow.insert(QStringLiteral("id"), group.id);
		groupRow.insert(QStringLiteral("name"), group.name);

		QVariantList models;
		for (const ModelOption& option : group.models) {
			QVariantMap row;
			row.insert(QStringLiteral("id"), option.id);
			row.insert(QStringLiteral("name"), option.name);
			// 副标题：说明为空时退回 id
			row.insert(QStringLiteral("description"),
				option.description.isEmpty() ? option.id : option.description);
			row.insert(QStringLiteral("selected"),
				m_directory.current.provider == group.id && m_directory.current.model == option.id);
			models.append(row);
		}
		groupRow.insert(QStringLiteral("models"), models);
		groups.append(groupRow);
	}
	m_modelGroups = groups;

	QVariantList levels;
	for (const ReasoningLevel& level : m_directory.selectableLevels()) {
		QVariantMap row;
		row.insert(QStringLiteral("id"), level.id);
		row.insert(QStringLiteral("name"), level.name);
		row.insert(QStringLiteral("selected"), level.id == currentLevelId());
		levels.append(row);
	}
	m_reasoningLevels = levels;

	emit catalogChanged();
}

QString ChatController::currentLevelId() const
{
	const ReasoningLevel* level = m_directory.currentLevel();
	return level ? level->id : m_directory.current.reasoningEffort;
}

QString ChatController::currentModelName() const
{
	return m_directory.currentModelName();
}

QString ChatController::currentLevelName() const
{
	return m_directory.currentLevelName();
}

bool ChatController::useGenericLevels() const
{
	return m_directory.usesFallbackLevels();
}

bool ChatController::hasModels() const
{
	return m_catalogLoaded && !m_directory.groups.isEmpty();
}

void ChatController::selectModel(const QString& provider, const QString& model)
{
	if (!m_api || m_sessionId.isEmpty() || provider.isEmpty() || model.isEmpty())
		return;

	ModelSelection selection;  // 换模型清空档位（交给新模型的默认档位），成功后重拉目录
	selection.provider = provider;
	selection.model = model;

	ModelSelectionService::select(m_api, m_sessionId, selection,
		[this](const ModelSelection& selected) {
			m_overrideProvider = selected.provider;
			m_overrideModel = selected.model;
			m_overrideEffort = selected.reasoningEffort;
			refreshModelCatalog();
		},
		[this](const DshApiClient::RpcError& error) {
			qWarning().noquote() << "[Chat] session/selectModel failed:" << error.code << error.message;
			refreshModelCatalog();
		});
}

void ChatController::selectLevel(const QString& levelId)
{
	if (!m_api || m_sessionId.isEmpty() || !m_directory.current.isValid())
		return;

	// 换档位保留 provider/model，只改 reasoningEffort，不重拉目录
	ModelSelection selection = m_directory.current;
	selection.reasoningEffort = levelId;

	ModelSelectionService::select(m_api, m_sessionId, selection,
		[this](const ModelSelection& selected) {
			m_overrideEffort = selected.reasoningEffort;
			m_directory.current.reasoningEffort = selected.reasoningEffort;
			rebuildCatalog();
		},
		[this](const DshApiClient::RpcError& error) {
			qWarning().noquote() << "[Chat] session/selectModel (level) failed:" << error.code << error.message;
		});
}

void ChatController::applySessionModelSelection(const QString& provider, const QString& model, const QString& reasoningEffort)
{
	m_overrideProvider = provider;
	m_overrideModel = model;
	m_overrideEffort = reasoningEffort;

	if (!provider.isEmpty() && !model.isEmpty())
		refreshModelCatalog();
}
