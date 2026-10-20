#include "common/session/SessionCatalog.h"

#include <QCoreApplication>

// 会话/工作区列表的解析与派生逻辑。所有函数都不触碰控件，因此可以脱离 UI 单独做单元测试。

namespace
{
	const char* const kWorkspaceIdKey = "workspaceId";
	const char* const kTitleKey = "title";
	const char* const kSessionIdsKey = "sessionIds";

	// 解析一条 workspace 记录；workspaceId 为空返回 false（这种条目一律丢弃）。
	bool parseWorkspace(const QJsonObject& obj, WorkspaceRecord* out)
	{
		const QString workspaceId = obj.value(QLatin1String(kWorkspaceIdKey)).toString();
		if (workspaceId.isEmpty())
			return false;

		WorkspaceRecord record;
		record.workspaceId = workspaceId;
		const QString title = obj.value(QLatin1String(kTitleKey)).toString();
		record.title = title.isEmpty() ? workspaceId : title;

		const QJsonArray sessionIds = obj.value(QLatin1String(kSessionIdsKey)).toArray();
		for (const auto& sidValue : sessionIds) {
			const QString sid = sidValue.toString();
			if (!sid.isEmpty())
				record.sessionIds.append(sid);
		}

		*out = record;
		return true;
	}
}

void SessionCatalog::clear(bool keepArchived)
{
	m_workspaces.clear();
	m_sessions.clear();
	if (!keepArchived)
		m_archivedSessionIds.clear();   // 见头文件：清空全部会话时不能清它，否则旧会话会以"未分组"回流
	m_childSessions.clear();
	m_subagentCatalogs.clear();
	m_childParentIndex.clear();
}

// 0.1.5：工作区清单与归档集合来自 mux 上的 workspace/follow，不再随会话列表回来
void SessionCatalog::applySnapshot(const SessionListSnapshot& snapshot)
{
	if (snapshot.workspacesOk) {  // false = 本次没有任何工作区信息：必须保留现有分组，清空会把 workspace/follow 的基线抹掉
		setArchivedSessionIds(snapshot.archivedSessionIds);
		setWorkspaces(snapshot.workspaces);
	}

	if (snapshot.sessionsOk)
		setSessions(snapshot.sessions);
}

bool SessionCatalog::applyRunningStates(const QJsonArray& items)
{
	bool changed = false;
	for (const auto& item : items) {
		const QJsonObject session = item.toObject();
		const QString sid = session.value(QStringLiteral("sessionId")).toString();
		if (sid.isEmpty())
			continue;
		const bool running = session.value(QStringLiteral("running")).toBool();

		// 子代理在 m_childSessions，普通会话在 m_sessions：两边都试，谁命中改谁（同一个 id 不会同时在两处）。
		bool hit = false;
		for (SessionRecord& child : m_childSessions) {
			if (child.sessionId != sid)
				continue;
			if (child.running != running) {
				child.running = running;
				changed = true;
			}
			hit = true;
			break;
		}
		if (hit)
			continue;
		for (SessionRecord& record : m_sessions) {
			if (record.sessionId != sid)
				continue;
			if (record.running != running) {
				record.running = running;
				changed = true;
			}
			hit = true;
			break;
		}
		if (hit)
			continue;

		// 表里根本没有它（典型：刚 spawn 出来的子代理，还没赶上一次完整 session/list）：
		// 子代理补一条记录就够（running 与标题都在这行里）；普通会话不在这里补 —— 它还缺工作区/归档等字段，
		// 交给下一次完整的 refreshSessions。
		if (isSubagent(session)) {
			SessionRecord child;
			child.sessionId = sid;
			child.title = sessionTitle(session);
			child.parentSessionId = session.value(QStringLiteral("parentSessionId")).toString();
			child.running = running;
			child.subagent = true;
			m_childSessions.append(child);
			changed = true;
		}
	}
	return changed;
}

void SessionCatalog::setWorkspaces(const QJsonArray& items)
{
	m_workspaces.clear();
	for (const auto& item : items) {
		WorkspaceRecord record;
		if (parseWorkspace(item.toObject(), &record))
			m_workspaces.append(record);
	}
}

// 单条 upsert：同 id 替换，否则追加。workspace/create 之后本地先补一条，分组不在 catalog 里时
// addSession 找不到归属，照样落到未分组（workspace/upserted 是异步推送，等它到就晚了）。
void SessionCatalog::upsertWorkspace(const QJsonObject& workspace)
{
	WorkspaceRecord record;
	if (!parseWorkspace(workspace, &record))
		return;

	for (WorkspaceRecord& existing : m_workspaces) {
		if (existing.workspaceId == record.workspaceId) {
			existing = record;
			return;
		}
	}
	m_workspaces.append(record);
}

