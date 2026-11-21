// 见 MessageListModel.h：会话消息列表模型。

#include "common/session/MessageListModel.h"

#include "common/util/MarkdownSegmenter.h"

#include <QCryptographicHash>
#include <QDebug>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStringList>

namespace
{
	QString textDigest(const QString& text)
	{
		return QString::fromLatin1(QCryptographicHash::hash(text.toUtf8(),
			QCryptographicHash::Sha1).toHex().left(12));
	}
	QString normalizedForDedup(const QString& text)
	{
		return text.simplified();
	}

	// 过程思考有时会把同一段带不同空白的快照重复追加。只对 thinking 做
	// “相邻重复段落”折叠，不能动最终 reply，否则会改用户可见正文。
	QString dedupeThinkingParagraphs(const QString& text)
	{
		if (text.trimmed().isEmpty())
			return text;
		const QStringList parts = text.split(QRegularExpression(QStringLiteral("\\n\\s*\\n")),
			Qt::KeepEmptyParts);
		QStringList out;
		QString previous;
		for (const QString& part : parts) {
			const QString normalized = normalizedForDedup(part);
			if (!normalized.isEmpty() && normalized == previous)
				continue;
			out.append(part);
			if (!normalized.isEmpty())
				previous = normalized;
		}
		return out.join(QStringLiteral("\n\n"));
	}

	QString kindName(int kind)
	{
		switch (kind) {
		case MessageListModel::User:
			return QStringLiteral("user");
		case MessageListModel::System:
			return QStringLiteral("system");
		default:
			return QStringLiteral("agent");
		}
	}
}

MessageListModel::MessageListModel(QObject* parent)
	: QAbstractListModel(parent)
{
}

void MessageListModel::beginBatchRebuild()
{
	beginResetModel();
	m_rows.clear();
	m_batchRebuild = true;
}

void MessageListModel::endBatchRebuild()
{
	m_batchRebuild = false;
	endResetModel();
	emit countChanged();
}

void MessageListModel::clear()
{
	beginResetModel();
	m_rows.clear();
	endResetModel();
	emit countChanged();
}

bool MessageListModel::lastRowIsUser(const QString& text, const QVector<ImageRef>& images) const
{
	if (m_rows.isEmpty())
		return false;
	const Row& last = m_rows.last();
	if (last.kind != User || last.text != text || last.images.size() != images.size())
		return false;
	for (int i = 0; i < images.size(); ++i) {
		if (last.images.at(i).attachmentId != images.at(i).attachmentId
			|| last.images.at(i).mediaType != images.at(i).mediaType)
			return false;
	}
	return true;
}

bool MessageListModel::containsUser(const QString& text) const
{
	const QString wanted = text.trimmed();
	if (wanted.isEmpty())
		return false;
	for (const Row& row : m_rows) {
		if (row.kind == User && row.text.trimmed() == wanted)
			return true;
	}
	return false;
}

int MessageListModel::appendUserWithImages(const QString& text, const QVector<ImageRef>& images)
{
	const int row = appendRow(User, text);
	if (row >= 0 && row < m_rows.size()) {
		m_rows[row].images = images;   // 图片引用在插入后才写回，需补发一次 ImagesRole，否则 QML delegate 首次读到的是空数组
		if (!images.isEmpty()) {
			const QModelIndex idx = index(row, 0);
			emit dataChanged(idx, idx, { ImagesRole });
		}
	}
	return row;
}

int MessageListModel::appendUser(const QString& text) { return appendRow(User, text); }
int MessageListModel::appendSystem(const QString& text) { return appendRow(System, text); }
int MessageListModel::appendAgent() { return appendRow(Agent, QString()); }

void MessageListModel::appendDeliverables(int row, const QVector<DeliverableRef>& files)
{
	if (row < 0 || row >= m_rows.size() || files.isEmpty())
		return;
	Row& target = m_rows[row];
	bool changed = false;
	for (const DeliverableRef& file : files) {
		if (file.path.trimmed().isEmpty())
			continue;
		bool found = false;
		for (DeliverableRef& existing : target.deliverables) {
			if (existing.path == file.path) {
				if (!file.description.isEmpty() && existing.description != file.description) {
					existing.description = file.description;
					changed = true;
				}
				found = true;
				break;
			}
		}
		if (!found) {
			target.deliverables.append(file);
			changed = true;
		}
	}
	if (changed && !m_batchRebuild) {
		const QModelIndex idx = index(row, 0);
		emit dataChanged(idx, idx, { DeliverablesRole });
	}
}

