// 见 QmlBridge.h：QML 唯一的门，只做接线（转调 + 属性/信号转发）。
//   服务端/客户端生命周期与会话 RPC  → core/ClientRuntime
//   发送策略与自愈状态机            → common/session/ChatController
//   主题切换编排                    → qmlbridge/ThemeController
// 带逻辑的模型/控制器都在 common/**；留在这里的只有接线，以及"当前会话 id"这类纯界面状态。

#include "qmlbridge/QmlBridge.h"
#include "ExtensionSystem/HostUi.h"

#include "common/session/ChatController.h"
#include "common/session/TraceModel.h"   // trace() 把 TraceModel* 当 QAbstractItemModel* 返回，需要完整类型
#include "common/session/ToolsFilter.h"
#include "common/util/CodeHighlighter.h"
#include "common/util/MarkdownSegmenter.h"
#include "core/ClientRuntime.h"
#include "network/DshApiClient.h"
#include "network/SessionCommands.h"
#include "qmlbridge/TerminalBridge.h"
#include "qmlbridge/ThemeController.h"
#include "qmlbridge/ThemeBridge.h"

#include <QAbstractTextDocumentLayout>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QJsonArray>
#include <QJsonObject>
#include <QQuickTextDocument>
#include <QTextBlock>
#include <QTextBlockFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextDocumentFragment>
#include <QTextFrame>
#include <QTextTable>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <QVariantMap>

#include <functional>

namespace
{
	// 界面上的"未分组"分组（见 SessionListModel::ungroupedWorkspaceKey）
	const char kUngroupedKey[] = "#ungrouped";
}

QmlBridge& QmlBridge::instance()
{
	static QmlBridge bridge;
	return bridge;
}

QmlBridge::QmlBridge(QObject* parent)
	: QObject(parent)
{
}

void QmlBridge::init()
{
	MarkdownSegmenter::ensureHighlightRulesLoaded();  // 高亮规则（qrc）在切分正文之前装载一次

	// ---- 核心层：服务端 + 出站客户端 + 会话目录（业务都在那边）----
	m_runtime = new ClientRuntime(this);
	m_runtime->start();

	// 侧栏列表模型只读 core 的目录（数据不归它所有，见 SessionListModel.h）
	m_sessions = new SessionListModel(m_runtime->catalog(), this);

	DshApiClient* api = m_runtime->apiClient();

	// 设置页（模型 / API key），侧栏 ⚙ 打开；必须在 api 之后（它要拿 api 发 RPC）
	m_settings = new SettingsController(api, this);
	// 插件市场：HTTP 与解析走 PluginMarketClient / PluginMarketModel
	m_plugins = new PluginMarketController(api, this);
	// 扩展管理：工具扩展 / 客户端扩展两种来源在 ExtensionManagerController 里合成
	m_extensions = new ExtensionManagerController(m_runtime->server(), m_runtime->toolHost(), this);
	// 斜杠命令（发现/过滤/执行）：命令不进模型历史，单独一层；发送分支在 sendPrompt()
	m_commands = new CommandController(api, this);
	// /permission 切完权限后重读一次投影；命令本身是异步的，等一个小延迟让服务端先落事件。
	connect(m_commands, &CommandController::executionFinished, this, [this](bool ok, const QString&) {
		if (ok)
			QTimer::singleShot(180, this, &QmlBridge::refreshPermissionPreset);
	});
	// 右栏「终端」块：会话对象由 TerminalBridge 按块 id 管，这里只把唯一的出站客户端交给它
	TerminalBridge::instance().init(api);

	// 用户保存服务器地址后重启内置服务端
	connect(m_settings, &SettingsController::serverRestartRequested, this, [this]() {
		m_runtime->restartServer();
		});
	// 风格包重置后热重载 QML 侧 token：SettingsController 在领域层，不许反向 include qmlbridge，
	// 于是由它发信号、这里落地。
	connect(m_settings, &SettingsController::stylesReset, this, []() {
		ThemeBridge::instance().reload();
		});
	// 插件市场装完市场包也要重启内置服务端（新插件才会被加载）：PluginMarketController 在领域层，
	// 不许反向 include core，于是由它发信号、这里落地。
	connect(m_plugins, &PluginMarketController::serverRestartRequested, this, [this]() {
		m_runtime->restartServer();
		});

	// ---- 业务层：消息区（follow 快照 + mux 事件 -> 消息模型）+ 发送/自愈 ----
	m_chat = new ChatController(api, this);
	// 命令注册表变了（插件运行期注册/注销命令）⇒ 重拉当前会话的命令表
	connect(m_chat, &ChatController::commandRegistryChanged, this, [this]() {
		if (!m_currentSessionId.isEmpty())
			m_commands->refresh(m_currentSessionId);
	});
	// ---- 主题切换编排 ----
	m_theme = new ThemeController(this);
	// ---- 工具过滤器：数据层与形状（含摊平的行模型）都在它自己身上，桥只负责开关面板 ----
	m_toolsFilter = new ToolsFilter(this);
	// 基地址跟着服务端走：每次拿到 baseUrl 就同步给过滤器（它只借 scheme/host/port）
	connect(m_runtime, &ClientRuntime::serverStateChanged, this, [this, api]() {
		m_toolsFilter->setBaseUrl(api->baseUrl());
	});
	m_toolsFilter->setBaseUrl(api->baseUrl());

	wireRuntime();
	wireChat();
}

