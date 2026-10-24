// 见 core/ClientRuntime.h：服务端 + 出站客户端 + 会话目录的生命周期与 RPC 业务。

#include "core/ClientRuntime.h"

#include "common/session/SessionCatalog.h"
#include "network/SessionCommands.h"
#include "common/session/SessionService.h"
#include "common/util/Logger.h"
#include "common/util/CommonRegistry.h"
#include "common/util/HostIndex.h"
#include "core/ServerManager.h"
#include "network/DshApiClient.h"
#include "ExtensionSystem/ExtensionToolHost.h"

#include <QCoreApplication>
#include <QDir>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QProcess>
#include <QStringList>
#include <QTimer>
#include <QUrl>

#include <memory>

namespace
{
	// workspace/follow 的 archivedSessionIds / pinnedSessionIds 都是 JSON 字符串数组：统一转成 id 列表，
	// 顺手丢掉空串（catalog 的 setter 还会再校验/去重，这里只做形状转换，不重复业务口径）。
	QStringList idListFromJsonArray(const QJsonArray& values)
	{
		QStringList ids;
		ids.reserve(values.size());
		for (const QJsonValue& value : values) {
			const QString id = value.toString();
			if (!id.isEmpty())
				ids.append(id);
		}
		return ids;
	}

	QSet<QString> idSetFromJsonArray(const QJsonArray& values)   // 同上，给归档集合用（QSet 无序，去重交给 set）
	{
		QSet<QString> ids;
		for (const QString& id : idListFromJsonArray(values))
			ids.insert(id);
		return ids;
	}
}

ClientRuntime::ClientRuntime(QObject* parent)
	: QObject(parent)
{
}