void SessionCatalog::removeWorkspace(const QString& workspaceId)
{
	if (workspaceId.isEmpty())
		return;

	for (int i = 0; i < m_workspaces.size(); ++i) {
		if (m_workspaces.at(i).workspaceId == workspaceId) {
			m_workspaces.remove(i);
			return;
		}
	}
}

// 按服务端给的完整顺序重排；没被点名的接在后面，避免丢分组。
void SessionCatalog::reorderWorkspaces(const QStringList& workspaceIds)
{
	if (workspaceIds.isEmpty())
		return;

	QVector<WorkspaceRecord> ordered;
	QSet<QString> used;
	for (const QString& workspaceId : workspaceIds) {
		for (const WorkspaceRecord& record : m_workspaces) {
			if (record.workspaceId != workspaceId)
				continue;
			ordered.append(record);
			used.insert(workspaceId);
			break;
		}
	}
	for (const WorkspaceRecord& record : m_workspaces) {
		if (!used.contains(record.workspaceId))
			ordered.append(record);
	}
	m_workspaces = ordered;
}

QString SessionCatalog::firstWorkspaceId() const
{
	for (const WorkspaceRecord& record : m_workspaces) {
		if (!record.workspaceId.isEmpty())
			return record.workspaceId;
	}
	return QString();
}

void SessionCatalog::setSessions(const QJsonArray& items)
{
	m_sessions.clear();
	m_childSessions.clear();
	for (const auto& item : items) {
		const QJsonObject session = item.toObject();
		const QString sid = session.value(QStringLiteral("sessionId")).toString();
		if (sid.isEmpty())
			continue;

		// 子代理会话不进侧栏（与旧行为一致），但顶栏"主 ↔ 子"页签要用它的标题与运行态：
		// 单独收进 m_childSessions，parentSessionId 用于"从子代理反推父会话"。
		if (isSubagent(session)) {
			SessionRecord child;
			child.sessionId = sid;
			child.title = sessionTitle(session);
			child.parentSessionId = session.value(QStringLiteral("parentSessionId")).toString();
			child.running = session.value(QStringLiteral("running")).toBool();
			child.subagent = true;
			m_childSessions.append(child);
			continue;
		}

		SessionRecord record;
		record.sessionId = sid;
		record.title = sessionTitle(session);
		record.workspaceId = workspaceFor(sid);
		record.running = session.value(QStringLiteral("running")).toBool();
		record.archived = m_archivedSessionIds.contains(sid);

		const QJsonObject projections = session.value(QStringLiteral("projections")).toObject();  // 游标回落值 + 该会话的模型选择都在这份投影里（0.1.5 起）
		record.asOfSeq = projections.value(QStringLiteral("asOfSeq")).toInt();

		const QJsonObject values = projections.value(QStringLiteral("values")).toObject();
		const QJsonObject selection = values.value(QStringLiteral("modelSelection")).toObject();
		QJsonObject chosen = selection.value(QStringLiteral("next")).toObject();  // next = 用户为下一轮选定的；没有再退回 lastUsed
		if (chosen.isEmpty())
			chosen = selection.value(QStringLiteral("lastUsed")).toObject();
		if (!chosen.isEmpty()) {
			record.hasModelSelection = true;
			record.modelProvider = chosen.value(QStringLiteral("provider")).toString();
			record.modelId = chosen.value(QStringLiteral("model")).toString();
			record.reasoningEffort = chosen.value(QStringLiteral("reasoningEffort")).toString();
		}

		// 会话列表本身也带一份（可能不完整的）投影提示：有 subagentCatalog 就顺手记下来，
		// 这样冷会话在 follow 快照到之前也能立刻画出页签。返回值这里不关心通知（setSessions
		// 的调用方本来就会整表刷新界面）。
		if (values.contains(QStringLiteral("subagentCatalog")))
			setSubagentCatalog(sid, values.value(QStringLiteral("subagentCatalog")).toArray());

		m_sessions.append(record);
	}

	// 列表重建后父会话的标题/运行态可能变了：目录反向索引保持有效即可，这里只需要
	// 保证 setSessions 清掉了旧的子代理行后，索引里没有指向已不存在孩子的过期项 ——
	// parentForSession 的兜底来自 m_childSessions，索引本身由目录驱动，无需重建。
}

void SessionCatalog::setArchivedSessionIds(const QSet<QString>& ids)
{
	m_archivedSessionIds = ids;
	for (SessionRecord& record : m_sessions)
		record.archived = m_archivedSessionIds.contains(record.sessionId);
}