// 接线：core 的信号 → 本类的属性信号 / 业务层对象。**这里不写业务**。
void QmlBridge::wireRuntime()
{
	// 状态机（值是 core 里的，去重也在那边；这里只转发）
	connect(m_runtime, &ClientRuntime::statusChanged, this, [this](const QString&) { emit statusTextChanged(); });
	connect(m_runtime, &ClientRuntime::serverStateChanged, this, [this](const QString&) { emit serverStateChanged(); });
	connect(m_runtime, &ClientRuntime::initializingChanged, this, [this](bool) { emit initializingChanged(); });

	// 目录变了 → 列表模型整份重建；列表空时给一句引导
	connect(m_runtime, &ClientRuntime::catalogChanged, this, [this]() {
		if (m_sessions)
			m_sessions->rebuild();
		// 工作区标题/归属可能随目录刷新变化：让顶栏与输入框的 currentWorkspaceTitle 重新求值。
		emit currentSessionChanged();
		// 子代理会话的标题/运行态也来自 session.list：列表重建后顶栏页签跟着重画
		refreshPeerRoot();
		emit peerTabsChanged();
	});

	// 子代理目录（会话投影 subagentCatalog）变了：顶栏"主 ↔ 子"页签重画
	connect(m_runtime, &ClientRuntime::peerCatalogChanged, this, [this]() {
		// 父目录可能在"进入子代理之后"才到（冷父会话）：这里把组根从子代理纠正回父会话，
		// 否则页签行会一直把子代理当成组根（= 切回父会话后仍看到子代理那颗）。
		refreshPeerRoot();
		emit peerTabsChanged();
	});

	// 列表拉回来：按服务端建议自动选中（第一个非运行中的会话）
	connect(m_runtime, &ClientRuntime::sessionsRefreshed, this, [this](const QString& autoSelectSessionId) {
		if (!m_sessions)
			return;
		const QString keep = m_sessions->selectedSessionId();
		m_sessions->rebuild();

		if (keep.isEmpty() && !autoSelectSessionId.isEmpty())
			selectSession(autoSelectSessionId);
		else if (!keep.isEmpty())
			m_sessions->setSelectedSession(keep);

		if (m_sessions->rowCount() == 0)
			m_runtime->setStatus(qtTrId("session_none_click_new"));

		refreshPeerRoot();       // 父目录/父子关系可能是这次刷新才到的
		emit peerTabsChanged();   // 标题/运行态可能是这次刷新才变的
	});

	// 会话被归档：删的正是当前会话就清空消息区、回到占位页（目录由 runtime 更新，这里只管界面状态）
	connect(m_runtime, &ClientRuntime::sessionArchived, this, [this](const QString& sessionId) {
		if (sessionId.isEmpty())
			return;

		if (m_currentSessionId == sessionId) {
			// 当前会话被归档：会话与页签组一起作废（只发一轮信号）
			m_currentSessionId.clear();
			m_peerRootId.clear();
			if (m_chat)
				m_chat->setSession(QString());
			emit currentSessionChanged();
			emit peerTabsChanged();
			return;
		}

		// 页签组根被归档（删的是当前所在组的父会话）：页签组作废
		if (m_peerRootId == sessionId) {
			m_peerRootId.clear();
			emit peerTabsChanged();
		}
	});

	// ⚠️ 别把 operationFailed 吞掉：新建/删除工作区失败、目录非法…… 用户端只会表现为"点了没反应"，
	//    要原样转发给 QML 弹 Toast。
	connect(m_runtime, &ClientRuntime::operationFailed, this, &QmlBridge::operationFailed);
	connect(m_runtime, &ClientRuntime::sessionCreateFailed, this, &QmlBridge::sessionCreateFailed);

	// 会话被整份清空：当前会话连同消息区一起作废。
	// ⚠️ 必须把选中态也清掉：sessionsRefreshed 的自动选中只在 `keep.isEmpty()` 时生效，
	//    留着旧选中项就等于新会话建出来了却选不上。
	connect(m_runtime, &ClientRuntime::sessionsCleared, this, [this]() {
		m_currentSessionId.clear();
		m_peerRootId.clear();
		if (m_chat)
			m_chat->setSession(QString());
		if (m_sessions)
			m_sessions->setSelectedSession(QString());
		emit currentSessionChanged();
		emit peerTabsChanged();
	});

	// 服务端重连：如果发送链路攒着一条待补发的（"会话被旧进程占用"自愈），在这里补发
	connect(m_runtime->apiClient(), &DshApiClient::connected, this, [this]() {
		if (!m_chat || !m_runtime)
			return;
		if (m_chat->hasPendingRetry() && !m_currentSessionId.isEmpty()) {
			m_runtime->refollow(m_currentSessionId);
			QTimer::singleShot(400, this, [this]() { m_chat->sendPromptAgain(); });
		}
	});
}