void ClientRuntime::start()
{
	m_server = new ServerManager(this);
	m_api = new DshApiClient(this);

	// findObject(kApiClient) 取出站客户端；此前这个 index 从没登记过 ⇒ 扩展只拿到 null 并静默退化成"没接管"。
	CommonRegistry::instance().AddToRegistry(DshHostIndex::kApiClient, m_api);

	// DshApiClient 既有信号一一对应（见 DshApiClient.h）—— 扩展灌进来的数据因此走与真服务端完全相同的路。
	CommonRegistry::instance().AddToRegistry(DshHostIndex::kMainWindow, m_api);

	// 退出时关闭 API 流：服务端据此释放会话写句柄。缺了这一步，下一次启动用同一会话发消息
	// 会被 SessionAlreadyOwnedError 拒掉（共享退出钩子只做 ClientExtension::shutdownForExit）。
	connect(qApp, &QCoreApplication::aboutToQuit, this, [this]() {
		m_exiting = true;   // 先挂标记：下面杀进程会同步触发 finished，别让它被当成"启动失败"
		if (m_toolHost)   // 扩展工具管道先停：服务端一死，管道对端就没了；在途调用的回包也没人要了
			m_toolHost->stop();
		if (m_api)
			m_api->closeStreams();

		// 紧接着必须把内置引擎带走，否则 node 变孤儿：它继续占着会话的写句柄，下次启动
		// 终端 / 命令全报 session/writer-held（本机日志里刷了一整天）。顺序不能反 ——
		// 先让服务端收到"流关了"，再结束它的进程（stopForExit: terminate → 杀进程树 → waitForFinished）。
		// ⚠️ 别指望 ServerManager 的析构：它是静态单例链上的一环，要等 QApplication 拆完才轮到，
		// 那时太晚；崩溃/强杀更是走不到。所以这里显式调，与 job 的那层保险叠加。
		if (m_server)
			m_server->stopForExit();
		});

	// 服务端起来 -> 把 baseUrl 交给 API 客户端并开流
	connect(m_server, &ServerManager::baseUrlReady, this, [this](const QUrl& url) {
		TimingLogger::mark(QStringLiteral("qml shell: server baseUrl ready"));
		setStatus(qtTrId("session_loading"));
		setServerState(QStringLiteral("running"));
		m_api->setBaseUrl(url);
		m_api->openStreams();
		});

	connect(m_server, &ServerManager::errorLine, this, [this](const QString& line) {
		setStatus(line);
		});

	connect(m_server, &ServerManager::finished, this, [this](int exitCode, QProcess::ExitStatus) {
		if (m_exiting)   // 退出路径自己杀的，不是故障：报"启动失败"只会给下次排查添噪声
			return;
		if (m_server && m_server->isRestarting())
			return;
		if (m_api && !m_api->isConnected()) {
			setServerState(QStringLiteral("failed"));  // 服务端起不来：如实报状态，别让界面一直转圈
			qWarning().noquote() << "[ClientRuntime] 内置服务端启动失败 exitCode=" << exitCode;   // 退出码只进日志，界面上用户看不懂
			setStatus(qtTrId("server_start_failed"));
			setInitializing(false);
		}
		});

	connect(m_api, &DshApiClient::connected, this, [this]() {
		setServerState(QStringLiteral("connected"));
		setInitializing(false);
		refreshSessions();
	});

	connect(m_api, &DshApiClient::transportError, this,
		[this](const QString& context, const QString& message) {
			setStatus(QStringLiteral("%1：%2").arg(context, message));
		});

	// 会话投影：子代理目录（subagentCatalog）由引擎折叠后随三条路推来 ——
	// session/control 的 baseline（活会话全量）+ projection 增量（实时），以及 session/follow 快照
	// （冷会话兜底）。三条都只喂 SessionCatalog，变了就发 peerCatalogChanged 让顶栏页签重画。
	// 别的投影键归 ChatController（统计行）等各自消费者，这里不碰。
	connect(m_api, &DshApiClient::sessionProjectionsBaselineReady, this,
		[this](const QJsonObject& projectionsBySession) {
			if (m_catalog.applyProjectionBaseline(projectionsBySession))
				emit peerCatalogChanged();
		});
	connect(m_api, &DshApiClient::sessionProjectionChanged, this,
		[this](const QString& sessionId, const QString& key, const QJsonValue& value, int) {
			if (m_catalog.applyProjectionChange(sessionId, key, value))
				emit peerCatalogChanged();
		});
	connect(m_api, &DshApiClient::sessionProjectionsReady, this,
		[this](const QString& sessionId, int, const QJsonObject& values) {
			if (m_catalog.applyFollowProjections(sessionId, values))
				emit peerCatalogChanged();
		});

	// 工作区基线（workspace/follow 首帧）：分组、归档集合与置顶集合都在这里，会话列表本身不返回工作区。
	// pinnedSessionIds 是**有序**的（最近置顶的在前），侧栏"置顶"段直接按这个顺序排 ⇒ 用 QStringList 转一道。
	connect(m_api, &DshApiClient::workspaceSnapshotReady, this,
		[this](const QJsonArray& items, const QJsonArray& archivedSessionIds, const QJsonArray& pinnedSessionIds) {
			m_catalog.setArchivedSessionIds(idSetFromJsonArray(archivedSessionIds));
			m_catalog.setPinnedSessionIds(idListFromJsonArray(pinnedSessionIds));
			m_catalog.setWorkspaces(items);
			emit catalogChanged();
		});

	// workspace/follow 的 archived 增量帧：**整体替换**归档集合。
	// 少了这条，任何"归档/删除"之后侧栏都不会跟着变。
	connect(m_api, &DshApiClient::workspaceArchiveChanged, this,
		[this](const QJsonArray& archivedSessionIds) {
			m_catalog.setArchivedSessionIds(idSetFromJsonArray(archivedSessionIds));
			emit catalogChanged();
		});

	// workspace/follow 的 pinned 增量帧：同样**整体替换**（服务端每次给完整集合，最近置顶的在前）。
	// 置顶只影响侧栏排序/分组（会话可见性不变），但"置顶"段的出现/顺序/消失都得靠它刷新。
	connect(m_api, &DshApiClient::workspacePinnedChanged, this,
		[this](const QJsonArray& pinnedSessionIds) {
			m_catalog.setPinnedSessionIds(idListFromJsonArray(pinnedSessionIds));
			emit catalogChanged();
		});

	// workspace/follow 的分组增量帧（upserted / removed / reordered）。**这三条此前一条都没接**：
	// 缺 reordered ⇒ 拖拽后的顺序不生效。三条共用一份 catalog 基线，改完就通知侧栏重建。
	connect(m_api, &DshApiClient::workspaceUpserted, this, [this](const QJsonObject& workspace) {
		m_catalog.upsertWorkspace(workspace);
		emit catalogChanged();
	});
	connect(m_api, &DshApiClient::workspaceRemoved, this, [this](const QString& workspaceId) {
		m_catalog.removeWorkspace(workspaceId);
		emit catalogChanged();
	});
	connect(m_api, &DshApiClient::workspaceReordered, this, [this](const QStringList& workspaceIds) {
		m_catalog.reorderWorkspaces(workspaceIds);
		emit catalogChanged();
	});

	setStatus(qtTrId("server_starting"));
	setServerState(QStringLiteral("starting"));
	m_server->start();

	// 扩展工具宿主：服务端插件（AttachedPlugin）注册工具、DLL 在本进程执行，中间是
	// \\.\pipe\ulinhub-bridge。必须在 m_server->start() 之后 —— dshHome 在那里面才赋值；
	// 而 profile 目录（extensions/ 与 node_modules/）就在它下面的 profiles/web。
	m_toolHost = new ExtensionToolHost(this);
	const QString profileDir = m_server->dshHome() + QStringLiteral("/profiles/web");
	if (!m_toolHost->start(profileDir)) {
		qWarning().noquote() << "[Shell] extension tool host failed to start:"   // 桥起不来只影响"扩展 DLL 工具"这一条线，别把整个壳拖死：记日志、状态行不动
			<< m_toolHost->errorString();
	}
}

