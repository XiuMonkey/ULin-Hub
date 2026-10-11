// 见 SessionListModel.h：把工作区与会话摊平成一行一行的列表。

#include "qmlbridge/SessionListModel.h"

namespace
{
	const char kWorkspaceKind[] = "workspace";
	const char kSessionKind[] = "session";
}

SessionListModel::SessionListModel(SessionCatalog* catalog, QObject* parent)
	: QAbstractListModel(parent)
	, m_catalog(catalog)
{
}

QString SessionListModel::ungroupedWorkspaceKey()
{
	return QStringLiteral("#ungrouped");
}

// 整份重建：行数只有几十，全量重建比增量补丁简单也不会错（取舍见头文件）。
// 未分组档在两种情况下要给：①确有未归组的会话；②整表为空（首次使用 / 会话被清空 / 会话都被隐藏的分组挡着）。
// ② 不能省：这一档表头右侧的 ＋ 是唯一的"新建会话"入口（旧壳的未分组组是一直挂着的），少了它用户连一个能点的 ＋ 都看不到。
void SessionListModel::rebuild()
{
	beginResetModel();
	m_rows.clear();

	const QVector<SessionRecord> sessions = m_catalog->visibleSessions();
	const QVector<WorkspaceRecord> workspaces = m_catalog->workspaces();

	QHash<QString, QVector<SessionRecord>> byWorkspace;  // 先分桶：会话的归属以会话自身记录为准
	for (const SessionRecord& record : sessions)
		byWorkspace[record.workspaceId].append(record);

	for (const WorkspaceRecord& workspace : workspaces) {  // 有分组的按分组顺序出，没有归属的落到"未分组"档
		const QVector<SessionRecord> members = byWorkspace.value(workspace.workspaceId);
		if (members.isEmpty())
			continue;  // 空分组不占位置（旧壳也是这么做的）

		Row header;
		header.kind = QLatin1String(kWorkspaceKind);
		header.workspaceId = workspace.workspaceId;
		header.workspaceTitle = workspace.title;
		header.sessionCount = members.size();
		header.expanded = !m_collapsed.contains(workspace.workspaceId);
		m_rows.append(header);

		if (!header.expanded)
			continue;

		for (const SessionRecord& record : members) {
			Row row;
			row.kind = QLatin1String(kSessionKind);
			row.workspaceId = record.workspaceId;
			row.sessionId = record.sessionId;
			row.title = record.title;
			row.running = record.running;
			row.selected = (record.sessionId == m_selectedSessionId);
			m_rows.append(row);
		}
	}

	const QVector<SessionRecord> ungrouped = byWorkspace.value(QString());
	if (!ungrouped.isEmpty() || m_rows.isEmpty()) {  // ② 这一档表头的 ＋ 是唯一的"新建会话"入口，不能省
		const QString key = ungroupedWorkspaceKey();
		Row header;
		header.kind = QLatin1String(kWorkspaceKind);
		header.workspaceId = key;
		header.workspaceTitle = QStringLiteral("未分组");  // TODO(M6)：标题要进 ID-based 翻译（.ts 里没有对应 id，先写字面量）
		header.sessionCount = ungrouped.size();
		header.expanded = !m_collapsed.contains(key);
		m_rows.append(header);

		if (header.expanded) {
			for (const SessionRecord& record : ungrouped) {
				Row row;
				row.kind = QLatin1String(kSessionKind);
				row.sessionId = record.sessionId;
				row.title = record.title;
				row.running = record.running;
				row.selected = (record.sessionId == m_selectedSessionId);
				m_rows.append(row);
			}
		}
	}

	endResetModel();
	emit rebuilt();
}

void SessionListModel::setSelectedSession(const QString& sessionId)
{
	if (m_selectedSessionId == sessionId)
		return;

	m_selectedSessionId = sessionId;
	rebuild();
}

void SessionListModel::toggleWorkspace(const QString& workspaceId)
{
	if (workspaceId.isEmpty())
		return;

	if (m_collapsed.contains(workspaceId))
		m_collapsed.remove(workspaceId);
	else
		m_collapsed.insert(workspaceId);

	rebuild();
}

bool SessionListModel::isWorkspaceExpanded(const QString& workspaceId) const
{
	return !m_collapsed.contains(workspaceId);
}

int SessionListModel::rowCount(const QModelIndex& parent) const
{
	return parent.isValid() ? 0 : m_rows.size();
}

QVariant SessionListModel::data(const QModelIndex& index, int role) const
{
	if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
		return QVariant();

	const Row& row = m_rows.at(index.row());
	switch (role) {
	case KindRole:
		return row.kind;
	case WorkspaceIdRole:
		return row.workspaceId;
	case WorkspaceTitleRole:
		return row.workspaceTitle;
	case SessionCountRole:
		return row.sessionCount;
	case ExpandedRole:
		return row.expanded;
	case SessionIdRole:
		return row.sessionId;
	case TitleRole:
		return row.title.isEmpty() ? row.sessionId : row.title;
	case RunningRole:
		return row.running;
	case SelectedRole:
		return row.selected;
	default:
		return QVariant();
	}
}

QHash<int, QByteArray> SessionListModel::roleNames() const
{
	QHash<int, QByteArray> names;
	names.insert(KindRole, "kind");
	names.insert(WorkspaceIdRole, "workspaceId");
	names.insert(WorkspaceTitleRole, "workspaceTitle");
	names.insert(SessionCountRole, "sessionCount");
	names.insert(ExpandedRole, "expanded");
	names.insert(SessionIdRole, "sessionId");
	names.insert(TitleRole, "title");
	names.insert(RunningRole, "running");
	names.insert(SelectedRole, "selected");
	return names;
}