// 接线：业务层的信号 → 本类的属性信号
void QmlBridge::wireChat()
{
	// 事件入口（follow 快照 / mux 帧 / 会话投影）由 ChatController::connectApi() 自己接，
	// 这里只把它的信号转发给 QML —— 桥因此不碰 QJson、不认识事件结构。
	m_chat->connectApi();

	connect(m_chat, &ChatController::messagesChanged, this, &QmlBridge::messageCountChanged);
	connect(m_chat, &ChatController::streamingChanged, this, &QmlBridge::streamingChanged);
	connect(m_chat, &ChatController::cancellingChanged, this, &QmlBridge::cancellingChanged);
	connect(m_chat, &ChatController::turnFinished, this, &QmlBridge::lastTurnEmptyChanged);
	connect(m_chat, &ChatController::statsLineChanged, this, &QmlBridge::statsLineChanged);
	// 轨迹顶部概览（轮次/步骤/工具/token/时长）：模型算好一份 map，桥只转发
	connect(m_chat->trace(), &TraceModel::summaryChanged, this, &QmlBridge::traceSummaryChanged);
	connect(m_chat, &ChatController::interactionChanged, this, &QmlBridge::interactionChanged);
	connect(m_chat, &ChatController::olderChanged, this, &QmlBridge::olderChanged);
	connect(m_chat, &ChatController::olderPrepended, this, &QmlBridge::olderPrepended);

	// 子代理那边有新动静（目录/回传/结束）⇒ 1.2s 去抖后重拉一次 running。
	// 必须重拉：引擎的 running 只在 session/list 里现算（agents.status），投影帧不推 ——
	// 不拉的话子代理跑得再欢，顶栏也一直"未在运行"。
	m_peerStateTimer = new QTimer(this);
	m_peerStateTimer->setSingleShot(true);
	m_peerStateTimer->setInterval(1200);
	connect(m_peerStateTimer, &QTimer::timeout, this, &QmlBridge::refreshSessionStates);
	connect(m_chat, &ChatController::peerActivitySeen, this, [this]() {
		if (m_peerStateTimer && !m_peerStateTimer->isActive())
			m_peerStateTimer->start();
	});
	connect(m_chat, &ChatController::catalogChanged, this, &QmlBridge::modelCatalogChanged);
	// 后台任务名册（引擎 job/list 流 → ChatController → QML）
	connect(m_chat, &ChatController::jobsChanged, this, &QmlBridge::jobsChanged);
	// 扩展把产物（如截图）交给输入框：HostUi 只发信号，落到这里的待发附件列表
	connect(&HostUi::instance(), &HostUi::composerFilesRequested, this, [this](const QStringList& paths) {
		if (!m_chat || paths.isEmpty())
			return;
		QVariantList urls;
		for (const QString& path : paths)
			urls.append(QUrl::fromLocalFile(path).toString());
		m_chat->addImageFiles(urls);
	});
	connect(m_chat, &ChatController::pendingAttachmentsChanged, this, &QmlBridge::pendingAttachmentsChanged);
	connect(m_chat, &ChatController::attachmentSourceReady, this, [this](const QString&) {
		++m_attachmentRevision;
		emit attachmentRevisionChanged();
	});
}

// ------------------------------------------------------------------
// 会话 / 工作区：转调 core（换算是 core 的事，界面 key 只在这里出现）
// ------------------------------------------------------------------

void QmlBridge::refreshSessions()
{
	m_runtime->refreshSessions();
}

// 轻量刷新：只更新运行态（会话列表/顶栏页签那颗"运行中"点）。
// 触发：打开「子智能体」下拉、换会话、子代理活动事件（去抖）。
void QmlBridge::refreshSessionStates()
{
	if (m_runtime)
		m_runtime->refreshSessionStates();
}

void QmlBridge::refreshPermissionPreset()
{
	if (!m_runtime || m_currentSessionId.isEmpty())
		return;

	DshApiClient* api = m_runtime->apiClient();
	if (!api)
		return;

	const QString sessionId = m_currentSessionId;
	api->callMethod(QStringLiteral("session/projections"), SessionCommands::sessionProjections(sessionId),
		[this, sessionId](const QJsonObject& value) {
			if (sessionId != m_currentSessionId)
				return;
			const QString preset = value.value(QStringLiteral("values")).toObject()
				.value(QStringLiteral("permissions")).toObject()
				.value(QStringLiteral("currentValue")).toString();
			if (preset == m_permissionPreset)
				return;
			m_permissionPreset = preset;
			emit permissionPresetChanged();
		},
		[](const DshApiClient::RpcError&) {
			// 权限投影失败不挡输入：按钮保持"权限"占位，下一次切会话再读。
		});
}

void QmlBridge::setPermissionPreset(const QString& preset)
{
	const bool known = preset == QLatin1String("read-only")
		|| preset == QLatin1String("workspace-write")
		|| preset == QLatin1String("danger-full-access");
	if (!known || !m_commands || m_currentSessionId.isEmpty())
		return;

	// 引擎的官方写路径：/permission <preset>。成功后 executionFinished 会触发重读投影。
	m_commands->execute(m_currentSessionId, QStringLiteral("/permission ") + preset);
}