void ClientRuntime::restartServer()
{
	if (m_server)
		m_server->restart();
}

void ClientRuntime::refollow(const QString& sessionId)
{
	followSession(sessionId);   // 与 followSession 同一条解析：重连后子代理仍然要按 subagent address 重开（别退回 session 形状）
}

bool ClientRuntime::setStatus(const QString& text)
{
	if (m_status == text)
		return false;
	m_status = text;
	emit statusChanged(text);
	return true;
}

bool ClientRuntime::setServerState(const QString& state)
{
	if (m_serverState == state)
		return false;
	m_serverState = state;
	emit serverStateChanged(state);
	return true;
}

bool ClientRuntime::setInitializing(bool value)
{
	if (m_initializing == value)
		return false;
	m_initializing = value;
	emit initializingChanged(value);
	return true;
}

void ClientRuntime::refreshSessions()
{
	if (!m_api)
		return;

	SessionService::refreshSessions(m_api, &m_catalog,
		[this](const QString& autoSelectSessionId) {
			emit sessionsRefreshed(autoSelectSessionId);
		},
		[this](const DshApiClient::RpcError& error) {
			const QString text = qtTrId("session_list_load_failed_fmt").arg(error.message);
			if (setStatus(text))
				emit operationFailed(text);
		});
}

void ClientRuntime::refreshSessionStates()
{
	if (!m_api)
		return;

	// 只刷 running：session.list 的每一行都是引擎现算的（agents.get(id).status === "running"），
	// 而 subagentCatalog 投影里没有运行态 ⇒ 页签上那颗点只能靠这条路变准。
	SessionService::refreshRunningStates(m_api, &m_catalog, [this](bool changed) {
		if (changed)
			emit peerCatalogChanged();
	});
}

void ClientRuntime::createSession(const QString& workspaceId)
{
	// 侧栏"未分组"表头传下来的是 **界面 key**（SessionListModel::ungroupedWorkspaceKey），不是服务端的
	// 工作区 id：原样发给 session/create，服务端 workspaceRegistry.get() 查不到就回 workspace/not-found
	// （"workspace \"#ungrouped\" not found"）→ 点＋永远建不出会话。未分组 == 没有工作区：按空 id 发。
	// ⚠️ 这个 key 与 SessionListModel::ungroupedWorkspaceKey() 必须一致 —— 两侧各留一个字面量是因为
	// core 不能反向依赖 qmlbridge；改一处就要改另一处。
	const QString serverWorkspaceId = (workspaceId == QStringLiteral("#ungrouped")) ? QString() : workspaceId;

	qInfo().noquote() << "[Shell] createSession requested, workspaceId=" << workspaceId   // 用户动作值得留痕：点没点进来、参数是什么、翻译成了什么，一眼可辨（排查"按钮没反应"时很关键）
		<< "-> server workspaceId="
		<< (serverWorkspaceId.isEmpty() ? QStringLiteral("(none)") : serverWorkspaceId);

	if (!m_api)
		return;

	if (serverWorkspaceId.isEmpty()) {
		// "未分组"是界面上的一个**伪分组**，服务端并不认识它。直接按空 id 发 session/create 时服务端
		// （dsh-api-session-controller 的 create：只有 `request.workspaceId !== void 0` 才查注册表并
		// attachSession）会把会话建出来却不挂进任何工作区 ⇒ 侧栏只能回落到未分组；而且注册表一旦是空的
		// 就会一直空下去，不是"这一次没分组"而是永久没分组。
		resolveSessionWorkspace([this](const QString& resolvedId) {
			createSessionIn(resolvedId);
			});
		return;
	}

	createSessionIn(serverWorkspaceId);
}

