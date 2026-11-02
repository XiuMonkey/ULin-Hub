#include "common/session/SessionCatalog.h"

#include <QCoreApplication>
#include <QDir>

namespace   // 会话/工作区列表的解析与派生逻辑。所有函数都不触碰控件，因此可以脱离 UI 单独做单元测试
{
	const char* const kWorkspaceIdKey = "workspaceId";
	const char* const kTitleKey = "title";
	const char* const kSessionIdsKey = "sessionIds";
	const char* const kPathKey = "path";   // workspace view 里的根目录；会话 header 里的启动目录叫 cwd（session/list 会原样带回来）
	const char* const kCwdKey = "cwd";

	// 路径比较口径：Windows 大小写不敏感、`/` 与 `\` 等价、尾部斜杠不算区别。
	// 不解析 8.3 短名与符号链接 —— 会话 header 与 workspace.path 由同一个引擎写出来，
	// 同一台机器上是同一形态，这里只做"字符串层面能对上"的归一化。
	bool samePath(const QString& left, const QString& right)
	{
		if (left.isEmpty() || right.isEmpty())
			return false;

		QString a = QDir::fromNativeSeparators(left);
		QString b = QDir::fromNativeSeparators(right);
		while (a.size() > 1 && a.endsWith(QLatin1Char('/')))
			a.chop(1);
		while (b.size() > 1 && b.endsWith(QLatin1Char('/')))
			b.chop(1);
		return a.compare(b, Qt::CaseInsensitive) == 0;
	}

	bool parseWorkspace(const QJsonObject& obj, WorkspaceRecord* out)   // 解析一条 workspace 记录；workspaceId 为空返回 false（这种条目一律丢弃）
	{
		const QString workspaceId = obj.value(QLatin1String(kWorkspaceIdKey)).toString();
		if (workspaceId.isEmpty())
			return false;

		WorkspaceRecord record;
		record.workspaceId = workspaceId;
		const QString title = obj.value(QLatin1String(kTitleKey)).toString();
		record.title = title.isEmpty() ? workspaceId : title;
		record.path = obj.value(QLatin1String(kPathKey)).toString();

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
	if (!keepArchived) {
		m_archivedSessionIds.clear();   // 见头文件：清空全部会话时不能清它，否则旧会话会以"未分组"回流
		m_pinnedSessionIds.clear();     // 置顶只影响排序/分组、不决定可见性；下一次 workspace/follow baseline 会重新给
	}
	m_childSessions.clear();
	m_subagentCatalogs.clear();
	m_childParentIndex.clear();
}

// 0.1.5：工作区清单、归档集合与置顶集合都来自 mux 上的 workspace/follow，不再随会话列表回来
void SessionCatalog::applySnapshot(const SessionListSnapshot& snapshot)
{
	if (snapshot.workspacesOk) {  // false = 本次没有任何工作区信息：必须保留现有分组，清空会把 workspace/follow 的基线抹掉
		setArchivedSessionIds(snapshot.archivedSessionIds);
		setPinnedSessionIds(snapshot.pinnedSessionIds);
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

		bool hit = false;   // 子代理在 m_childSessions，普通会话在 m_sessions：两边都试，谁命中改谁（同一个 id 不会同时在两处）
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

void SessionCatalog::reorderWorkspaces(const QStringList& workspaceIds)   // 按服务端给的完整顺序重排；没被点名的接在后面，避免丢分组
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
		record.cwd = session.value(QLatin1String(kCwdKey)).toString();   // header.cwd（session/list 原样带回）：归属的第三条依据，见 workspaceFor
		record.workspaceId = workspaceFor(sid, record.cwd);
		record.running = session.value(QStringLiteral("running")).toBool();
		record.archived = m_archivedSessionIds.contains(sid);

		// 最近活动时间：session/list 的 updatedAt（服务端 = max(createdAt, 最后一次提问)）是数字（epoch ms）。
		// 字段缺失时退 createdAt；两者都没有就留 0（模型侧当"未知"处理，不显示相对时间）。
		qint64 updatedAtMs = static_cast<qint64>(session.value(QStringLiteral("updatedAt")).toDouble(0));
		if (updatedAtMs <= 0)
			updatedAtMs = static_cast<qint64>(session.value(QStringLiteral("createdAt")).toDouble(0));
		record.updatedAtMs = updatedAtMs;

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

// 整体替换置顶集合。空 id 与重复 id 丢掉；顺序保留（服务端是"最近置顶的在前"）。
// 这里只在侧栏分组/排序时用，所以不需要回头改 SessionRecord（record 里不放 pinned 标志，
// 避免同一份状态存两处、baseline 与 session/list 先后到达时对不上）。
void SessionCatalog::setPinnedSessionIds(const QStringList& ids)
{
	m_pinnedSessionIds.clear();
	m_pinnedSessionIds.reserve(ids.size());

	QSet<QString> seen;
	for (const QString& id : ids) {
		if (id.isEmpty() || seen.contains(id))
			continue;
		seen.insert(id);
		m_pinnedSessionIds.append(id);
	}
}

QStringList SessionCatalog::pinnedSessionIds() const
{
	return m_pinnedSessionIds;
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
QString SessionCatalog::cwdForSession(const QString& sessionId) const
{
	if (const SessionRecord* record = find(sessionId))
		return record->cwd;
	if (const SessionRecord* child = findChild(sessionId))
		return child->cwd;
	return QString();
}

QString SessionCatalog::workspaceFor(const QString& sessionId) const
{
	const SessionRecord* record = find(sessionId);
	return workspaceFor(sessionId, record ? record->cwd : QString());
}

QString SessionCatalog::workspaceFor(const QString& sessionId, const QString& cwd) const
{
	if (const SessionRecord* record = find(sessionId)) {
		if (!record->workspaceId.isEmpty())
			return record->workspaceId;
	}

	for (const WorkspaceRecord& workspace : m_workspaces) {  // 其次看工作区记录（workspace/follow 的 baseline）给出的 sessionIds 归属
		if (workspace.sessionIds.contains(sessionId))
			return workspace.workspaceId;
	}

	// 引擎的 sessionIds 是"有序归属账"，但 dsh-workspace 的唯一写路径 `mutate()` 每写一次都会
	// sessionIds.filter(id => host.sessionPath(id) === record.path)
	// 把"这一刻内存表里解析不出 cwd"的会话从账上剪掉（lib/index.js 里那句 "pruning candidates
	// that no longer pass the id-plus-canonical-cwd membership check"）；而能按 cwd 重建归属的
	// bootstrap() 只在 initialized === false 时跑一次。于是旧会话一旦被剪就永久除名，重启也不自愈。
	if (!cwd.isEmpty()) {
		for (const WorkspaceRecord& workspace : m_workspaces) {
			if (samePath(workspace.path, cwd))
				return workspace.workspaceId;
		}
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
	// 启动默认进入"最近有活动"的会话：session/list 的 updatedAt 是服务端按
	// max(createdAt, 最后一次提问) 现算的，取未归档里最大的那条（运行中也算——
	// 用户往往就是想回到那个正在跑的会话）。旧服务端没有 updatedAt 时退回原规则：
	// 第一条非运行中的会话。
	const SessionRecord* newest = nullptr;
	for (const SessionRecord& record : m_sessions) {
		if (record.archived || record.updatedAtMs <= 0)
			continue;
		if (newest == nullptr || record.updatedAtMs > newest->updatedAtMs)
			newest = &record;
	}
	if (newest != nullptr)
		return newest->sessionId;

	for (const SessionRecord& record : m_sessions) {
		if (!record.archived && !record.running)
			return record.sessionId;
	}
	return QString();
}

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