void QmlBridge::createSession(const QString& workspaceId)
{
	m_runtime->createSession(workspaceId.isEmpty() ? QString::fromLatin1(kUngroupedKey) : workspaceId);
}

void QmlBridge::createWorkspace()
{
	// 目录选择是控件层的活（QFileDialog），RPC 交给 runtime
	const QString path = QFileDialog::getExistingDirectory(nullptr,
		qtTrId("sidebar_new_workspace"), QDir::homePath());
	if (path.isEmpty())
		return;
	m_runtime->createWorkspace(path);
}

void QmlBridge::deleteSession(const QString& sessionId)
{
	m_runtime->archiveSession(sessionId);
}

void QmlBridge::deleteWorkspace(const QString& workspaceId)
{
	m_runtime->deleteWorkspace(workspaceId);
}

void QmlBridge::clearAllSessions()
{
	m_runtime->clearAllSessions();
}

// 当前会话所属页签组的根：父目录已知就用父会话，否则当前会话自己。
// 页签行只从这一个根的直接子代理目录生成 —— 任何别的会话的子代理都不该混进来。
void QmlBridge::refreshPeerRoot()
{
	if (m_currentSessionId.isEmpty()) {
		m_peerRootId.clear();
		return;
	}

	const SessionCatalog* catalog = m_sessions ? m_sessions->catalog() : nullptr;
	const QString parentSessionId = catalog ? catalog->parentForSession(m_currentSessionId) : QString();
	m_peerRootId = parentSessionId.isEmpty() ? m_currentSessionId : parentSessionId;
}

void QmlBridge::selectSession(const QString& sessionId)
{
	if (sessionId.isEmpty() || m_currentSessionId == sessionId)
		return;

	m_currentSessionId = sessionId;
	// 组根按**当前会话**重算：目标是子代理就跟着父会话，否则就是自己。
	// 父子关系来自引擎：subagentCatalog 投影优先，session.list 的 parentSessionId 兜底。
	refreshPeerRoot();
	if (DshApiClient* api = m_runtime->apiClient())
		api->setCurrentSessionId(sessionId);   // 扩展经 VirtualMain 读

	// 消息区换会话（清空 + 记住当前会话），再把跟随流切过去：首帧快照会由 ChatController 接收。
	// 子代理的地址形状（parent + mode）由 ClientRuntime 从 catalog 解析，follow/page 都走 subagent address。
	m_chat->setSession(sessionId);
	m_chat->refreshModelCatalog();  // 输入区的模型/思考 chip 按会话重拉
	m_runtime->followSession(sessionId);   // 同时会换掉 job/list 名册流
	// 顺手把运行态刷一次：换到子代理/切回父会话都是"看谁在跑"的时刻，
	// 而 session.list 的 running 是快照，启动后不再自动更新。
	m_runtime->refreshSessionStates();
	// 命令是**按 agent（会话）注册**的：换会话必须重拉，否则会拿上一个会话的命令表。
	// 子代理会话没有 commands/list（引擎回 session/agent-busy）⇒ 清空。
	// 组根 == 当前会话 ⇔ 目标是普通会话，只有这种才重拉。
	if (m_peerRootId == m_currentSessionId)
		m_commands->refresh(sessionId);
	else
		m_commands->clear();

	m_sessions->setSelectedSession(sessionId);
	emit currentSessionChanged();

	// 权限预设是会话级的投影：切会话先清空旧值（按钮回到"权限"占位），再读新会话。
	if (!m_permissionPreset.isEmpty()) {
		m_permissionPreset.clear();
		emit permissionPresetChanged();
	}
	refreshPermissionPreset();

	emit peerTabsChanged();
}

// ------------------------------------------------------------------
// 子代理页签（顶栏"主 ↔ 子"）
// ------------------------------------------------------------------