void ClientRuntime::createSessionIn(const QString& workspaceId)
{
	SessionService::createSession(m_api, workspaceId,
		[this, workspaceId](const QString& sessionId) {
			// ⚠️ 归属必须带上：addSession 的 workspaceId 为空时侧栏就把这条挂到"未分组"下面
			// （见 SessionListModel::rebuild 的分桶），而服务端其实已经把它挂到 workspaceId 了。
			m_catalog.addSession(sessionId, qtTrId("session_untitled"), workspaceId);
			emit catalogChanged();
			emit sessionsRefreshed(sessionId);   // 建完直接选中它（宿主按这个建议切会话）
		},
		[this](const DshApiClient::RpcError& error) {
			const QString text = qtTrId("session_new_failed_fmt").arg(error.code, error.message);
			if (setStatus(text))
				emit operationFailed(text);

			// 只有"服务端没连上"的遮罩会画 statusText，初始化完成后它看不见 —— 失败必须走 Toast，
			// 否则用户看到的就是"点了没反应"（这次就是被这个静默坑了很久）。
			qWarning().noquote() << "[Shell] createSession failed:" << error.code << error.message;
			emit sessionCreateFailed(text);
		});
}

void ClientRuntime::resolveSessionWorkspace(const std::function<void(const QString&)>& onResolved)
{
	// ① 基线里已经有分组（workspace/follow 的快照或增量帧给的）：新会话就跟第一个分组走。
	const QString existing = m_catalog.firstWorkspaceId();
	if (!existing.isEmpty()) {
		onResolved(existing);
		return;
	}

	// ② 一个分组都没有：按内置服务端的 cwd 补建默认工作区。路径必须与服务端的 defaultCwd 同源
	// （ServerManager::defaultWorkspacePath 就是内置进程的 cwd），否则补出来的分组和"不带
	// workspaceId 时服务端用的 cwd"会指向两个目录。
	if (!m_server) {
		onResolved(QString());
		return;
	}

	const QString path = m_server->defaultWorkspacePath();
	m_api->callMethod(QStringLiteral("workspace/create"), SessionCommands::workspaceCreate(path),
		[this, onResolved, path](const QJsonObject& value) {
			// 回包形状 { workspace: { workspaceId, title, sessionIds }, created }
			const QString created = value.value(QStringLiteral("workspace")).toObject()
				.value(QStringLiteral("workspaceId")).toString();
			if (created.isEmpty()) {
				qWarning().noquote() << "[Shell] workspace/create 回包没有 workspaceId，新会话将落到未分组";
				onResolved(QString());
				return;
			}

			// 本地先补一条分组再往下走：workspace/upserted 是异步推送，等它到就晚了 ——
			// 分组不在 catalog 里时 addSession 找不到归属，照样是未分组。
			QJsonObject workspace;
			workspace.insert(QStringLiteral("workspaceId"), created);
			workspace.insert(QStringLiteral("title"), QDir(path).dirName());
			workspace.insert(QStringLiteral("sessionIds"), QJsonArray());
			m_catalog.upsertWorkspace(workspace);
			emit catalogChanged();

			qInfo().noquote() << "[Shell] 默认工作区就绪:" << created << "<-" << path;
			onResolved(created);
		},
		[onResolved](const DshApiClient::RpcError& error) {
			qWarning().noquote() << "[Shell] 默认工作区补建失败，新会话将落到未分组:"   // 补建失败也要建会话：宁可落到未分组，也不能让"点新建"没反应
				<< error.code << error.message;
			onResolved(QString());
		});
}

