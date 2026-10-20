#pragma once

// 侧边栏"工作区 / 会话"列表的纯逻辑模型（无 Qt Widget 依赖）：解析 session.list 回包，维护
// sessionId 的归属/标题/归档，并派生分组与自动选中结果。工作区不随 session.list 返回，只由
// workspace/follow 的 baseline 提供。
//
// 2026-10-19：子代理（subagent）数据也归这里（与"会话目录"同一份语义，避免再开一份新类）：
//   · 子代理会话本身（session.list 里 origin == subagent / 带 parentSessionId 的行）不进侧栏，
//     但顶栏的"主 ↔ 子"页签要拿它们的标题与运行态 ⇒ 单独存一份 m_childSessions；
//   · "某个会话有哪些直接子代理"来自**会话投影 subagentCatalog**（引擎 @deepseek-ai/dsh-subagent
//     注册的 parent-owned 目录投影），由 session/follow 快照、session/control 的 baseline/增量帧喂进来。
//     投影是引擎权威数据，客户端**不自己造**成员关系（与官方 ui-subagent 一致）。
// 侧栏列表的对外契约（visibleSessions / titleFor / autoSelectSessionId）刻意不变 —— 子代理仍不出现在
// 侧栏、titleFor 对子代理仍返回空串（见 TestSessionCatalog 的用例）；页签要用子代理标题时走
// childTitleFor()。

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QHash>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>

// 工作区记录（workspaceId 为空表示"未分组"）
struct WorkspaceRecord
{
	QString workspaceId;
	QString title;
	QStringList sessionIds;
};

struct SessionRecord
{
	QString sessionId;
	QString title;
	QString workspaceId;
	bool running = false;
	bool archived = false;
	// 子代理会话（只进 m_childSessions，不进侧栏）：父会话 id + 是否是子代理
	QString parentSessionId;
	bool subagent = false;

	int asOfSeq = 0;  // projections.asOfSeq（本会话投影位置），follow 不来时当 throughSeq 回落值
	// 下面三个 + hasModelSelection = 会话自己记录的模型选择（next 优先，退回 lastUsed）；会话级"当前模型"只在这里，catalog 给的是部署默认值。
	bool hasModelSelection = false;
	QString modelProvider;
	QString modelId;
	QString reasoningEffort;
};

// session.list 一次回包的结构化结果（工作区不随之返回），避免把 JSON 细节带进 UI。
struct SessionListSnapshot
{
	bool workspacesOk = false;
	QSet<QString> archivedSessionIds;
	QJsonArray workspaces;

	bool sessionsOk = false;
	QJsonArray sessions;

	QString errorCode;
	QString errorMessage;
};

class SessionCatalog
{
public:
	// keepArchived：保留归档集合。**清空全部会话时必须传 true** —— 服务端那边这些会话仍是归档态，
	// 把它一起清掉的话，随后 session/list 拉回来的旧会话会因为"不再被标记为归档"而全部冒出来，
	// 而它们此刻已经没有归属（workspace/delete 会 detach）⇒ 全堆进删不掉的"未分组"。
	void clear(bool keepArchived = false);

	// 按"先工作区、后会话"的顺序应用一次回包
	void applySnapshot(const SessionListSnapshot& snapshot);

	// 只刷新"运行态"（session.list 的 running 字段），不重建列表、不动选中项。
	// 用途：子代理/会话的运行态只有 session.list 会给（引擎在 summaryFor 里按 agents.status 现算），
	// 且**不会**随 session/control 投影推送 ⇒ 页签上那颗"运行中"的点必须靠偶尔的轻量重拉才准。
	// 返回 true = 至少有一行的 running 变了（调用方据此重画顶栏）。
	bool applyRunningStates(const QJsonArray& items);
	void setWorkspaces(const QJsonArray& items);
	// 单条分组的增 / 改 / 删 / 重排：workspace/upserted、workspace/removed、workspace/reordered 增量帧用，
	// 以及 workspace/create 之后本地先行补一条（不等异步推送，否则新会话找不到归属又落到未分组）。
	void upsertWorkspace(const QJsonObject& workspace);
	void removeWorkspace(const QString& workspaceId);
	void reorderWorkspaces(const QStringList& workspaceIds);
	void setSessions(const QJsonArray& items);
	void setArchivedSessionIds(const QSet<QString>& ids);