// 当前页签组根会话的直接子代理（不含主会话自己）：顶栏「子智能体」下拉框的列表来源，
// 页签行也只在里面找"当前子代理"那一行（父会话不画子代理）。
// 目录是引擎 subagentCatalog 投影的原样内容；标题优先用目录里的 label（continuable 必有、
// one-shot 可缺），缺了退回该子会话的标题，再退回 id。
QVariantList QmlBridge::peerTabs() const
{
	QVariantList rows;
	const SessionCatalog* catalog = m_sessions ? m_sessions->catalog() : nullptr;
	if (!catalog || m_peerRootId.isEmpty())
		return rows;

	const QJsonArray entries = catalog->subagentCatalog(m_peerRootId);
	for (const QJsonValue& value : entries) {
		const QJsonObject entry = value.toObject();
		const QString id = entry.value(QStringLiteral("id")).toString();
		if (id.isEmpty())
			continue;

		QString label = entry.value(QStringLiteral("label")).toString();
		if (label.isEmpty())
			label = catalog->childTitleFor(id);
		if (label.isEmpty())
			label = id;

		QVariantMap row;
		row.insert(QStringLiteral("id"), id);
		row.insert(QStringLiteral("label"), label);
		row.insert(QStringLiteral("mode"), entry.value(QStringLiteral("mode")).toString());
		row.insert(QStringLiteral("running"), catalog->childRunning(id));
		rows.append(row);
	}

	// 兜底：父会话的目录还没到（冷父会话、投影没推来）但当前会话确实是它的子代理时，
	// 至少保留"当前子代理"这一行 —— 下拉框里能看见它，页签第二颗也能拿到真实标题。
	if (rows.isEmpty() && m_currentSessionId != m_peerRootId
		&& catalog->parentForSession(m_currentSessionId) == m_peerRootId) {
		QString label = catalog->childTitleFor(m_currentSessionId);
		if (label.isEmpty())
			label = m_currentSessionId;

		QVariantMap row;
		row.insert(QStringLiteral("id"), m_currentSessionId);
		row.insert(QStringLiteral("label"), label);
		row.insert(QStringLiteral("mode"), QString());
		row.insert(QStringLiteral("running"), catalog->childRunning(m_currentSessionId));
		rows.append(row);
	}

	return rows;
}

QString QmlBridge::peerRootTitle() const
{
	if (m_peerRootId.isEmpty())
		return currentSessionTitle();

	const SessionCatalog* catalog = m_sessions ? m_sessions->catalog() : nullptr;
	if (!catalog)
		return m_peerRootId;

	QString title = catalog->titleFor(m_peerRootId);
	if (title.isEmpty())
		title = catalog->childTitleFor(m_peerRootId);
	return title.isEmpty() ? m_peerRootId : title;
}

int QmlBridge::subagentCount() const
{
	return peerTabs().size();
}

bool QmlBridge::subagentRunning() const
{
	const SessionCatalog* catalog = m_sessions ? m_sessions->catalog() : nullptr;
	if (!catalog || m_peerRootId.isEmpty())
		return false;

	const QJsonArray entries = catalog->subagentCatalog(m_peerRootId);
	for (const QJsonValue& value : entries) {
		const QString id = value.toObject().value(QStringLiteral("id")).toString();
		if (!id.isEmpty() && catalog->childRunning(id))
			return true;
	}
	// 兜底行（父目录未到）：当前子代理的运行态也要算进去
	return catalog->childRunning(m_currentSessionId);
}

QVariantList QmlBridge::jobs() const
{
	return m_chat ? m_chat->jobs() : QVariantList();
}

int QmlBridge::jobCount() const
{
	return m_chat ? m_chat->jobCount() : 0;
}

bool QmlBridge::jobRunning() const
{
	return m_chat && m_chat->jobRunning();
}

QString QmlBridge::currentSessionTitle() const
{
	if (m_currentSessionId.isEmpty() || !m_sessions)
		return QString();

	const SessionCatalog* catalog = m_sessions->catalog();
	QString title = catalog->titleFor(m_currentSessionId);
	if (title.isEmpty())   // 当前会话可能是子代理（不进侧栏，标题在另一张表里）
		title = catalog->childTitleFor(m_currentSessionId);
	return title.isEmpty() ? qtTrId("session_untitled") : title;
}

// 当前会话所属工作区的显示名；未分组（workspaceId 为空）返回空串，由 QML 决定是否隐藏。
QString QmlBridge::currentWorkspaceTitle() const
{
	const SessionCatalog* catalog = m_sessions ? m_sessions->catalog() : nullptr;
	if (!catalog || m_currentSessionId.isEmpty())
		return QString();

	const QString workspaceId = catalog->workspaceFor(m_currentSessionId);
	if (workspaceId.isEmpty())
		return QString();

	for (const WorkspaceRecord& workspace : catalog->workspaces()) {
		if (workspace.workspaceId == workspaceId)
			return workspace.title;
	}
	return QString();
}

QString QmlBridge::currentWorkspaceId() const
{
	const SessionCatalog* catalog = m_sessions ? m_sessions->catalog() : nullptr;
	if (!catalog || m_currentSessionId.isEmpty())
		return QString();

	return catalog->workspaceFor(m_currentSessionId);
}

// ------------------------------------------------------------------
// 会话收发：转调 ChatController（发送策略/自愈都在那边）
// ------------------------------------------------------------------

QVariantList QmlBridge::pendingAttachments() const
{
	return m_chat ? m_chat->pendingAttachments() : QVariantList();
}

void QmlBridge::chooseImageFiles()
{
	const QStringList files = QFileDialog::getOpenFileNames(
		nullptr, QStringLiteral("\u9009\u62e9\u56fe\u7247"), QString(),
		QStringLiteral("\u56fe\u7247 (*.png *.jpg *.jpeg *.webp *.gif)"));
	if (files.isEmpty())
		return;
	QVariantList urls;
	for (const QString& file : files)
		urls.append(QUrl::fromLocalFile(file).toString());
	addAttachments(urls);
}

void QmlBridge::addAttachments(const QVariantList& urls)
{
	if (m_chat)
		m_chat->addImageFiles(urls);
}