void MessageListModel::addUsage(int row, const QJsonObject& usage)
{
	if (row < 0 || row >= m_rows.size() || usage.isEmpty())
		return;

	auto addNumber = [&usage](QJsonObject& target, const QString& targetKey,
		const QStringList& sourceKeys) {
		double value = target.value(targetKey).toDouble(0);
		for (const QString& key : sourceKeys) {
			if (usage.contains(key)) {
				value += usage.value(key).toDouble(0);
				break;
			}
		}
		if (value != 0 || !target.contains(targetKey))
			target.insert(targetKey, value);
	};

	Row& target = m_rows[row];
	QJsonObject merged = target.usage;
	addNumber(merged, QStringLiteral("inputTokens"),
		{ QStringLiteral("inputTokens"), QStringLiteral("promptTokens") });
	addNumber(merged, QStringLiteral("outputTokens"),
		{ QStringLiteral("outputTokens"), QStringLiteral("completionTokens") });
	addNumber(merged, QStringLiteral("cacheReadTokens"),
		{ QStringLiteral("cacheReadTokens"), QStringLiteral("cacheReadInputTokens") });
	addNumber(merged, QStringLiteral("cacheWriteTokens"),
		{ QStringLiteral("cacheWriteTokens"), QStringLiteral("cacheWriteInputTokens") });
	addNumber(merged, QStringLiteral("reasoningTokens"),
		{ QStringLiteral("reasoningTokens") });
	addNumber(merged, QStringLiteral("totalTokens"),
		{ QStringLiteral("totalTokens"), QStringLiteral("total") });

	if (!merged.contains(QStringLiteral("totalTokens"))) {
		const double input = merged.value(QStringLiteral("inputTokens")).toDouble(0)
			+ merged.value(QStringLiteral("cacheReadTokens")).toDouble(0)
			+ merged.value(QStringLiteral("cacheWriteTokens")).toDouble(0);
		merged.insert(QStringLiteral("totalTokens"),
			input + merged.value(QStringLiteral("outputTokens")).toDouble(0));
	}

	target.usage = merged;
	if (!m_batchRebuild) {
		const QModelIndex idx = index(row, 0);
		emit dataChanged(idx, idx, { UsageRole });
	}
}

// 上面三个的公共动作：占行号 → beginInsertRows → 追加 → endInsertRows → 报数
int MessageListModel::appendRow(int kind, const QString& text)
{
	const int row = m_rows.size();
	if (m_batchRebuild) {
		Row entry;
		entry.kind = kind;
		entry.text = text;
		m_rows.append(entry);
		return row;
	}

	beginInsertRows(QModelIndex(), row, row);
	Row entry;
	entry.kind = kind;
	entry.text = text;
	m_rows.append(entry);
	endInsertRows();
	emit countChanged();
	return row;
}

