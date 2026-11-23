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

// 工具产出的可交付文件：回合未结束时先攒着，等回合收尾统一归到最后一条 agent 行。
void ChatController::applyDeliverablesEvent(const QJsonObject& event, bool deferUntilTurnEnd)
{
	const QVector<MessageListModel::DeliverableRef> files = extractEventDeliverables(event);
	if (files.isEmpty() || !m_messages)
		return;
	if (deferUntilTurnEnd && m_turnActive) {
		if (m_pendingDeliverablesRow < 0)
			m_pendingDeliverablesRow = agentRowForTurn();
		m_pendingDeliverables += files;
		return;
	}
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

} // namespace

void ChatController::addImageFiles(const QVariantList& urls)
{
	addFiles(urls);   // 旧入口保留：图片继续内联，普通文件也一起接住
}

void ChatController::addQuote(const QString& text)
{
	const QString trimmed = text.trimmed();
	if (trimmed.isEmpty())
		return;
	QVariantMap entry;
	entry.insert(QStringLiteral("kind"), QStringLiteral("quote"));
	entry.insert(QStringLiteral("text"), trimmed);
	m_pendingImages.append(entry);
	emit pendingAttachmentsChanged();
}

void ChatController::addBrowserContext(const QVariantMap& card)
{
	QVariantMap entry = card;
	entry.insert(QStringLiteral("kind"), QStringLiteral("browser"));
	m_pendingImages.append(entry);
	emit pendingAttachmentsChanged();
}

// 读文件并归类：图片内联成 base64 并生成预览，其余文件只记路径/大小，发送时附在正文里。
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
		entry.insert(QStringLiteral("sizeLabel"), formatByteSize(info.size()));
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
			// 必须排队发：扩展 RPC 可能是同步回调，而调用方是 QML 的 source 绑定。
			// 同步 emit 会让 attachmentRevision 在绑定求值过程中变化，QML 判定"绑定写了
			// 自己依赖的属性"→ 直接废弃 source 绑定，历史图片永远加载不出来。
			QMetaObject::invokeMethod(this, [this, attachmentId]() {
				emit attachmentSourceReady(attachmentId);
			}, Qt::QueuedConnection);
		},
		[this, attachmentId](const DshApiClient::RpcError& error) {
			m_attachmentInFlight.remove(attachmentId);
			qWarning().noquote() << "[Chat] session/attachment failed:" << error.code << error.message;
		});
	return QString();
}