	// 新增/更新单个会话；返回 false = 被忽略（sessionId 为空或已归档），调用方不应再渲染它。
	bool addSession(const QString& sessionId, const QString& title, const QString& workspaceId = QString());
	// 更新已存在会话的标题；会话不存在时返回 false
	bool updateTitle(const QString& sessionId, const QString& title);

	QString titleFor(const QString& sessionId) const;
	QString workspaceFor(const QString& sessionId) const;

	// 该会话投影到的日志位置（projections.asOfSeq）；没有该会话时返回 0。
	int asOfSeqFor(const QString& sessionId) const;

	// 该会话自己记录的模型选择（modelSelection.next → lastUsed）；返回 false = 服务端还没记录过该会话的选择。
	bool modelSelectionFor(const QString& sessionId, QString* provider, QString* model, QString* reasoningEffort) const;

	const QVector<WorkspaceRecord>& workspaces() const;
	// 基线里的第一个工作区；一个都没有返回空串（新会话该挂哪个分组的回落值）
	QString firstWorkspaceId() const;
	// 需要显示在侧边栏的会话（未归档、非子代理）
	QVector<SessionRecord> visibleSessions() const;

	// 自动选中：第一个"非运行中、非子代理"的会话，无则返回空串
	QString autoSelectSessionId() const;

	// ---- 子代理（顶栏"主 ↔ 子"页签的数据面）----
	// 一个会话的直接子代理目录（投影 subagentCatalog 的 wire view）。没有记录返回空数组。
	QJsonArray subagentCatalog(const QString& sessionId) const;
	// 子代理会话的标题 / 运行态（session.list 里那批行）；没有记录返回空串 / false
	QString childTitleFor(const QString& sessionId) const;
	bool childRunning(const QString& sessionId) const;
	// 某个会话的直接父会话 id：优先用 subagentCatalog 反向索引（引擎权威），退回 session.list 的
	// parentSessionId。没有父会话返回空串。
	QString parentForSession(const QString& sessionId) const;
	// 子代理会话在父目录里的 mode（one-shot / continuable）：session/follow、session/page 的
	// subagent address 要带上它（引擎会与投影里的 subagent.mode 比对）。目录里没有该子代理时返回空串。
	QString subagentModeFor(const QString& sessionId) const;

	// 会话投影的三个入口：返回 true = 子代理目录确实变了（调用方据此通知界面）
	bool applyProjectionBaseline(const QJsonObject& projectionsBySession);
	bool applyProjectionChange(const QString& sessionId, const QString& key, const QJsonValue& value);
	bool applyFollowProjections(const QString& sessionId, const QJsonObject& values);

	static bool isSubagent(const QJsonObject& session);
	// 从 projections.values 取标题（取不到返回空串）
	static QString projectionTitle(const QJsonObject& session);
	// 展示用标题（取不到时回退到"未命名会话"）
	static QString sessionTitle(const QJsonObject& session);
	static QSet<QString> parseArchivedSessionIds(const QJsonObject& workspaceListValue);

private:
	SessionRecord* find(const QString& sessionId);
	const SessionRecord* find(const QString& sessionId) const;
	const SessionRecord* findChild(const QString& sessionId) const;
	// 写入一个会话的子代理目录；返回目录内容是否真的变了（空目录不存，等价于"没有"）
	bool setSubagentCatalog(const QString& sessionId, const QJsonArray& entries);
	// subagentCatalog 变了之后重建 childId → parentSessionId 的反向索引
	void rebuildChildParentIndex();

	QVector<WorkspaceRecord> m_workspaces;
	QVector<SessionRecord> m_sessions;
	QSet<QString> m_archivedSessionIds;

	// ---- 子代理数据（见文件头说明）----
	QVector<SessionRecord> m_childSessions;          // session.list 里的子代理会话（不进侧栏）
	QHash<QString, QJsonArray> m_subagentCatalogs;   // sessionId -> 直接子代理目录（投影值）
	QHash<QString, QString> m_childParentIndex;      // 子代理 id -> 直接父会话 id（由目录反推）
};
