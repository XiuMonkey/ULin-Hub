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

	// 与上一段同类型就续写（流式 chunk 连着来），否则新开一段：段顺序即"交错出现"的顺序
	if (!entry.segments.isEmpty()) {
		Segment& last = entry.segments.last();
		if (last.type == type && (type != QLatin1String("toolCall") || last.toolName == toolName)) {
			last.text += text;  // 首段赋值与空串 += 等价
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
				segment.text += QStringLiteral("\n\n--- 结果 ---\n") + text;
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

// 段的 QML 视图：正文段按代码围栏切成单元（旧壳规则：正文与代码块按出现顺序排布）。
// ⚠️ 切分结果**缓存**在段里（见 Segment::parts）：这一步是纯函数、只依赖 text，而 data(SegmentsRole) 会被 QML 反复取，
// 每取一次就重切一遍全文 —— 实测 88 条历史回复把 GUI 线程卡了 ~7.8 秒；partsReady 被打回 false 时才在这里重算。
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
	names.insert(SegmentsRole, "segments");
	names.insert(StreamingRole, "streaming");
	return names;
}