void MessageListModel::appendChunk(int row, const QString& type, const QString& text, const QString& toolName)
{
	if (row < 0 || row >= m_rows.size() || text.isEmpty())
		return;

	Row& entry = m_rows[row];
	qInfo().noquote() << "[ChatChunk] in"
		<< "row=" << row << "type=" << type
		<< "len=" << text.size() << textDigest(text)
		<< "streaming=" << entry.streaming
		<< "segments=" << entry.segments.size()
		<< "replyTail=" << entry.replyTail.size();

	// 只有"最终 reply"走增量尾：流式期间只发 ReplyTailRole，稳定段与过程组一个都不重建。
	// 占位段放在末尾是因为它确实是当前最新的段；AgentBubble 始终按 segments 原序渲染，不做重排。
	if (entry.streaming && type == QLatin1String("reply")) {
		appendReplyTail(row, text);
		return;
	}

	// 任何非 reply 内容到来都说明当前正文段落已经结束：先把尾文本落回稳定段（同时清掉 QML 的流式尾），
	// 再走下面的既有追加逻辑。
	commitReplyTail(row, true);

	QString incoming = text;
	if (type == QLatin1String("thinking"))
		incoming = dedupeThinkingParagraphs(text);
	const QString incomingNorm = normalizedForDedup(incoming);

	// 重连/历史回放时，同一个 thinking/reply 快照可能隔着工具或正文再次出现。
	// 只比较“最近一段同类型”会漏掉这种跨段重复，导致同一个分析出现两个过程组。
	// thinking/reply 的重复直接丢弃；空白差异也按同一段处理。
	if (type == QLatin1String("thinking") || type == QLatin1String("reply")) {
		for (const Segment& existing : entry.segments) {
			if (existing.type == type
					&& (existing.text == incoming
						|| normalizedForDedup(existing.text) == incomingNorm)) {
				qInfo().noquote() << "[ChatChunk] drop-exact"
					<< "row=" << row << "type=" << type << textDigest(incoming);
				return;
			}
		}
	}

	// 引擎的 assistant/message 有时是**整段快照**而不是增量（同一 turn 的工具前后会重复发）。
	// 若本次文本与同类上一段相同/是其前缀，跳过；若是它的累积版本，直接替换，避免正文重复两遍。
	for (int i = entry.segments.size() - 1; i >= 0; --i) {
		Segment& previous = entry.segments[i];
		if (previous.type != type)
			continue;
		if (type == QLatin1String("toolCall") && previous.toolName != toolName)
			continue;
		const QString previousNorm = normalizedForDedup(previous.text);
		if (previous.text == incoming || previousNorm == incomingNorm
				|| previousNorm.startsWith(incomingNorm)) {
			qInfo().noquote() << "[ChatChunk] drop-prefix"
				<< "row=" << row << "type=" << type << textDigest(incoming)
				<< "prev=" << previous.text.size() << textDigest(previous.text);
			return;
		}
		if (incomingNorm.startsWith(previousNorm)) {
			qInfo().noquote() << "[ChatChunk] replace-grow"
				<< "row=" << row << "type=" << type
				<< "prev=" << previous.text.size() << "next=" << incoming.size();
			previous.text = type == QLatin1String("thinking")
				? dedupeThinkingParagraphs(incoming) : incoming;
			previous.partsReady = false;
			touchRow(row);
			return;
		}
		break;   // 只看最近一段同类型；中间隔了工具卡时，新回复本来就该新开段
	}

	// 与上一段同类型就续写（流式 chunk 连着来），否则新开一段：段顺序即"交错出现"的顺序
	if (!entry.segments.isEmpty()) {
		Segment& last = entry.segments.last();
		if (last.type == type && (type != QLatin1String("toolCall") || last.toolName == toolName)) {
			const QString lastNorm = normalizedForDedup(last.text);
			if (type == QLatin1String("thinking")) {
				// 同一份思考快照被重复追加，而不是真正的增量。
				if (!incomingNorm.isEmpty() && lastNorm.endsWith(incomingNorm)) {
					qInfo().noquote() << "[ChatChunk] drop-tail-dup"
						<< "row=" << row << "type=" << type << textDigest(incoming);
					return;
				}
				last.text = dedupeThinkingParagraphs(last.text + incoming);
			} else {
				last.text += incoming;
			}
			qInfo().noquote() << "[ChatChunk] append-last"
				<< "row=" << row << "type=" << type
				<< "len=" << incoming.size() << "total=" << last.text.size();
			last.partsReady = false;  // text 变了 ⇒ 缓存的 markdown 切分作废（见 Segment::parts）
			touchRow(row);
			return;
		}
	}

	Segment segment;
	segment.type = type;
	segment.text = incoming;
	segment.toolName = toolName;
	entry.segments.append(segment);
	qInfo().noquote() << "[ChatChunk] new-segment"
		<< "row=" << row << "type=" << type
		<< "len=" << incoming.size() << "segments=" << entry.segments.size();
	touchRow(row);
}

void MessageListModel::replaceThinkingSnapshot(int row, const QString& text)
{
	if (row < 0 || row >= m_rows.size() || text.isEmpty())
		return;

	Row& entry = m_rows[row];
	const QString incoming = dedupeThinkingParagraphs(text);
	const QString incomingNorm = normalizedForDedup(incoming);
	for (int i = entry.segments.size() - 1; i >= 0; --i) {
		Segment& segment = entry.segments[i];
		if (segment.type != QLatin1String("thinking"))
			continue;
		if (segment.text == incoming
				|| normalizedForDedup(segment.text) == incomingNorm) {
			qInfo().noquote() << "[ChatChunk] drop-thinking-snapshot"
				<< "row=" << row << textDigest(incoming);
			return;
		}
		qInfo().noquote() << "[ChatChunk] replace-thinking"
			<< "row=" << row
			<< "prev=" << segment.text.size() << textDigest(segment.text)
			<< "next=" << incoming.size() << textDigest(incoming);
		segment.text = incoming;
		segment.partsReady = false;
		touchRow(row);
		return;
	}

	// 当前 step 还没有 thinking 段（例如只收到最终快照），按普通追加落段。
	appendChunk(row, QStringLiteral("thinking"), incoming);
}

