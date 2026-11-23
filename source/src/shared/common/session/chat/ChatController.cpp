#include "common/util/Debugger.h"
#include "common/util/Format.h"
#include "common/util/Json.h"
#include "common/session/EventRecord.h"
#include "common/session/ChatController.h"
#include "common/session/MessageListModel.h"
#include "common/agent/AgentSessionClient.h"
#include "common/session/TraceModel.h"

#include "common/agent/dsh/DshApiClient.h"
#include "common/agent/AgentEventParser.h"
#include "common/util/ConnectionManager.h"
#include "network/SessionCommands.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDebug>
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
#include "common/session/chat/ChatControllerInternal.h"

using namespace chatctl;

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

	// 输入默认接收者：没有后端接管时，原始 Prompt 由本类发送。
	dshRegister("agent.prompt",
		this, &ChatController::promptRequested,
		this, &ChatController::sendRawPrompt);

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

	// 本轮耗时：等待/流式期间每秒刷新一次；turn 结束后停止，最终值写进对应 Agent 行。
	m_turnElapsedTimer = new QTimer(this);
	m_turnElapsedTimer->setInterval(1000);
	connect(m_turnElapsedTimer, &QTimer::timeout, this, &ChatController::updateTurnElapsed);

	m_turnWatchdog = new QTimer(this);
	m_turnWatchdog->setSingleShot(true);
	m_turnWatchdog->setInterval(kTurnWatchdogMs);
	connect(m_turnWatchdog, &QTimer::timeout, this, [this]() {
		if (!m_turnActive)
			return;
		// 本轮已经吐过内容 ⇒ 后端在干活，只是暂时静默（长工具执行 / 模型思考）。
		// 这时判死会 setStreaming(false) + m_turnActive=false，之后迟到的 chunk 仍会画进
		// 气泡，但 setStreaming(true) 被守卫挡掉 ⇒ 停止键永久变发送键。收尾交给 turn/end。
		if (m_turnHadOutput) {
			flushPending();
			m_turnWatchdog->start();   // 正常静默，不逐次打日志；真判死时下面的 warning 仍在
			return;
		}
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
void ChatController::submitPrompt(const QString& sessionId, const QString& text, const std::function<void()>& restartServer, const std::function<void(const QString&)>& setStatusLine)
{
	m_restartServer = restartServer;
	m_setStatusLine = setStatusLine;
	emit promptRequested(sessionId, text);
}

void ChatController::sendRawPrompt(const QString& sessionId, const QString& text)
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
	QStringList browserNotes;
	QStringList quoteNotes;
	for (const QVariant& value : m_pendingImages) {
		const QVariantMap entry = value.toMap();
		if (entry.value(QStringLiteral("kind")).toString() == QLatin1String("quote")) {
			const QString quote = entry.value(QStringLiteral("text")).toString();
			if (!quote.trimmed().isEmpty()) {
				QString normalized = quote;
				normalized.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
				normalized.replace(QLatin1Char('\r'), QLatin1Char('\n'));
				const QStringList lines = normalized.split(QLatin1Char('\n'));
				QString block;
				for (const QString& line : lines) {
					if (!block.isEmpty())
						block += QLatin1Char('\n');
					block += QStringLiteral("> ") + line;
				}
				quoteNotes.append(block);
			}
			continue;
		}
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
		if (entry.value(QStringLiteral("kind")).toString() == QLatin1String("browser")) {
			const QString ref = entry.value(QStringLiteral("ref")).toString();
			const QString title = entry.value(QStringLiteral("title")).toString();
			const QString url = entry.value(QStringLiteral("url")).toString();
			const QString selector = entry.value(QStringLiteral("selector")).toString();
			browserNotes.append(QStringLiteral("[浏览器上下文] %1\nURL: %2\n位置: %3\n请使用 ulin-kit 的 browser_context 工具读取（ref: %4）。").arg(title.isEmpty() ? url : title, url, selector, ref));
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
	if (!quoteNotes.isEmpty()) {
		const QString quoted = quoteNotes.join(QStringLiteral("\n\n"));
		promptText = promptText.isEmpty() ? quoted : (quoted + QStringLiteral("\n\n") + promptText);
	}
	if (!fileNotes.isEmpty()) {
		if (!promptText.isEmpty())
			promptText += QStringLiteral("\n\n");
		promptText += QStringLiteral("附件：\n") + fileNotes.join(QLatin1Char('\n'));
	}
	if (!browserNotes.isEmpty()) {
		if (!promptText.isEmpty())
			promptText += QStringLiteral("\n\n");
		promptText += browserNotes.join(QLatin1Char('\n'));
	}

	beginTurn();
	if (m_messages)
		m_messages->appendUserWithImages(promptText, imageRefs);
	m_rowSeqs.append(0);   // 本地刚发的消息；服务端记录回来前不知道 seq
	// 运行期唯一一次收束判断：用户发消息时检查，行数 >60 就收到 60。
	// 不再跟随流式事件/新增行频繁触发，避免收束和高频事件互相打架。
	enforceRowCap();
	// 快照/压缩重建可能比 prompt 先到；先记住这条本地消息，重建时按需补回。
	m_pendingUserText = promptText;
	m_pendingUserImages = imageRefs;

	if (Debugger::attachProbeEnabled()) {
		const QJsonObject payload = SessionCommands::sessionPrompt(sessionId, promptText, imageParts);
		const QByteArray json = QJsonDocument(payload).toJson(QJsonDocument::Compact);
		qInfo().noquote() << "[Chat] prompt payload bytes:" << json.size()
			<< "images:" << imageParts.size()
			<< "preview:" << QString::fromUtf8(json).left(240);
	}
	m_api->callMethod(QStringLiteral("session/prompt"),
		SessionCommands::sessionPrompt(sessionId, promptText, imageParts),
		[](const QJsonObject&) {},
		[this, promptText, sessionId, imageParts](const DshApiClient::RpcError& error) {
			// 失败文案用 chat_send_failed_fmt；同时记一行，便于看清服务端拒绝的原因
			qWarning().noquote() << "[Chat] session/prompt failed:" << error.code << error.message;

			// 会话写句柄还挂在已经退出的旧进程上（服务端不会因为客户端消失就立刻释放）：
			// 重启内置服务端清掉占用，随后把这条消息补发一次，否则永远发不出消息。
			const bool busy = error.message.contains(QLatin1String("already owned"))
				|| error.message.contains(QLatin1String("resume failed"));
			if (busy && !m_pendingRetry && m_restartServer) {
				m_pendingRetry = true;
				m_retryText = promptText;
				m_retrySessionId = sessionId;
				m_retryImages = imageParts;
				if (m_messages) {
					m_messages->appendSystem(qtTrId("chat_session_busy_restarting"));
				}
				if (m_setStatusLine)
					m_setStatusLine(qtTrId("chat_session_busy_status"));
				m_restartServer();
				return;
			}

			const QString text = qtTrId("chat_send_failed_fmt").arg(error.code, error.message);
			if (m_setStatusLine)
				m_setStatusLine(text);
			if (m_messages)
				m_messages->appendSystem(text);
		});
}

void ChatController::appendSystemNotice(const QString& text, bool error)
{
	if (!m_messages || text.trimmed().isEmpty())
		return;
	flushPending();
	m_messages->appendSystem(error
		? qtTrId("chat_command_failed_fmt").arg(text)
		: text);
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
	// 本轮输出标志与统计行（用量小灰字）也是会话级的：前者不清会污染下一轮"空回复"判定；
	// 后者必须从头算 —— 新会话的投影 seq 从低位重新开始，不重置的话 applyProjections 的
	// higher-seq-wins 会把新会话的帧全丢掉，用量行从此空白。
	m_turnHadOutput = false;
	m_pendingDeliverables.clear();
	m_pendingDeliverablesRow = -1;
	m_projectionSeq = -1;
	m_usage = UsageStats();
	if (!m_statsLine.isEmpty()) {
		m_statsLine.clear();
		emit statsLineChanged();
	}
	m_loggedEventTypes.clear();
	m_pendingUserText.clear();
	m_pendingUserImages.clear();
	m_messages->clear();
	m_trace->clear();
	// 翻页账本也是会话级的：不清的话，新会话的首帧空快照会被"空帧守卫"当噪声丢掉，
	// 而且 m_cursor/hasMore 还是上一个会话的。
	m_historyRecords = QJsonArray();
	m_cursor = 0;
	m_hasOlder = false;
	m_loadingOlder = false;
	m_rowsTrimmed = false;
	m_rowSeqs.clear();
	emit olderChanged();
	// 名册是会话级的：不先清掉的话，切到新会话的头几百毫秒里顶栏还挂着上一个会话的任务
	if (!m_jobs.isEmpty()) {
		m_jobs.clear();
		emit jobsChanged();
	}
	if (!m_todos.isEmpty()) {
		m_todos.clear();
		emit todosChanged();
	}
	if (!m_goal.isEmpty()) {
		m_goal.clear();
		emit goalChanged();
	}
	emit streamingChanged();
	emit messagesChanged();
}

int ChatController::messageCount() const
{
	return m_messages ? m_messages->rowCount() : 0;
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
	// 必须听 AgentSessionClient 的，不能只听 m_api 的：AgentSessionClient 既转发
	// m_api 的接管态帧（见其构造函数里的 connect），又负责真实 DSH 的 session/follow
	// 快照投影（handleSessionItem 里 emit）。只听 m_api 的话，真实 DSH（非接管）这条路
	// 永远不会来帧 —— 小灰字会一直是空的。
	connect(m_agentSession, &AgentSessionClient::sessionProjectionsReady, this,
		[this](const QString& sessionId, int asOfSeq, const QJsonObject& values) {
			if (sessionId == m_sessionId)
				applyProjections(asOfSeq, values);
		});
}