void QmlBridge::removeAttachment(int index)
{
	if (m_chat)
		m_chat->removePendingAttachment(index);
}

QString QmlBridge::attachmentSource(const QString& attachmentId)
{
	return m_chat ? m_chat->attachmentSource(attachmentId) : QString();
}

void QmlBridge::sendPrompt(const QString& text)
{
	if (!m_chat)
		return;

	// 斜杠命令**不进模型**：形如 `/name …` 的行交给命令处理器（解析/执行/报结果全在它那边）。
	// 判定用引擎同一条文法（`^/[a-z][a-z0-9_-]*(\s|$)`），不满足就当普通提示发；命令名认不认识由引擎裁定。
	if (m_commands && !m_currentSessionId.isEmpty() && CommandController::looksLikeCommand(text)) {
		m_commands->execute(m_currentSessionId, text);
		return;
	}

	m_chat->sendPrompt(m_currentSessionId, text,
		[this]() { m_runtime->restartServer(); },
		[this](const QString& line) {
			if (m_runtime->setStatus(line))
				emit statusTextChanged();
		});
}

void QmlBridge::cancelPrompt()
{
	m_chat->cancel();   // 见 ChatController::cancel
}

// ------------------------------------------------------------------
// 纯转发（getter / invokable）
// ------------------------------------------------------------------

bool QmlBridge::questionActive() const { return m_chat && m_chat->questionActive(); }
QVariantList QmlBridge::questions() const { return m_chat ? m_chat->questions() : QVariantList(); }
bool QmlBridge::approvalActive() const { return m_chat && m_chat->approvalActive(); }
QString QmlBridge::approvalToolName() const { return m_chat ? m_chat->approvalToolName() : QString(); }
QString QmlBridge::approvalReason() const { return m_chat ? m_chat->approvalReason() : QString(); }
void QmlBridge::submitAnswers(const QVariantList& answers) { if (m_chat) m_chat->submitAnswers(answers); }
void QmlBridge::respondApproval(bool allow) { if (m_chat) m_chat->respondApproval(allow); }
bool QmlBridge::olderAvailable() const { return m_chat && m_chat->olderAvailable(); }
bool QmlBridge::loadingOlder() const { return m_chat && m_chat->loadingOlder(); }
void QmlBridge::loadOlder() { if (m_chat) m_chat->loadOlder(); }

QAbstractItemModel* QmlBridge::messages() const { return m_chat ? m_chat->messages() : nullptr; }
QAbstractItemModel* QmlBridge::trace() const { return m_chat ? m_chat->trace() : nullptr; }
QVariantMap QmlBridge::traceSummary() const
{
	return m_chat && m_chat->trace() ? m_chat->trace()->summary() : QVariantMap();
}

void QmlBridge::setTracePaused(bool paused)
{
	// 轨迹视图在用户往回翻记录时调它：暂停追加，避免每次新事件都整表重建把滚动位置打回去
	if (m_chat && m_chat->trace())
		m_chat->trace()->setPaused(paused);
}
int QmlBridge::messageCount() const { return m_chat ? m_chat->messageCount() : 0; }
bool QmlBridge::streaming() const { return m_chat && m_chat->streaming(); }
bool QmlBridge::cancelling() const { return m_chat && m_chat->cancelling(); }
bool QmlBridge::lastTurnEmpty() const { return m_chat && !m_chat->lastTurnHadOutput(); }

QString QmlBridge::statsLine() const { return m_chat ? m_chat->statsLine() : QString(); }
QVariantList QmlBridge::modelGroups() const { return m_chat ? m_chat->modelGroups() : QVariantList(); }
QVariantList QmlBridge::reasoningLevels() const { return m_chat ? m_chat->reasoningLevels() : QVariantList(); }
QString QmlBridge::currentModelName() const { return m_chat ? m_chat->currentModelName() : QString(); }
QString QmlBridge::currentLevelName() const { return m_chat ? m_chat->currentLevelName() : QString(); }
bool QmlBridge::useGenericLevels() const { return m_chat && m_chat->useGenericLevels(); }
bool QmlBridge::hasModels() const { return m_chat && m_chat->hasModels(); }
bool QmlBridge::modelCatalogMissing() const { return m_chat && m_chat->modelCatalogMissing(); }
void QmlBridge::selectModel(const QString& provider, const QString& model) { if (m_chat) m_chat->selectModel(provider, model); }
void QmlBridge::selectLevel(const QString& levelId) { if (m_chat) m_chat->selectLevel(levelId); }
void QmlBridge::refreshModelCatalog() { if (m_chat) m_chat->refreshModelCatalog(); }

bool QmlBridge::initializing() const { return m_runtime && m_runtime->initializing(); }
QString QmlBridge::statusText() const { return m_runtime ? m_runtime->status() : QString(); }
QString QmlBridge::serverState() const { return m_runtime ? m_runtime->serverState() : QString(); }

// ------------------------------------------------------------------
// 主题 / 模型设置 / 诊断
// ------------------------------------------------------------------

void QmlBridge::toggleTheme()
{
	if (m_theme)
		m_theme->toggle();
}