void MessageListModel::appendReplyTail(int row, const QString& text)
{
	Row& entry = m_rows[row];

	// 尾文本必须挂在最后一段 reply 上；最后一段是思考/工具时先补一个空 reply 占位，
	// 这样段顺序仍是真实输出顺序，AgentBubble 不需要做任何重排。
	const bool created = entry.segments.isEmpty()
		|| entry.segments.last().type != QLatin1String("reply");
	if (created) {
		Segment placeholder;
		placeholder.type = QLatin1String("reply");
		placeholder.partsReady = true;   // 空文本，避免 segmentsFor 在这里切分
		entry.segments.append(placeholder);
	}

	Segment& reply = entry.segments.last();
	const QString base = entry.replyTail.isEmpty() ? reply.text : entry.replyTail;
	if (base == text || base.startsWith(text))
		return;   // 同一快照重复到达 / 迟到的更短快照

	entry.replyTail = text.startsWith(base) ? text : (base + text);
	entry.tailPartsReady = false;
	const QModelIndex idx = index(row, 0);
	if (created)
		emit dataChanged(idx, idx, { SegmentsRole, ReplyTailRole, ReplyTailPartsRole });
	else
		emit dataChanged(idx, idx, { ReplyTailRole, ReplyTailPartsRole });
}

bool MessageListModel::commitReplyTail(int row, bool notify)
{
	if (row < 0 || row >= m_rows.size())
		return false;
	Row& entry = m_rows[row];
	if (entry.replyTail.isEmpty())
		return false;

	int replyIndex = -1;
	for (int i = entry.segments.size() - 1; i >= 0; --i) {
		if (entry.segments.at(i).type == QLatin1String("reply")) {
			replyIndex = i;
			break;
		}
	}

	if (replyIndex < 0) {
		// 防御：正常路径下 appendReplyTail 已经建好占位段。
		Segment reply;
		reply.type = QLatin1String("reply");
		reply.text = entry.replyTail;
		reply.partsReady = false;
		entry.segments.append(reply);
	} else {
		Segment& reply = entry.segments[replyIndex];
		reply.text = entry.replyTail;
		reply.partsReady = false;
	}
	entry.replyTail.clear();
	entry.tailParts.clear();
	entry.tailPartsReady = false;

	if (notify) {
		const QModelIndex idx = index(row, 0);
		emit dataChanged(idx, idx, { SegmentsRole, ReplyTailRole });
	}
	return true;
}

void MessageListModel::appendToolResult(int row, const QString& text)
{
	if (row < 0 || row >= m_rows.size() || text.isEmpty())
		return;

	commitReplyTail(row, true);
	Row& entry = m_rows[row];

	for (int i = entry.segments.size() - 1; i >= 0; --i) {
		if (entry.segments.at(i).type == QLatin1String("toolCall")) {
			Segment& segment = entry.segments[i];
			if (segment.text.contains(QLatin1String("\n\n--- 结果 ---\n")))
				segment.text += QLatin1Char('\n') + text;   // 同一调用多段结果就继续接
			else
				segment.text += QStringLiteral("\n\n--- 结果 ---\n") + text;   // 数据分隔符、不是界面文案：故意不进 ID-based 翻译（改了接不上历史里已存的同一标记）
			segment.partsReady = false;  // text 变了 ⇒ 缓存的 markdown 切分作废（见 Segment::parts）
			touchRow(row);
			return;
		}
	}

	appendChunk(row, QStringLiteral("toolResult"), text);
}

void MessageListModel::setStreaming(int row, bool streaming)
{
	if (row < 0 || row >= m_rows.size())
		return;

	Row& entry = m_rows[row];
	bool tailCommitted = false;
	if (!streaming)
		tailCommitted = commitReplyTail(row, false);

	if (entry.streaming == streaming) {
		// 正常不会走到这里；若收到"已经是 false、但还残留尾文本"的异常收尾，也要把稳定段推给 QML。
		if (tailCommitted && !m_batchRebuild) {
			const QModelIndex idx = index(row, 0);
			emit dataChanged(idx, idx, { SegmentsRole, ReplyTailRole });
		}
		return;
	}

	entry.streaming = streaming;
	if (m_batchRebuild)
		return;
	const QModelIndex idx = index(row, 0);
	if (streaming) {
		emit dataChanged(idx, idx, { StreamingRole });
		return;
	}

	// 收尾：commitReplyTail 已把尾文本写进稳定 reply 段，必须同时通知 SegmentsRole，
	// 否则 QML 只清掉 tail、不重新取 segments，最终正文会消失。
	emit dataChanged(idx, idx, { StreamingRole, ReplyTailRole, SegmentsRole });
}

