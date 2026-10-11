// 见 core/ClientRuntime.h：服务端 + 出站客户端 + 会话目录的生命周期与 RPC 业务。
// 本文件从原 src/qmlbridge/ShellController.cpp 拆出（2026-10-10），业务语义一字不改。

#include "core/ClientRuntime.h"

#include "common/session/SessionCatalog.h"
#include "network/SessionCommands.h"
#include "common/session/SessionService.h"
#include "common/util/Logger.h"
#include "common/util/CommonRegistry.h"
#include "common/util/HostIndex.h"
#include "core/ServerManager.h"
#include "network/DshApiClient.h"

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

ClientRuntime::ClientRuntime(QObject* parent)
	: QObject(parent)
{
}

void ClientRuntime::start()
{
	m_server = new ServerManager(this);
	m_api = new DshApiClient(this);

	// 后端接管那条线要的宿主对象（2026-10-01 补）：CodexBackend 这类扩展在 attachHost() 里第一件事就是
	// findObject(kApiClient) 取出站客户端；此前这个 index 从没登记过 ⇒ 扩展只拿到 null 并静默退化成"没接管"。
	CommonRegistry::instance().AddToRegistry(DshHostIndex::kApiClient, m_api);
	// 入站注入面（2026-10-01 工单 B-1 补）：同一对象也登记为 kMainWindow。VirtualMain 的 13 个 Handle* 注入方法与
	// DshApiClient 既有信号一一对应（见 DshApiClient.h）—— 扩展灌进来的数据因此走与真服务端完全相同的路。
	CommonRegistry::instance().AddToRegistry(DshHostIndex::kMainWindow, m_api);

	// 退出时关闭 API 流：服务端据此释放会话写句柄。缺了这一步，下一次启动用同一会话发消息
	// 会被 SessionAlreadyOwnedError 拒掉（共享退出钩子只做 ClientExtension::shutdownForExit）。
	connect(qApp, &QCoreApplication::aboutToQuit, this, [this]() {
		if (m_api)
			m_api->closeStreams();
		});

	// 服务端起来 -> 把 baseUrl 交给 API 客户端并开流（与旧壳 DSHHub::installServer 同一条路）
	connect(m_server, &ServerManager::baseUrlReady, this, [this](const QUrl& url) {
		TimingLogger::mark(QStringLiteral("qml shell: server baseUrl ready"));
		setStatus(QStringLiteral("已连上服务端，正在加载会话…"));
		setServerState(QStringLiteral("running"));
		m_api->setBaseUrl(url);
		m_api->openStreams();
		});

	connect(m_server, &ServerManager::errorLine, this, [this](const QString& line) {
		setStatus(line);
		});

	connect(m_server, &ServerManager::finished, this, [this](int exitCode, QProcess::ExitStatus) {
		if (m_server && m_server->isRestarting())
			return;
		if (m_api && !m_api->isConnected()) {
			setServerState(QStringLiteral("failed"));  // 服务端起不来：如实报状态，别让界面一直转圈
			setStatus(QStringLiteral("内置服务端未能启动（退出码 %1）。发行版需要把 resources/server 部署到 exe 同目录。").arg(exitCode));
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

	// 工作区基线（workspace/follow 首帧）：分组与归档集合都在这里，会话列表本身不返回工作区
	connect(m_api, &DshApiClient::workspaceSnapshotReady, this,
		[this](const QJsonArray& items, const QJsonArray& archivedSessionIds) {
			QSet<QString> archived;
			for (const QJsonValue& value : archivedSessionIds)
				archived.insert(value.toString());

			m_catalog.setArchivedSessionIds(archived);
			m_catalog.setWorkspaces(items);
			emit catalogChanged();
		});

	// workspace/follow 的 archived 增量帧：**整体替换**归档集合（旧壳 handleWorkspaceArchiveChanged 同义）。
	// 少了这条，任何"归档/删除"之后侧栏都不会跟着变。
	connect(m_api, &DshApiClient::workspaceArchiveChanged, this,
		[this](const QJsonArray& archivedSessionIds) {
			QSet<QString> archived;
			for (const QJsonValue& value : archivedSessionIds)
				archived.insert(value.toString());

			m_catalog.setArchivedSessionIds(archived);
			emit catalogChanged();
		});

	// workspace/follow 的分组增量帧（upserted / removed / reordered）。**这三条此前一条都没接**：
	// 缺 upserted ⇒ 点"新建工作区"后新分组要等下次启动才出现；缺 removed ⇒ 删掉的分组在侧栏赖着不走；
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

	setStatus(QStringLiteral("正在启动内置服务端…"));
	setServerState(QStringLiteral("starting"));
	m_server->start();
}

void ClientRuntime::restartServer()
{
	if (m_server)
		m_server->restart();
}

void ClientRuntime::refollow(const QString& sessionId)
{
	if (m_api && !sessionId.isEmpty())
		m_api->followSession(sessionId);
}

// ------------------------------------------------------------------
// 状态机：值去重在这里，宿主只管转成 QML 属性信号
// ------------------------------------------------------------------

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

// 会话 / 工作区 RPC
// ------------------------------------------------------------------

void ClientRuntime::refreshSessions()
{
	if (!m_api)
		return;

	SessionService::refreshSessions(m_api, &m_catalog,
		[this](const QString& autoSelectSessionId) {
			emit sessionsRefreshed(autoSelectSessionId);
		},
		[this](const DshApiClient::RpcError& error) {
			const QString text = QStringLiteral("会话列表加载失败：%1").arg(error.message);
			if (setStatus(text))
				emit operationFailed(text);
		});
}

void ClientRuntime::createSession(const QString& workspaceId)
{
	// 侧栏"未分组"表头传下来的是 **界面 key**（SessionListModel::ungroupedWorkspaceKey），不是服务端的
	// 工作区 id：原样发给 session/create，服务端 workspaceRegistry.get() 查不到就回 workspace/not-found
	// （"workspace \"#ungrouped\" not found"）→ 点＋永远建不出会话。未分组 == 没有工作区：按空 id 发。
	// ⚠️ 这个 key 与 SessionListModel::ungroupedWorkspaceKey() 必须一致 —— 两侧各留一个字面量是因为
	//    core 不能反向依赖 qmlbridge；改一处就要改另一处。
	const QString serverWorkspaceId = (workspaceId == QStringLiteral("#ungrouped")) ? QString() : workspaceId;

	// 用户动作值得留痕：点没点进来、参数是什么、翻译成了什么，一眼可辨（排查"按钮没反应"时很关键）
	qInfo().noquote() << "[Shell] createSession requested, workspaceId=" << workspaceId
		<< "-> server workspaceId="
		<< (serverWorkspaceId.isEmpty() ? QStringLiteral("(none)") : serverWorkspaceId);

	if (!m_api)
		return;

	if (serverWorkspaceId.isEmpty()) {
		// "未分组"是界面上的一个**伪分组**，服务端并不认识它。直接按空 id 发 session/create 时服务端
		// （dsh-api-session-controller 的 create：只有 `request.workspaceId !== void 0` 才查注册表并
		// attachSession）会把会话建出来却不挂进任何工作区 ⇒ 侧栏只能回落到未分组；而且注册表一旦是空的
		// 就会一直空下去，不是"这一次没分组"而是永久没分组。
		// 所以先把"没指定分组"解析成真实分组（已有取第一个 / 一个都没有就补建默认），再建会话。
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
			// TODO(M6)：标题要进 ID-based 翻译（.ts 里有 session_untitled，但 QML 侧还没接翻译通道）。
			// ⚠️ 归属必须带上：addSession 的 workspaceId 为空时侧栏就把这条挂到"未分组"下面
			//    （见 SessionListModel::rebuild 的分桶），而服务端其实已经把它挂到 workspaceId 了。
			m_catalog.addSession(sessionId, QStringLiteral("未命名会话"), workspaceId);
			emit catalogChanged();
			emit sessionsRefreshed(sessionId);   // 建完直接选中它（宿主按这个建议切会话）
		},
		[this](const DshApiClient::RpcError& error) {
			const QString text = QStringLiteral("新建会话失败：%1").arg(error.message);
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
	//    （ServerManager::defaultWorkspacePath 就是内置进程的 cwd），否则补出来的分组和"不带
	//    workspaceId 时服务端用的 cwd"会指向两个目录。
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
			// 补建失败也要建会话：宁可落到未分组，也不能让"点新建"没反应
			qWarning().noquote() << "[Shell] 默认工作区补建失败，新会话将落到未分组:"
				<< error.code << error.message;
			onResolved(QString());
		});
}

// 清空会话 = 旧壳那颗「清空会话」按钮的全部语义（DSHHub::onClearConversationClicked）：
//   ① 抹掉磁盘上的会话数据（sessions 目录 + storages/workspace.json）；
//   ② 本地目录当场清空（会话 + 分组），侧栏立刻空掉；
//   ③ 服务端注册表逐个 workspace/delete 删干净，删完再建一个新的默认会话。
// ③ 的顺序不能反：反过来的话 workspace/create 可能被随后到达的 delete 抹掉，新会话就挂在一个已删除的分组上了。
void ClientRuntime::clearAllSessions()
{
	qInfo().noquote() << "[Shell] clearAllSessions requested";

	QStringList workspaceIds;  // 先把要删的分组 id 取出来 —— 下面第 ② 步会清本地目录，之后就取不到了
	for (const WorkspaceRecord& workspace : m_catalog.workspaces()) {
		if (!workspace.workspaceId.isEmpty())
			workspaceIds.append(workspace.workspaceId);
	}

	// ① 磁盘：不删的话下次启动会把旧会话与旧分组原样带回来（注册表文件见 SessionService 的说明）。
	if (m_server)
		SessionService::clearAllSessionData(m_server->dshHome());

	m_catalog.clear();  // ② 本地目录：侧栏先空，也保证随后取不到旧分组。
	emit catalogChanged();
	// "当前会话"已经不存在了：宿主先清掉消息区与选中态。少了这一条，随后的自动选中会被旧选中项挡掉
	// （宿主那边是 `keep.isEmpty()` 才自动选）。
	emit sessionsCleared();

	// ③ 服务端注册表活在服务端进程内存里，删 workspace.json 当次无效（之后任何变更都会把它整体写回来），
	//    所以必须逐个删。删完（或超时兜底）再建一个新会话：catalog 已空，它会走"补建默认分组"那条路。
	deleteWorkspacesThen(workspaceIds, [this]() { createSession(QString()); });
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
				// 删失败也继续：宁可留一个空分组，也不能让"清空"卡死在这里
				qWarning().noquote() << QStringLiteral("[Shell] 工作区删除失败，新会话不会再挂到它: %1")
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

	// 与旧壳同路：workspace/archiveSession（args { request: { sessionId } }）
	m_api->callMethod(QStringLiteral("workspace/archiveSession"), SessionCommands::sessionArchive(sessionId),
		[this, sessionId](const QJsonObject& value) {
			// ⚠️ 关键：回包里是**完整的归档集合**，必须立刻写进 SessionCatalog —— 侧栏就是靠 catalog 的
			// archivedSessionIds 把这条滤掉的（旧壳同路：DSHHub.cpp:814-818）；只刷新列表而不更新归档集合
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

	m_api->callMethod(QStringLiteral("workspace/delete"), SessionCommands::workspaceDelete(workspaceId),
		[this](const QJsonObject&) {
			refreshSessions();
		},
		[this](const DshApiClient::RpcError& error) {
			// 没有专门的"删除工作区失败" id，先用通用状态行文案（TODO(M6)：等 .ts 补 id）
			const QString text = QStringLiteral("删除工作区失败: %1 %2").arg(error.code, error.message);
			if (setStatus(text))
				emit operationFailed(text);
		});
}

void ClientRuntime::createWorkspace(const QString& path)
{
	if (!m_api || path.isEmpty())
		return;

	// 与旧壳同路：workspace/create（args 是 { request: { path } }）；目录由调用方先选好
	m_api->callMethod(QStringLiteral("workspace/create"), SessionCommands::workspaceCreate(path),
		[this](const QJsonObject&) {
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
	if (m_api && !sessionId.isEmpty())
		m_api->followSession(sessionId);
}