bool QmlBridge::themeSwitching() const
{
	return m_theme && m_theme->switching();
}

// ---- 工具过滤器：桥只做"开关 + 拉一次" ----
void QmlBridge::openToolsFilter()
{
	// ⚠️ 这个函数**只允许被调一次**：若两处都调，一次点击就"开→立刻收"，肉眼表现是"点了没反应"。
	//    排查同类问题先看这行日志出现几次。
	qInfo().noquote() << "[Shell] openToolsFilter: open=" << m_toolsFilterOpen
		<< "session=" << (m_currentSessionId.isEmpty() ? QStringLiteral("(空)") : m_currentSessionId);

	if (m_toolsFilterOpen) {   // 顶栏那颗图标当开关用：再点一次收起
		closeToolsFilter();
		return;
	}
	if (m_currentSessionId.isEmpty()) {
		if (m_runtime->setStatus(qtTrId("toolfilter_no_session")))
			emit statusTextChanged();
		return;
	}

	m_toolsFilterOpen = true;
	emit toolsFilterOpenChanged();

	// 打开时拉一次该会话的目录（没有配置文件时由 C++ 侧写一份初始版）
	m_toolsFilter->setBaseUrl(m_runtime->apiClient()->baseUrl());
	m_toolsFilter->refresh(m_currentSessionId);
}

void QmlBridge::closeToolsFilter()
{
	if (!m_toolsFilterOpen)
		return;
	m_toolsFilterOpen = false;
	emit toolsFilterOpenChanged();
}

// ---- 右侧栏：纯界面开关（不涉及数据，也就不需要会话/服务端）----
void QmlBridge::togglePanelBar()
{
	// 与 openToolsFilter 同理：**只允许被调一次**。顶栏那颗按钮只发信号，由 Shell 接到这里；
	// 若两处都调，表现就是"点了没反应"（开→立刻收）。
	if (m_panelBarOpen)
		closePanelBar();
	else
		openPanelBar();
}

void QmlBridge::openPanelBar()
{
	if (m_panelBarOpen)
		return;
	qInfo().noquote() << "[Shell] openPanelBar";
	m_panelBarOpen = true;
	emit panelBarOpenChanged();
}

void QmlBridge::closePanelBar()
{
	if (!m_panelBarOpen)
		return;
	m_panelBarOpen = false;
	emit panelBarOpenChanged();
}

void QmlBridge::markdownAttach(QObject* quickDocument, const QVariantMap& colors)
{
	QQuickTextDocument* quick = qobject_cast<QQuickTextDocument*>(quickDocument);
	if (quick == nullptr)
		return;

	MarkdownDocumentState state;
	state.colors = colors;
	m_markdownDocuments.insert(quickDocument, state);

	if (QTextDocument* document = quick->textDocument())
		document->clear();
}

void QmlBridge::markdownUpdate(QObject* quickDocument, const QString& markdown)
{
	auto it = m_markdownDocuments.find(quickDocument);
	if (it == m_markdownDocuments.end())
		return;
	QQuickTextDocument* quick = qobject_cast<QQuickTextDocument*>(quickDocument);
	if (quick == nullptr)
		return;
	QTextDocument* document = quick->textDocument();
	if (document == nullptr)
		return;

	MarkdownDocumentState& state = it.value();
	if (markdown == state.text)
		return;

	// 流式正常情况下新文本是旧文本的前缀扩展；如果不是（重放/换消息），整篇重建。
	if (!state.text.isEmpty() && !markdown.startsWith(state.text)) {
		document->clear();
		state.stableOffset = 0;
		state.stablePos = 0;
	}

	if (markdown.isEmpty()) {
		document->clear();
		state.text.clear();
		state.stableOffset = 0;
		state.stablePos = 0;
		return;
	}

	// 稳定边界：最后一个空行。它之前的段落已经不会再变，只需追加进 QTextDocument；
	// 最后一个段落仍在流式增长，每次只替换文档末尾那一小段。
	int stableOffset = 0;
	const int blank = markdown.lastIndexOf(QStringLiteral("\n\n"));
	if (blank >= 0)
		stableOffset = blank + 2;

	if (stableOffset < state.stableOffset) {
		document->clear();
		state.stableOffset = 0;
		state.stablePos = 0;
	}

	// 先移除上一次渲染的尾段，保留已提交的稳定内容。
	QTextCursor tailCursor(document);
	tailCursor.setPosition(qBound(0, state.stablePos, qMax(0, document->characterCount() - 1)));
	tailCursor.movePosition(QTextCursor::End, QTextCursor::KeepAnchor);
	tailCursor.removeSelectedText();

	if (stableOffset > state.stableOffset) {
		const QString chunk = markdown.mid(state.stableOffset, stableOffset - state.stableOffset);
		const QTextDocumentFragment fragment = MarkdownSegmenter::renderFragment(chunk, state.colors);
		QTextCursor insertCursor(document);
		insertCursor.setPosition(qBound(0, state.stablePos, qMax(0, document->characterCount() - 1)));
		// insertFragment 会把 fragment 的首块并入当前块；stable 段从空行后开始，
		// 必须先起新块，否则标题会和上一段/下一段粘成同一块。
		if (insertCursor.position() > 0)
			insertCursor.insertBlock(QTextBlockFormat());
		insertCursor.insertFragment(fragment);
		state.stablePos = insertCursor.position();
		state.stableOffset = stableOffset;
	}

	const QString tail = markdown.mid(stableOffset);
	if (!tail.isEmpty()) {
		const QTextDocumentFragment fragment = MarkdownSegmenter::renderFragment(tail, state.colors);
		QTextCursor endCursor(document);
		endCursor.movePosition(QTextCursor::End);
		if (endCursor.position() > 0)
			endCursor.insertBlock(QTextBlockFormat());
		endCursor.insertFragment(fragment);
	}

	state.text = markdown;
}