bool SessionCatalog::addSession(const QString& sessionId, const QString& title, const QString& workspaceId)
{
	if (sessionId.isEmpty() || m_archivedSessionIds.contains(sessionId))
		return false;

	if (SessionRecord* existing = find(sessionId)) {
		existing->title = title;
		if (!workspaceId.isEmpty())
			existing->workspaceId = workspaceId;
		return true;
	}

	SessionRecord record;
	record.sessionId = sessionId;
	record.title = title;
	record.workspaceId = workspaceId;
	m_sessions.append(record);
	return true;
}

bool SessionCatalog::updateTitle(const QString& sessionId, const QString& title)
{
	SessionRecord* record = find(sessionId);
	if (!record)
		return false;

	record->title = title;
	return true;
}

QString SessionCatalog::titleFor(const QString& sessionId) const
{
	for (const SessionRecord& record : m_sessions) {
		if (record.sessionId == sessionId)
			return record.title.isEmpty() ? record.sessionId : record.title;
	}
	return QString();
}

int SessionCatalog::asOfSeqFor(const QString& sessionId) const
{
	if (const SessionRecord* record = find(sessionId))
		return record->asOfSeq;
	return 0;
}

bool SessionCatalog::modelSelectionFor(const QString& sessionId, QString* provider, QString* model, QString* reasoningEffort) const
{
	const SessionRecord* record = find(sessionId);
	if (!record || !record->hasModelSelection)
		return false;

	if (provider)
		*provider = record->modelProvider;
	if (model)
		*model = record->modelId;
	if (reasoningEffort)
		*reasoningEffort = record->reasoningEffort;
	return true;
}

// 会话自身记录的归属优先（addSession / addSessionToWorkspace 会显式写入），否则界面在"在工作区里新建会话"之后会把按钮挂到未分组下面
QString SessionCatalog::workspaceFor(const QString& sessionId) const
{
	if (const SessionRecord* record = find(sessionId)) {
		if (!record->workspaceId.isEmpty())
			return record->workspaceId;
	}

	for (const WorkspaceRecord& workspace : m_workspaces) {  // 其次看工作区记录（workspace/follow 的 baseline）给出的 sessionIds 归属
		if (workspace.sessionIds.contains(sessionId))
			return workspace.workspaceId;
	}
	return QString();
}

const QVector<WorkspaceRecord>& SessionCatalog::workspaces() const
{
	return m_workspaces;
}

QVector<SessionRecord> SessionCatalog::visibleSessions() const
{
	QVector<SessionRecord> visible;
	visible.reserve(m_sessions.size());
	for (const SessionRecord& record : m_sessions) {
		if (!record.archived)
			visible.append(record);
	}
	return visible;
}

QString SessionCatalog::autoSelectSessionId() const
{
	for (const SessionRecord& record : m_sessions) {
		if (!record.running)
			return record.sessionId;
	}
	return QString();
}

// ------------------------------------------------------------------
// 子代理：目录（投影）+ 子代理会话索引
// ------------------------------------------------------------------

QJsonArray SessionCatalog::subagentCatalog(const QString& sessionId) const
{
	if (sessionId.isEmpty())
		return QJsonArray();
	return m_subagentCatalogs.value(sessionId);
}

QString SessionCatalog::childTitleFor(const QString& sessionId) const
{
	if (const SessionRecord* child = findChild(sessionId))
		return child->title.isEmpty() ? child->sessionId : child->title;
	return QString();
}

bool SessionCatalog::childRunning(const QString& sessionId) const
{
	if (const SessionRecord* child = findChild(sessionId))
		return child->running;
	return false;
}

// 父会话 id：目录反推优先（引擎权威，冷父会话也有），退回 session.list 的 parentSessionId。
QString SessionCatalog::parentForSession(const QString& sessionId) const
{
	if (sessionId.isEmpty())
		return QString();

	const auto it = m_childParentIndex.constFind(sessionId);
	if (it != m_childParentIndex.constEnd() && !it.value().isEmpty())
		return it.value();

	if (const SessionRecord* child = findChild(sessionId))
		return child->parentSessionId;
	return QString();
}

// 子代理在父目录里的 mode：引擎的 subagent address 要拿它做一致性校验
// （dsh-api-session-controller 的 validateAddress：address.mode != "unknown" 时必须等于投影里的 mode）。
// 只在"父会话 → 直接子代理"这一层找；找不到返回空串，调用方按 "unknown" 兜底。
QString SessionCatalog::subagentModeFor(const QString& sessionId) const
{
	const QString parent = parentForSession(sessionId);
	if (parent.isEmpty())
		return QString();

	const QJsonArray entries = m_subagentCatalogs.value(parent);
	for (const QJsonValue& value : entries) {
		const QJsonObject entry = value.toObject();
		if (entry.value(QStringLiteral("id")).toString() != sessionId)
			continue;
		return entry.value(QStringLiteral("mode")).toString();
	}
	return QString();
}

