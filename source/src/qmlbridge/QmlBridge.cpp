// 见 QmlBridge.h：QML 唯一的门，只做接线（转调 + 属性/信号转发）。
// 本文件由原 src/qmlbridge/ShellController.cpp 拆分而来（2026-10-10）：
//   服务端/客户端生命周期与会话 RPC  → core/ClientRuntime
//   发送策略与自愈状态机            → common/session/ChatController
//   主题切换编排                    → qmlbridge/ThemeController
// 留在这里的只有接线，以及"当前会话 id"这类纯界面状态。
//
// 2026-10-15 领域归位：带逻辑的模型/控制器（SessionListModel / CommandController / SettingsController /
// PanelDockModel / PluginMarketController / StyleResolver）搬去 common/**，本文件只留下接线与转调。

#include "qmlbridge/QmlBridge.h"

#include "common/session/ChatController.h"
#include "common/session/ToolsFilter.h"
#include "common/util/MarkdownSegmenter.h"
#include "core/ClientRuntime.h"
#include "network/DshApiClient.h"
#include "qmlbridge/TerminalBridge.h"
#include "qmlbridge/ThemeController.h"
#include "qmlbridge/ThemeBridge.h"

#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QUrl>
#include <QUrlQuery>

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
	// 插件市场（M6）：HTTP 与解析走既有 PluginMarketClient / PluginMarketModel
	m_plugins = new PluginMarketController(api, this);
	// 扩展管理：工具扩展 / 客户端扩展两种来源在 ExtensionManagerController 里合成
	m_extensions = new ExtensionManagerController(m_runtime->server(), this);
	// 斜杠命令（发现/过滤/执行）：命令不进模型历史，单独一层；发送分支在 sendPrompt()
	m_commands = new CommandController(api, this);
	// 右栏「终端」块：会话对象由 TerminalBridge 按块 id 管，这里只把唯一的出站客户端交给它
	TerminalBridge::instance().init(api);

	// 旧壳 ULinHub.042：用户保存服务器地址后重启内置服务端
	connect(m_settings, &SettingsController::serverRestartRequested, this, [this]() {
		m_runtime->restartServer();
		});
	// 风格包重置后热重载 QML 侧 token：SettingsController 在 common（领域层），不许反向 include qmlbridge，
	// 于是由它发信号、这里落地（等价于搬家前 SettingsController 里直接调 ThemeBridge::reload()）。
	connect(m_settings, &SettingsController::stylesReset, this, []() {
		ThemeBridge::instance().reload();
		});
	// 插件市场装完市场包也要重启内置服务端（新插件才会被加载）：PluginMarketController 在 common（领域层），
	// 不许反向 include core，于是由它发信号、这里落地（等价于搬家前它在 installFinished 里直调 ServerManager::restart()）。
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

	// 目录变了 → 列表模型整份重建；列表空时给一句引导（与旧壳同一文案位置）
	connect(m_runtime, &ClientRuntime::catalogChanged, this, [this]() {
		if (m_sessions)
			m_sessions->rebuild();
		});

	// 列表拉回来：按服务端建议自动选中（旧壳规则：第一个非运行中的会话）
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
	});

	// 会话被归档：删的正是当前会话就清空消息区、回到占位页（目录由 runtime 更新，这里只管界面状态）
	connect(m_runtime, &ClientRuntime::sessionArchived, this, [this](const QString& sessionId) {
		if (sessionId.isEmpty() || m_currentSessionId != sessionId)
			return;

		m_currentSessionId.clear();
		if (m_chat)
			m_chat->setSession(QString());
		emit currentSessionChanged();
	});

	connect(m_runtime, &ClientRuntime::operationFailed, this, [](const QString&) {});
	connect(m_runtime, &ClientRuntime::sessionCreateFailed, this, &QmlBridge::sessionCreateFailed);

	// 会话被整份清空：当前会话连同消息区一起作废（清空发生在"当前会话"还在的时候）。
	// ⚠️ 必须把选中态也清掉：下一句 sessionsRefreshed 的自动选中在宿主那边是 `keep.isEmpty()` 才生效，
	//    留着旧选中项就等于新会话建出来了却选不上。
	connect(m_runtime, &ClientRuntime::sessionsCleared, this, [this]() {
		m_currentSessionId.clear();
		if (m_chat)
			m_chat->setSession(QString());
		if (m_sessions)
			m_sessions->setSelectedSession(QString());
		emit currentSessionChanged();
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
	// 事件入口（follow 快照 / mux 帧 / 会话投影）由 ChatController 自己接 —— 它的 connectApi()
	// 里接的，这里只管把它的信号转发给 QML。桥因此不碰 QJson / 不认识事件结构。
	m_chat->connectApi();

	connect(m_chat, &ChatController::messagesChanged, this, &QmlBridge::messageCountChanged);
	connect(m_chat, &ChatController::streamingChanged, this, &QmlBridge::streamingChanged);
	connect(m_chat, &ChatController::turnFinished, this, &QmlBridge::lastTurnEmptyChanged);
	connect(m_chat, &ChatController::statsLineChanged, this, &QmlBridge::statsLineChanged);
	connect(m_chat, &ChatController::interactionChanged, this, &QmlBridge::interactionChanged);
	connect(m_chat, &ChatController::olderChanged, this, &QmlBridge::olderChanged);
	connect(m_chat, &ChatController::olderPrepended, this, &QmlBridge::olderPrepended);
	connect(m_chat, &ChatController::catalogChanged, this, &QmlBridge::modelCatalogChanged);
}

// ------------------------------------------------------------------
// 会话 / 工作区：转调 core（换算是 core 的事，界面 key 只在这里出现）
// ------------------------------------------------------------------

void QmlBridge::refreshSessions()
{
	m_runtime->refreshSessions();
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

void QmlBridge::selectSession(const QString& sessionId)
{
	if (sessionId.isEmpty() || m_currentSessionId == sessionId)
		return;

	m_currentSessionId = sessionId;

	// 消息区换会话（清空 + 记住当前会话），再把跟随流切过去：首帧快照会由 ChatController 接收
	m_chat->setSession(sessionId);
	m_chat->refreshModelCatalog();  // 输入区的模型/思考 chip 按会话重拉
	m_runtime->followSession(sessionId);
	// 命令是**按 agent（会话）注册**的：换会话必须重拉一份，否则会拿上一个会话的命令表
	m_commands->refresh(sessionId);

	m_sessions->setSelectedSession(sessionId);
	emit currentSessionChanged();
}

QString QmlBridge::currentSessionTitle() const
{
	if (m_currentSessionId.isEmpty() || !m_sessions)
		return QString();

	const QString title = m_sessions->catalog()->titleFor(m_currentSessionId);
	return title.isEmpty() ? qtTrId("session_untitled") : title;
}

// ------------------------------------------------------------------
// 会话收发：转调 ChatController（发送策略/自愈都在那边）
// ------------------------------------------------------------------

void QmlBridge::sendPrompt(const QString& text)
{
	if (!m_chat)
		return;

	// 斜杠命令**不进模型**：形如 `/name …` 的行交给命令处理器（解析/执行/报结果全在它那边）。
	// 判定用引擎同一条文法（`^/[a-z][a-z0-9_-]*(\s|$)`）；不满足就当普通提示发出去 ——
	// 用户写 "/ 你好" 这种不会被吞掉。命令名认不认识由引擎裁定（execute 回 undefined）。
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
int QmlBridge::messageCount() const { return m_chat ? m_chat->messageCount() : 0; }
bool QmlBridge::streaming() const { return m_chat && m_chat->streaming(); }
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
	// ⚠️ 这个函数**只允许被调一次**。踩过的坑：顶栏按钮一度既直调它、又发信号让 Shell 再调它，
	//    于是一次点击"开→立刻收"，肉眼表现就是"点了没反应"。排查同类问题时先看这行日志出现几次。
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
	// 若哪天两处都调，表现就是"点了没反应"（开→立刻收），排查时先看这行日志出现几次。
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
