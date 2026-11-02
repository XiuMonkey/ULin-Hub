// 见 MessageListModel.h：会话消息列表模型。

#include "common/session/MessageListModel.h"

#include "common/util/MarkdownSegmenter.h"

namespace
{
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

// 上面三个的公共动作：占行号 → beginInsertRows → 追加 → endInsertRows → 报数
int MessageListModel::appendRow(int kind, const QString& text)
{
	const int row = m_rows.size();
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

	// 引擎的 assistant/message 有时是**整段快照**而不是增量（同一 turn 的工具前后会重复发）。
	// 若本次文本与同类上一段相同/是其前缀，跳过；若是它的累积版本，直接替换，避免正文重复两遍。
	for (int i = entry.segments.size() - 1; i >= 0; --i) {
		Segment& previous = entry.segments[i];
		if (previous.type != type)
			continue;
		if (type == QLatin1String("toolCall") && previous.toolName != toolName)
			continue;
		if (previous.text == text || previous.text.startsWith(text))
			return;
		if (text.startsWith(previous.text)) {
			previous.text = text;
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
			last.text += text;
			last.partsReady = false;  // text 变了 ⇒ 缓存的 markdown 切分作废（见 Segment::parts）
			touchRow(row);
			return;
		}
	}

	Segment segment;
	segment.type = type;
	segment.text = text;
	segment.toolName = toolName;
	entry.segments.append(segment);
	touchRow(row);
}

void MessageListModel::appendToolResult(int row, const QString& text)
{
	if (row < 0 || row >= m_rows.size() || text.isEmpty())
		return;

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
	if (row < 0 || row >= m_rows.size() || m_rows.at(row).streaming == streaming)
		return;

	m_rows[row].streaming = streaming;
	const QModelIndex idx = index(row, 0);
	emit dataChanged(idx, idx, { StreamingRole });
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
	case StreamingRole:
		return row.streaming;
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
	names.insert(StreamingRole, "streaming");
	return names;
}