// 投影 baseline：{ sessionId: { asOfSeq, values: {...} } }（session/control 的 baseline 形状）
bool SessionCatalog::applyProjectionBaseline(const QJsonObject& projectionsBySession)
{
	bool changed = false;
	for (auto it = projectionsBySession.constBegin(); it != projectionsBySession.constEnd(); ++it) {
		const QJsonObject values = it.value().toObject().value(QStringLiteral("values")).toObject();
		if (!values.contains(QStringLiteral("subagentCatalog")))
			continue;
		if (setSubagentCatalog(it.key(), values.value(QStringLiteral("subagentCatalog")).toArray()))
			changed = true;
	}
	return changed;
}

// 投影增量：只认 subagentCatalog（别的键是别的消费者的账本）
bool SessionCatalog::applyProjectionChange(const QString& sessionId, const QString& key, const QJsonValue& value)
{
	if (key != QStringLiteral("subagentCatalog") || sessionId.isEmpty())
		return false;
	return setSubagentCatalog(sessionId,
		value.isArray() ? value.toArray() : QJsonArray());
}

// follow 快照带的整份投影：只取 subagentCatalog（其余归 ChatController 的统计行）
bool SessionCatalog::applyFollowProjections(const QString& sessionId, const QJsonObject& values)
{
	if (sessionId.isEmpty() || !values.contains(QStringLiteral("subagentCatalog")))
		return false;
	return setSubagentCatalog(sessionId,
		values.value(QStringLiteral("subagentCatalog")).toArray());
}

bool SessionCatalog::setSubagentCatalog(const QString& sessionId, const QJsonArray& entries)
{
	if (sessionId.isEmpty())
		return false;

	const auto it = m_subagentCatalogs.constFind(sessionId);
	if (it != m_subagentCatalogs.constEnd()) {
		if (it.value() == entries)
			return false;
		if (entries.isEmpty())
			m_subagentCatalogs.remove(sessionId);  // 空目录不占内存，语义与"没有"一致
		else
			m_subagentCatalogs.insert(sessionId, entries);
	}
	else {
		if (entries.isEmpty())
			return false;
		m_subagentCatalogs.insert(sessionId, entries);
	}

	rebuildChildParentIndex();
	return true;
}

void SessionCatalog::rebuildChildParentIndex()
{
	m_childParentIndex.clear();
	for (auto it = m_subagentCatalogs.constBegin(); it != m_subagentCatalogs.constEnd(); ++it) {
		for (const QJsonValue& value : it.value()) {
			const QString childId = value.toObject().value(QStringLiteral("id")).toString();
			if (!childId.isEmpty())
				m_childParentIndex.insert(childId, it.key());
		}
	}
}

const SessionRecord* SessionCatalog::findChild(const QString& sessionId) const
{
	for (const SessionRecord& record : m_childSessions) {
		if (record.sessionId == sessionId)
			return &record;
	}
	return nullptr;
}

bool SessionCatalog::isSubagent(const QJsonObject& session)
{
	const QString origin = session.value(QStringLiteral("origin")).toString();
	return origin == QStringLiteral("subagent") || session.contains(QStringLiteral("parentSessionId"));
}

QString SessionCatalog::projectionTitle(const QJsonObject& session)
{
	const QJsonObject projections = session.value(QStringLiteral("projections")).toObject();
	const QJsonObject values = projections.value(QStringLiteral("values")).toObject();

	QString label = values.value(QStringLiteral("title")).toString();
	if (label.isEmpty())
		label = values.value(QStringLiteral("sessionTitle")).toString();
	if (label.isEmpty())
		label = values.value(QStringLiteral("session.title")).toString();
	return label;
}

QString SessionCatalog::sessionTitle(const QJsonObject& session)
{
	const QString label = projectionTitle(session);
	return label.isEmpty() ? qtTrId("session_untitled") : label;
}

QSet<QString> SessionCatalog::parseArchivedSessionIds(const QJsonObject& workspaceListValue)
{
	QSet<QString> ids;
	const QJsonArray archived = workspaceListValue.value(QStringLiteral("archivedSessionIds")).toArray();
	for (const auto& value : archived) {
		const QString id = value.toString();
		if (!id.isEmpty())
			ids.insert(id);
	}
	return ids;
}

SessionRecord* SessionCatalog::find(const QString& sessionId)
{
	for (SessionRecord& record : m_sessions) {
		if (record.sessionId == sessionId)
			return &record;
	}
	return nullptr;
}

const SessionRecord* SessionCatalog::find(const QString& sessionId) const
{
	for (const SessionRecord& record : m_sessions) {
		if (record.sessionId == sessionId)
			return &record;
	}
	return nullptr;
}