void QmlBridge::markdownDetach(QObject* quickDocument)
{
	m_markdownDocuments.remove(quickDocument);
}

QVariantList QmlBridge::markdownTableLines(QObject* quickDocument)
{
	QVariantList lines;
	QQuickTextDocument* quick = qobject_cast<QQuickTextDocument*>(quickDocument);
	if (quick == nullptr)
		return lines;
	QTextDocument* document = quick->textDocument();
	if (document == nullptr)
		return lines;
	QAbstractTextDocumentLayout* layout = document->documentLayout();
	if (layout == nullptr)
		return lines;

	// 没有表格时不要 documentSize()：那会强制整篇文本重新布局，而 MarkdownBody 每个
	// 流式 tick 都会来取一次线坐标。先只走 frame 树判断有没有 QTextTable。
	bool hasTable = false;
	std::function<void(QTextFrame*)> findTable = [&](QTextFrame* frame) {
		if (frame == nullptr || hasTable)
			return;
		for (QTextFrame* child : frame->childFrames()) {
			if (dynamic_cast<QTextTable*>(child) != nullptr) {
				hasTable = true;
				return;
			}
			findTable(child);
			if (hasTable)
				return;
		}
	};
	findTable(document->rootFrame());
	if (!hasTable)
		return lines;

	// 强制一次布局，blockBoundingRect/frameBoundingRect 才是当前真实坐标。
	layout->documentSize();

	std::function<void(QTextFrame*)> walk = [&](QTextFrame* frame) {
		if (frame == nullptr)
			return;
		for (QTextFrame* child : frame->childFrames()) {
			if (QTextTable* table = dynamic_cast<QTextTable*>(child)) {
				const QRectF tableRect = layout->frameBoundingRect(table);
				const int rows = table->rows();
				if (!tableRect.isValid() || rows <= 0)
					continue;

				QVector<qreal> rowBottoms;
				rowBottoms.reserve(rows);
				for (int row = 0; row < rows; ++row) {
					qreal bottom = -1.0;
					for (int column = 0; column < table->columns(); ++column) {
						const QTextTableCell cell = table->cellAt(row, column);
						if (!cell.isValid())
							continue;
						const QTextTableCellFormat cellFormat = cell.format().toTableCellFormat();
						const QTextBlock lastBlock = cell.lastCursorPosition().block();
						const QRectF blockRect = layout->blockBoundingRect(lastBlock);
						bottom = qMax(bottom, blockRect.bottom() + cellFormat.bottomPadding());
					}
					if (bottom < 0.0)
						bottom = tableRect.top();
					rowBottoms.append(bottom);
				}
				for (int row = 0; row < rowBottoms.size(); ++row) {
					QVariantMap line;
					line.insert(QStringLiteral("x"), tableRect.left());
					line.insert(QStringLiteral("y"), rowBottoms.at(row));
					line.insert(QStringLiteral("w"), tableRect.width());
					lines.append(line);
				}
			}
			walk(child);
		}
	};
	walk(document->rootFrame());
	return lines;
}

QString QmlBridge::highlightCode(const QString& language, const QString& code) const
{
	return CodeHighlighter::instance().highlight(language, code);
}

void QmlBridge::openModelSettings()
{
	DshApiClient* api = m_runtime ? m_runtime->apiClient() : nullptr;
	if (!api) {
		if (m_runtime && m_runtime->setStatus(qtTrId("server_not_ready_yet")))
			emit statusTextChanged();
		return;
	}

	QUrl url = api->baseUrl();  // 带 launchToken 的地址才能铸出 /api 的认证 cookie；不带则直接 404
	if (url.isEmpty())
		url = QUrl(QStringLiteral("http://127.0.0.1:3080/"));

	const QString token = api->launchToken();
	if (!token.isEmpty()) {
		QUrlQuery query;
		query.addQueryItem(QStringLiteral("token"), token);
		url.setQuery(query);
	}

	qInfo().noquote() << "[Shell] openModelSettings ->" << url.toString(QUrl::RemoveQuery)
		<< "(token present:" << !token.isEmpty() << ")";
	QDesktopServices::openUrl(url);
}

void QmlBridge::probe(const QString& tag, const QString& detail)
{
	qInfo().noquote() << "[Probe]" << tag << detail;  // 诊断专用：调用方是 QML（消息区滚动条行为），输出进 info.log
}