// 清空会话 = 那颗「清空会话」按钮的全部语义（ULinHub::onClearConversationClicked）：
// ① 抹掉磁盘上的会话数据（sessions 目录 + storages/workspace.json）；
// ② 本地目录当场清空（会话 + 分组），侧栏立刻空掉；
// ③ 服务端注册表逐个 workspace/delete 删干净，删完再建一个新的默认会话。
// ③ 的顺序不能反：反过来的话 workspace/create 可能被随后到达的 delete 抹掉，新会话就挂在一个已删除的分组上了。
void ClientRuntime::clearAllSessions()
{
	qInfo().noquote() << "[Shell] clearAllSessions requested";

	QStringList workspaceIds;   // 先把要处理的东西都取出来 —— 下面会清本地目录，之后就取不到了
	for (const WorkspaceRecord& workspace : m_catalog.workspaces()) {
		if (!workspace.workspaceId.isEmpty())
			workspaceIds.append(workspace.workspaceId);
	}
	QStringList sessionIds;
	for (const SessionRecord& record : m_catalog.visibleSessions())
		sessionIds.append(record.sessionId);

	// ① **先归档所有会话**（顺序是关键）：
	// 服务端 workspace/delete 只删工作区注册、不动会话；而会话注册表活在**服务端进程内存**里
	// （下面 clearAllSessionData 只是删磁盘文件，服务端当次并不知道）。于是不归档的话：
	// 删完工作区 → 这些会话的 workspaceId 被清空 → 下一次 session/list 又原样把它们拉回来，
	// 全堆进"未分组"（用户看到的是"清空后刷新，一堆会话跑到未分组建"）。
	archiveSessionsThen(sessionIds, [this, workspaceIds]() {
		// ② 磁盘：会话文件 + 工作区注册表（不删的话下次启动会原样带回来）
		if (m_server)
			SessionService::clearAllSessionData(m_server->dshHome());

		// ③ 本地目录：侧栏先空，也保证随后取不到旧分组
		// ⚠️ 保留归档集合：服务端那边这些会话仍是归档态，这里清掉的话，随后 session/list
		// 会把它们当"未归档"全拉回来；而 workspace/delete 已经把它们 detach 了
		// ⇒ 全部堆进"未分组"（用户看到的"清空后一堆未分组会话"就是这个）。
		m_catalog.clear(true);
		emit catalogChanged();

		// "当前会话"已经不存在了：宿主先清掉消息区与选中态。少了这一条，随后的自动选中会被旧选中项挡掉
		// （宿主那边是 `keep.isEmpty()` 才自动选）。
		emit sessionsCleared();

		// ④ 服务端注册表活在服务端进程内存里，删 workspace.json 当次无效（之后任何变更都会把它整体写回来），
		// 所以必须逐个删。删完（或超时兜底）再建一个新会话：catalog 已空，它会走"补建默认分组"那条路。
		deleteWorkspacesThen(workspaceIds, [this]() { createSession(QString()); });
	}, false);   // 不逐条通知宿主：清空自己会发 sessionsCleared
}

// 注册表活在服务端内存里，回包收不齐时按超时兜底 —— 不能让"清空"把后面建新会话吊死。
void ClientRuntime::deleteWorkspacesThen(const QStringList& workspaceIds, const std::function<void()>& onDone)
{
	if (workspaceIds.isEmpty() || !m_api) {
		onDone();
		return;
	}

	auto called = std::make_shared<bool>(false);
	auto finish = [called, onDone]() {
		if (*called)
			return;
		*called = true;
		onDone();
		};
	auto remaining = std::make_shared<int>(static_cast<int>(workspaceIds.size()));
	for (const QString& workspaceId : workspaceIds) {
		m_api->callMethod(QStringLiteral("workspace/delete"), SessionCommands::workspaceDelete(workspaceId),
			[workspaceId, remaining, finish](const QJsonObject&) {
				qInfo().noquote() << QStringLiteral("[Shell] 工作区已删除: %1").arg(workspaceId);
				if (--(*remaining) == 0)
					finish();
			},
			[workspaceId, remaining, finish](const DshApiClient::RpcError& error) {
				qWarning().noquote() << QStringLiteral("[Shell] 工作区删除失败，新会话不会再挂到它: %1")   // 删失败也继续：宁可留一个空分组，也不能让"清空"卡死在这里
					.arg(workspaceId) << error.code << error.message;
				if (--(*remaining) == 0)
					finish();
			});
	}

	QTimer::singleShot(3000, this, finish);
}