int MessageListModel::lastAgentRow() const
{
	if (m_rows.isEmpty())
		return -1;
	return m_rows.last().kind == Agent ? m_rows.size() - 1 : -1;
}

// 段的 QML 视图：正文段按代码围栏切成单元（正文与代码块按出现顺序排布）。
// ⚠️ 切分结果**缓存**在段里（见 Segment::parts）：纯函数、只依赖 text，而 data(SegmentsRole) 会被 QML 反复取，
// 每取一次就重切一遍全文；partsReady 被打回 false 时才在这里重算。
QVariantList MessageListModel::segmentsFor(const Row& row) const
{
	QVariantList list;
	for (const Segment& segment : row.segments) {
		QVariantMap map;
		map.insert(QStringLiteral("type"), segment.type);
		map.insert(QStringLiteral("text"), segment.text);
		map.insert(QStringLiteral("toolName"), segment.toolName);

		if (segment.type == QLatin1String("reply")) {
			if (!segment.partsReady) {
				segment.parts = MarkdownSegmenter::split(segment.text);
				segment.partsReady = true;
			}
			map.insert(QStringLiteral("parts"), segment.parts);
		}

		list.append(map);
	}
	return list;
}

void MessageListModel::touchRow(int row)
{
	if (m_batchRebuild)
		return;
	const QModelIndex idx = index(row, 0);
	emit dataChanged(idx, idx, { SegmentsRole, TextRole });
}

void MessageListModel::invalidateSegmentParts()
{
	if (m_rows.isEmpty())
		return;

	bool touched = false;
	for (Row& row : m_rows) {
		for (Segment& segment : row.segments) {
			if (!segment.partsReady)
				continue;
			segment.partsReady = false;
			segment.parts.clear();
			touched = true;
		}
	}
	if (!touched)
		return;

	// ⚠️ 不能 beginResetModel：本模型的更新策略是"只发 dataChanged"，reset 会丢滚动位置与 delegate。
	emit dataChanged(index(0, 0), index(m_rows.size() - 1, 0), { SegmentsRole });
}

int MessageListModel::rowCount(const QModelIndex& parent) const
{
	if (parent.isValid())
		return 0;
	return m_rows.size();
}

QVariant MessageListModel::data(const QModelIndex& index, int role) const
{
	if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
		return QVariant();

	const Row& row = m_rows.at(index.row());
	switch (role) {
	case KindRole:
		return kindName(row.kind);
	case TextRole:
		return row.text;
	case SegmentsRole:
		return segmentsFor(row);
	case ReplyTailRole:
		return row.replyTail;
	case ReplyTailPartsRole:
		if (!row.tailPartsReady) {
			row.tailParts = row.replyTail.isEmpty() ? QVariantList()
				: MarkdownSegmenter::split(row.replyTail);
			row.tailPartsReady = true;
		}
		return row.tailParts;
	case ImagesRole: {
		QVariantList images;
		for (const ImageRef& image : row.images) {
			QVariantMap map;
			map.insert(QStringLiteral("id"), image.attachmentId);
			map.insert(QStringLiteral("mediaType"), image.mediaType);
			map.insert(QStringLiteral("preview"), image.previewUrl);
			images.append(map);
		}
		return images;
	}
	case DeliverablesRole: {
		QVariantList files;
		for (const DeliverableRef& file : row.deliverables) {
			const QFileInfo info(file.path);
			QVariantMap map;
			map.insert(QStringLiteral("path"), file.path);
			map.insert(QStringLiteral("name"), info.fileName().isEmpty() ? file.path : info.fileName());
			map.insert(QStringLiteral("suffix"), info.suffix().toLower());
			map.insert(QStringLiteral("description"), file.description);
			files.append(map);
		}
		return files;
	}
	case StreamingRole:
		return row.streaming;
	case UsageRole:
		return row.usage.toVariantMap();
	default:
		return QVariant();
	}
}

QHash<int, QByteArray> MessageListModel::roleNames() const
{
	QHash<int, QByteArray> names;
	names.insert(KindRole, "kind");
	names.insert(TextRole, "text");
	names.insert(ImagesRole, "images");
	names.insert(SegmentsRole, "segments");
	names.insert(ReplyTailRole, "replyTail");
	names.insert(ReplyTailPartsRole, "replyTailParts");
	names.insert(StreamingRole, "streaming");
	names.insert(DeliverablesRole, "deliverables");
	names.insert(UsageRole, "usage");
	return names;
}
