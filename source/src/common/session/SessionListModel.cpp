// 见 SessionListModel.h：把工作区与会话摊平成一行一行的列表。

#include "common/session/SessionListModel.h"

#include <QVariantMap>

namespace
{
	const char kWorkspaceKind[] = "workspace";
	const char kPinnedKind[] = "pinned";
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

QString SessionListModel::pinnedWorkspaceKey()
{
	return QStringLiteral("#pinned");
}

// 整份重建：行数只有几十，全量重建比增量补丁简单也不会错（取舍见头文件）。
// 顺序：置顶段（仅当有可见的置顶会话）→ 各工作区分组 → 未分组档。
// 未分组档在两种情况下要给：①确有未归组的会话；②整表为空（首次使用 / 会话被清空 / 会话都被隐藏的分组挡着）。
// ② 不能省：这一档表头右侧的 ＋ 是唯一的"新建会话"入口，少了它用户连一个能点的 ＋ 都看不到。
// 搜索时（filterText 非空）规则稍变：只保留"有命中会话"的分组；一个都没有时列表为空（由 QML 画空状态），
// 未分组档也不再硬塞 —— 否则搜不到东西时界面会剩下一个空表头，比空列表更费解。
void SessionListModel::rebuild()
{
	beginResetModel();
	m_rows.clear();

	const QVector<SessionRecord> sessions = m_catalog->visibleSessions();
	const QVector<WorkspaceRecord> workspaces = m_catalog->workspaces();
	const QStringList pinnedOrder = m_catalog->pinnedSessionIds();

	const QString filter = m_filterText;
	const bool filtering = !filter.isEmpty();

	// 过滤口径与 TitleRole 一致：标题为空时显示的是 sessionId，那就按 sessionId 匹配。
	// 只在 filtering == true 时调用（matchesFilter 本身不再判空）。
	auto matchesFilter = [&filter](const SessionRecord& record) {
		const QString title = record.title.isEmpty() ? record.sessionId : record.title;
		return title.contains(filter, Qt::CaseInsensitive);
	};

	// 过滤一组会话；不过滤时原样返回，不逐条走 lambda。置顶段 / 工作区分组 / 未分组共用这一份口径。
	auto matching = [&filter, &matchesFilter](const QVector<SessionRecord>& candidates) {
		if (filter.isEmpty())
			return candidates;
		QVector<SessionRecord> matched;
		matched.reserve(candidates.size());
		for (const SessionRecord& record : candidates) {
			if (matchesFilter(record))
				matched.append(record);
		}
		return matched;
	};

	QHash<QString, QVector<SessionRecord>> byWorkspace;   // 先分桶：会话的归属以会话自身记录为准；visibleById 给置顶段按 id 取记录用（顺带天然滤掉已归档的）
	QHash<QString, SessionRecord> visibleById;
	for (const SessionRecord& record : sessions) {
		byWorkspace[record.workspaceId].append(record);
		visibleById.insert(record.sessionId, record);
	}

	QVector<SessionRecord> pinnedSessions;
	pinnedSessions.reserve(pinnedOrder.size());
	for (const QString& id : pinnedOrder) {
		const auto it = visibleById.constFind(id);
		if (it == visibleById.constEnd())
			continue;
		const SessionRecord& record = it.value();
		if (filtering && !matchesFilter(record))
			continue;
		pinnedSessions.append(record);
	}

	if (!pinnedSessions.isEmpty()) {
		// 置顶段标题留空：文案属于界面层，由 SessionBar 用 qsTrId("sessionBar_pinned") 显示，
		// 不在模型里做 qtTrId + 中文硬兜底（英文界面缺 .qm 时会露中文，也不符合"文案只在 .ts"分层）。
		const Row header = makeHeaderRow(kPinnedKind, pinnedWorkspaceKey(),
			QString(), pinnedSessions.size());
		m_rows.append(header);

		if (header.expanded) {
			for (const SessionRecord& record : pinnedSessions)
				m_rows.append(makeSessionRow(record));
		}
	}

	for (const WorkspaceRecord& workspace : workspaces) {  // 有分组的按分组顺序出，没有归属的落到"未分组"档
		const QVector<SessionRecord> members = matching(byWorkspace.value(workspace.workspaceId));
		if (filtering && members.isEmpty())   // 搜索时没命中的分组不占位置（否则搜出来一串只有表头的空组）
			continue;

		// ⚠️ 空工作区也必须有行：跳过它，"新建工作区"看起来永远不生效 —— 服务端确实建了、也写进了
		// harness/storages/workspace.json，界面上却只看到有会话的分组；用户也没法在里面建第一个会话。
		const Row header = makeHeaderRow(kWorkspaceKind, workspace.workspaceId, workspace.title, members.size());
		m_rows.append(header);

		if (!header.expanded)
			continue;

		for (const SessionRecord& record : members)
			m_rows.append(makeSessionRow(record));
	}

	const QVector<SessionRecord> ungrouped = matching(byWorkspace.value(QString()));
	const bool showUngrouped = filtering ? !ungrouped.isEmpty()
		: (!ungrouped.isEmpty() || m_rows.isEmpty());  // ② 这一档表头的 ＋ 是唯一的"新建会话"入口，不能省
	if (showUngrouped) {
		const Row header = makeHeaderRow(kWorkspaceKind, ungroupedWorkspaceKey(),
			qtTrId("sessionBar_ungrouped"), ungrouped.size());
		m_rows.append(header);

		if (header.expanded) {
			for (const SessionRecord& record : ungrouped)
				m_rows.append(makeSessionRow(record));
		}
	}

	endResetModel();
	emit rebuilt();
}

SessionListModel::Row SessionListModel::makeSessionRow(const SessionRecord& record) const   // 会话行内容：置顶段 / 工作区分组 / 未分组三处共用，避免同一份赋值抄三遍漏字段
{
	Row row;
	row.kind = QLatin1String(kSessionKind);
	row.workspaceId = record.workspaceId;   // 置顶行也带真实归属：右键/建会话等操作仍能按分组处理
	row.sessionId = record.sessionId;
	row.title = record.title;
	row.running = record.running;
	row.selected = (record.sessionId == m_selectedSessionId);
	row.updatedAtMs = record.updatedAtMs;
	return row;
}

// 表头行内容：工作区 / 置顶 / 未分组三处共用。expanded 统一按 workspaceId 查折叠状态 ——
// 工作区的是服务端 id，置顶段与未分组档的是本模型的界面 key（见头文件），三者都只当折叠键用。
SessionListModel::Row SessionListModel::makeHeaderRow(const char* kind, const QString& workspaceId,
	const QString& workspaceTitle, int sessionCount) const
{
	Row header;
	header.kind = QLatin1String(kind);
	header.workspaceId = workspaceId;
	header.workspaceTitle = workspaceTitle;
	header.sessionCount = sessionCount;
	header.expanded = !m_collapsed.contains(workspaceId);
	return header;
}

void SessionListModel::setSelectedSession(const QString& sessionId)
{
	if (m_selectedSessionId == sessionId)
		return;

	m_selectedSessionId = sessionId;
	rebuild();
}

// 搜索过滤：会话行 / 置顶段 / 工作区分组都随 filterText 一起重建（见 rebuild）。
// 空串恢复全部；其余大小写不敏感匹配标题。置顶段里的会话如果被过滤掉，整段就不出。
void SessionListModel::setFilterText(const QString& text)
{
	if (m_filterText == text)
		return;

	m_filterText = text;
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

QVariantList SessionListModel::workspaceItems() const
{
	QVariantList items;
	for (const Row& row : m_rows) {
		if (row.kind != QLatin1String(kWorkspaceKind))
			continue;

		QVariantMap item;
		item.insert(QStringLiteral("workspaceId"), row.workspaceId);
		item.insert(QStringLiteral("title"), row.workspaceTitle);
		item.insert(QStringLiteral("count"), row.sessionCount);
		item.insert(QStringLiteral("ungrouped"), row.workspaceId == ungroupedWorkspaceKey());
		items.append(item);
	}
	return items;
}

QString SessionListModel::firstSessionForWorkspace(const QString& workspaceId) const
{
	if (workspaceId.isEmpty())
		return QString();

	const QString wanted = workspaceId == ungroupedWorkspaceKey() ? QString() : workspaceId;
	for (const Row& row : m_rows) {
		if (row.kind == QLatin1String(kSessionKind) && row.workspaceId == wanted)
			return row.sessionId;
	}
	return QString();
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
	case UpdatedAtRole:
		return row.updatedAtMs;
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
	names.insert(UpdatedAtRole, "updatedAtMs");   // QML（SessionBar）按 model.updatedAtMs 读
	return names;
}