void ClientRuntime::archiveSession(const QString& sessionId)
{
	if (sessionId.isEmpty() || !m_api)
		return;

	// 走同一条路：workspace/archiveSession（args { request: { sessionId } }）
	m_api->callMethod(QStringLiteral("workspace/archiveSession"), SessionCommands::sessionArchive(sessionId),
		[this, sessionId](const QJsonObject& value) {
			// ⚠️ 关键：回包里是**完整的归档集合**，必须立刻写进 SessionCatalog —— 侧栏就是靠 catalog 的
			// archivedSessionIds 把这条滤掉的；只刷新列表而不更新归档集合
			// = 会话看上去"删不掉"。
			m_catalog.setArchivedSessionIds(SessionCatalog::parseArchivedSessionIds(value));
			emit catalogChanged();
			emit sessionArchived(sessionId);   // 宿主判"删的是不是当前会话"
		},
		[this](const DshApiClient::RpcError& error) {
			const QString text = qtTrId("session_delete_failed_fmt").arg(error.code, error.message);
			if (setStatus(text))
				emit operationFailed(text);
		});
}

void ClientRuntime::deleteWorkspace(const QString& workspaceId)
{
	if (workspaceId.isEmpty() || !m_api)
		return;

	// 侧栏的"未分组"是界面伪分组（key "#ungrouped"），服务端没有对应实体：
	// 它的"删除"应该等于"把这一档里的会话逐条归档"。
	const bool ungrouped = (workspaceId == QStringLiteral("#ungrouped"));
	const QString groupKey = ungrouped ? QString() : workspaceId;

	QStringList sessionIds;
	for (const SessionRecord& record : m_catalog.visibleSessions()) {
		if (record.workspaceId == groupKey)
			sessionIds.append(record.sessionId);
	}

	qInfo().noquote() << "[Shell] deleteWorkspace:" << workspaceId
		<< (ungrouped ? "(未分组：只归档组内会话)" : QString())
		<< "组内会话数=" << sessionIds.size();

	// ⚠️ 服务端 workspace/delete 的语义是"只删工作区注册，不删目录、也不删会话"
	// （dsh 的注释原话：Delete one Workspace registration without deleting its directory or Sessions）。
	// 于是组内会话会变成无归属，刷新后全部落到"未分组"那一档 —— 这就是"删了工作区，
	// 会话全跑进未分组"的来源。正确顺序：先把组内会话逐条归档，再删分组。
	archiveSessionsThen(sessionIds, [this, ungrouped, workspaceId]() {
		if (ungrouped) {
			refreshSessions();   // 伪分组：没有服务端实体可删
			return;
		}
		m_api->callMethod(QStringLiteral("workspace/delete"), SessionCommands::workspaceDelete(workspaceId),
			[this](const QJsonObject&) {
				refreshSessions();
			},
			[this](const DshApiClient::RpcError& error) {
				const QString text = qtTrId("workdir_delete_failed_fmt").arg(error.code, error.message);
				if (setStatus(text))
					emit operationFailed(text);
			});
	});
}

// 删除工作区前把组内会话逐条归档：服务端 workspace/delete 不会顺带删会话，
// 不归档的话它们会变成"无归属"，在侧栏里全堆进删不掉的"未分组"。
void ClientRuntime::archiveSessionsThen(const QStringList& sessionIds, const std::function<void()>& onDone,
	bool notifyHost)
{
	if (sessionIds.isEmpty() || !m_api) {
		onDone();
		return;
	}

	auto called = std::make_shared<bool>(false);
	auto finish = [called, onDone]() {
		if (*called)
			return;
		*called = true;
		onDone();
		};
	auto remaining = std::make_shared<int>(static_cast<int>(sessionIds.size()));
	for (const QString& sessionId : sessionIds) {
		m_api->callMethod(QStringLiteral("workspace/archiveSession"), SessionCommands::sessionArchive(sessionId),
			[this, sessionId, remaining, finish, notifyHost](const QJsonObject& value) {
				m_catalog.setArchivedSessionIds(SessionCatalog::parseArchivedSessionIds(value));   // 回包是**完整的归档集合**，直接写进 catalog —— 侧栏就是靠它把会话滤掉的
				emit catalogChanged();

				// 和"删单个会话"同一条链路：宿主据此判断删掉的是不是当前会话、要不要切走
				// （"清空全部"不逐条发：那边紧接着有 sessionsCleared 统一处理）
				if (notifyHost)
					emit sessionArchived(sessionId);
				qInfo().noquote() << "[Shell] 会话已归档:" << sessionId;
				if (--(*remaining) == 0)
					finish();
			},
			[sessionId, remaining, finish](const DshApiClient::RpcError& error) {
				qWarning().noquote() << "[Shell] 会话归档失败:" << sessionId << error.code << error.message;   // 归档失败也继续：宁可留一条会话，也不能让删除流程卡死
				if (--(*remaining) == 0)
					finish();
			});
	}

	QTimer::singleShot(3000, this, finish);
}

void ClientRuntime::createWorkspace(const QString& path)
{
	if (!m_api || path.isEmpty())
		return;

	// 走同一条路：workspace/create（args 是 { request: { path } }）；目录由调用方先选好
	m_api->callMethod(QStringLiteral("workspace/create"), SessionCommands::workspaceCreate(path),
		[this, path](const QJsonObject& value) {
			// 回包形状 { workspace: { workspaceId, title, sessionIds, ... }, created }
			const QJsonObject workspace = value.value(QStringLiteral("workspace")).toObject();
			const QString created = workspace.value(QStringLiteral("workspaceId")).toString();

			// ⚠️ 服务端（dsh-api-workspace-controller 的 create）：**目录已经注册过就直接返回原记录、
			// created=false**，不新建。只看 created 才能区分"新建了工作区"和"选的目录早就在列表里"。
			const bool isNew = value.value(QStringLiteral("created")).toBool(false);
			const QString title = workspace.value(QStringLiteral("title")).toString();

			qInfo().noquote() << "[Shell] workspace/create 回包: path=" << path
				<< "id=" << created << "created=" << isNew << "title=" << title;

			if (created.isEmpty()) {
				qWarning().noquote() << "[Shell] workspace/create 回包没有 workspaceId，侧栏不会出现新分组";
				refreshSessions();
				return;
			}

			// 本地先补一条分组：侧栏的分组来自 catalog，而 workspace/follow 的 upsert 帧是异步的
			// 那条路径同一做法，也正是 SessionCatalog::upsertWorkspace 注释里要求的。
			QJsonObject local = workspace;
			if (local.value(QStringLiteral("title")).toString().isEmpty())
				local.insert(QStringLiteral("title"), QDir(path).dirName());
			if (!local.contains(QStringLiteral("sessionIds")))
				local.insert(QStringLiteral("sessionIds"), QJsonArray());
			m_catalog.upsertWorkspace(local);
			emit catalogChanged();

			if (!isNew) {
				// 目录已经是一个工作区了：明确告诉用户，别让"没变化"看起来像失败。
				// 这里**不看 setStatus 的返回值**：连续两次选同一个目录时状态行文案没变，
				// 但用户依然需要每次都看到提示（Toast 自己会重置）。
				const QString text = qtTrId("workdir_exists_fmt")
					.arg(title.isEmpty() ? QDir(path).dirName() : title);
				setStatus(text);
				emit operationFailed(text);
			} else {
				// 一个 0 会话的分组要么不显示、要么只是个空壳 —— 都不像"新建成功"。
				// 建完会走 createSessionIn 的 addSession + catalogChanged + sessionsRefreshed，
				// 于是这个分组立刻出现、并且直接选中那个新会话。
				createSessionIn(created);
			}
			refreshSessions();
		},
		[this](const DshApiClient::RpcError& error) {
			const QString text = qtTrId("workdir_create_failed_fmt").arg(error.code, error.message);
			if (setStatus(text))
				emit operationFailed(text);
		});
}

void ClientRuntime::followSession(const QString& sessionId)
{
	if (!m_api || sessionId.isEmpty())
		return;

	// 子代理会话的 follow 必须用 subagent address（parent + catalog 里的 mode）。关系全在 catalog：
	// parentForSession 优先取 subagentCatalog 反推，mode 取同一条目录项；普通会话两段都为空。
	const QString parentSessionId = m_catalog.parentForSession(sessionId);
	const QString mode = parentSessionId.isEmpty() ? QString() : m_catalog.subagentModeFor(sessionId);
	m_api->followSession(sessionId, parentSessionId, mode);
}
