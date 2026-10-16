#pragma once

// QmlBridge —— **QML 唯一的门**（QML 单例名 `Hub`）。
//
// 分层规矩（领域归位 2026-10-15，全仓通用）：
//   · common/**    = 领域与纯逻辑（解析、策略、状态机、模型本体）：不 import QML 概念，能脱离 QML 引擎单测。
//   · qmlbridge/** = 只做接线：属性/信号、Q_INVOKABLE 转发、QAbstractListModel 的角色名映射，**不放算法**。
//   留在本目录的接线对象：QmlBridge（本类）、ThemeBridge、WindowBridge、UrlInputBridge、PanelBarLayout（只转发）；
//   带逻辑的模型与控制器（SessionListModel / CommandController / SettingsController / PanelDockModel /
//   PluginMarketController / StyleResolver）已搬去 common/**，算法在那边、QML 面在这里。
//
// ⚠️ 本类的职责被刻意收窄成**只有接线**（2026-10-10 拆分，之前叫 ShellController）：
//   · 把核心层（core/ClientRuntime、core/ServerManager）与业务层（common/session/**、common/appearance/**）
//     的对象与信号，接到 QML 能用的形态上：Q_PROPERTY / Q_INVOKABLE / 信号转发；
//   · 持有"当前会话 id"这类**纯界面状态**，并在它们变化时发对应信号；
//   · 把 QML 的动作转成一次调用（`Hub.xxx()` → 某个核心/业务对象的方法）。
//
// ❌ 本类**不许**出现的东西（出现就说明逻辑又漏回来了）：
//   · 网络/RPC 细节（QNetworkAccessManager、DshApiClient::callMethod、JSON 解析）
//   · 服务端生命周期（起/停/重启）—— 在 core/ClientRuntime
//   · 发送策略、自愈状态机、模型目录判断 —— 在 common/session/ChatController
//   · 主题切换流程、落盘 —— 在 qmlbridge/ThemeController（它虽是 QML 面驱动，但流程本身不是接线）
//   · 会自己改数据模型的业务分支（目录更新、归档集合写回）—— 在 core/ClientRuntime
//
// 分层依赖方向（自上而下，不许反向）：
//   qmlbridge  →  core / common/session / common/appearance  →  common/util / network
//
// 原 ShellController 的那段历史上下文（保留）：M3 切片刻意**不复用**旧的 ULinHub（QMainWindow），
// 只用既有无控件依赖类把数据面重接一遍，M8 删旧壳时再合一。

#include "common/PluginMarket/PluginMarketController.h"
#include "common/session/CommandController.h"
#include "common/session/SessionListModel.h"
#include "common/settings/SettingsController.h"
#include "qmlbridge/ExtensionManagerController.h"

#include <QObject>
#include <QString>

class ChatController;
class ClientRuntime;
class DshApiClient;
class ThemeController;
class ToolsFilter;

// Q_PROPERTY 里用的是 ToolsFilter*，但这里只前置声明（完整定义在 common/session/ToolsFilter.h）。
// 指针元类型指向未定义类型时必须显式声明成 opaque pointer，否则 moc 生成的元对象编不过
// （qmetatype.h 的 static_assert：「Pointer Meta Types must either point to fully-defined types
// or be declared with Q_DECLARE_OPAQUE_POINTER(T *)」）。
Q_DECLARE_OPAQUE_POINTER(ToolsFilter*)

class QmlBridge : public QObject
{
	Q_OBJECT
	Q_PROPERTY(SessionListModel* sessions READ sessions CONSTANT)
	Q_PROPERTY(SettingsController* settings READ settings CONSTANT)  // 设置页数据面（模型 / API key）：QML 走 Hub.settings.*
	Q_PROPERTY(PluginMarketController* plugins READ plugins CONSTANT)  // 插件市场数据面：QML 走 Hub.plugins.*
	Q_PROPERTY(ExtensionManagerController* extensions READ extensions CONSTANT)  // 扩展管理数据面：QML 走 Hub.extensions.*
	Q_PROPERTY(CommandController* commands READ commands CONSTANT)  // 斜杠命令（发现/过滤/执行）：QML 走 Hub.commands.*
	// ---- 交互面板（M4b）：状态在 ChatController，这里只转发 ----
	Q_PROPERTY(bool questionActive READ questionActive NOTIFY interactionChanged)
	Q_PROPERTY(QVariantList questions READ questions NOTIFY interactionChanged)
	Q_PROPERTY(bool approvalActive READ approvalActive NOTIFY interactionChanged)
	Q_PROPERTY(QString approvalToolName READ approvalToolName NOTIFY interactionChanged)
	Q_PROPERTY(QString approvalReason READ approvalReason NOTIFY interactionChanged)
	// ---- M4b 翻页（转发 ChatController）----
	Q_PROPERTY(bool olderAvailable READ olderAvailable NOTIFY olderChanged)
	Q_PROPERTY(bool loadingOlder READ loadingOlder NOTIFY olderChanged)
	// ---- 消息区 ----
	Q_PROPERTY(QAbstractItemModel* messages READ messages CONSTANT)
	Q_PROPERTY(int messageCount READ messageCount NOTIFY messageCountChanged)
	Q_PROPERTY(bool streaming READ streaming NOTIFY streamingChanged)
	Q_PROPERTY(bool lastTurnEmpty READ lastTurnEmpty NOTIFY lastTurnEmptyChanged)
	// ---- 当前会话 / 初始化状态 ----
	Q_PROPERTY(QString currentSessionId READ currentSessionId NOTIFY currentSessionChanged)
	Q_PROPERTY(QString currentSessionTitle READ currentSessionTitle NOTIFY currentSessionChanged)
	Q_PROPERTY(bool initializing READ initializing NOTIFY initializingChanged)
	Q_PROPERTY(QString statusText READ statusText NOTIFY statusTextChanged)
	Q_PROPERTY(QString serverState READ serverState NOTIFY serverStateChanged)
	// ---- 输入区：小灰字统计行 + 模型/思考 chip（数据在 ChatController，这里只转发）----
	Q_PROPERTY(QString statsLine READ statsLine NOTIFY statsLineChanged)
	Q_PROPERTY(QVariantList modelGroups READ modelGroups NOTIFY modelCatalogChanged)
	Q_PROPERTY(QVariantList reasoningLevels READ reasoningLevels NOTIFY modelCatalogChanged)
	Q_PROPERTY(QString currentModelName READ currentModelName NOTIFY modelCatalogChanged)
	Q_PROPERTY(QString currentLevelName READ currentLevelName NOTIFY modelCatalogChanged)
	Q_PROPERTY(bool useGenericLevels READ useGenericLevels NOTIFY modelCatalogChanged)
	Q_PROPERTY(bool hasModels READ hasModels NOTIFY modelCatalogChanged)
	Q_PROPERTY(bool modelCatalogMissing READ modelCatalogMissing NOTIFY modelCatalogChanged)
	// 主题切换的过渡卡片（原版 #themeSwitchBody：360×200 + spinner + theme_switching）
	Q_PROPERTY(bool themeSwitching READ themeSwitching NOTIFY themeSwitchingChanged)
	// 工具过滤器：模型（common/session/ToolsFilter）+ 面板开关。QML 用 Hub.toolsFilter.* 取数据、
	// 用 Hub.toolsFilterOpen 决定面板是否装载；面板里的关闭走 closeToolsFilter()。
	Q_PROPERTY(ToolsFilter* toolsFilter READ toolsFilter CONSTANT)
	Q_PROPERTY(bool toolsFilterOpen READ toolsFilterOpen NOTIFY toolsFilterOpenChanged)
	// 右侧栏（面板坞）开合：纯界面状态，QML 用它决定面板是否装载 / 压窄多少；
	// 顶栏那颗 ◫ 走 togglePanelBar()，面板自己的 ✕ 走 closePanelBar()。
	Q_PROPERTY(bool panelBarOpen READ panelBarOpen NOTIFY panelBarOpenChanged)

public:
	static QmlBridge& instance();  // 程序级唯一实例；刻意不析构 —— QML 单例注册要求它活过引擎
	void init();  // 启动期调用（QML 装载之前）：建核心对象、接线、拉起内置服务端

	SessionListModel* sessions() const { return m_sessions; }
	SettingsController* settings() const { return m_settings; }
	PluginMarketController* plugins() const { return m_plugins; }
	ExtensionManagerController* extensions() const { return m_extensions; }
	CommandController* commands() const { return m_commands; }

	// ---- 交互面板（转发 ChatController）----
	bool questionActive() const;
	QVariantList questions() const;
	bool approvalActive() const;
	QString approvalToolName() const;
	QString approvalReason() const;
	Q_INVOKABLE void submitAnswers(const QVariantList& answers);
	Q_INVOKABLE void respondApproval(bool allow);
	// ---- M4b 翻页 ----
	bool olderAvailable() const;
	bool loadingOlder() const;
	Q_INVOKABLE void loadOlder();
	// ---- 消息区（转发）----
	QAbstractItemModel* messages() const;
	int messageCount() const;
	bool streaming() const;
	bool lastTurnEmpty() const;
	// ---- 输入区 chip（转发）----
	QString statsLine() const;
	QVariantList modelGroups() const;
	QVariantList reasoningLevels() const;
	QString currentModelName() const;
	QString currentLevelName() const;
	bool useGenericLevels() const;
	bool hasModels() const;
	bool modelCatalogMissing() const;
	Q_INVOKABLE void selectModel(const QString& provider, const QString& model);
	Q_INVOKABLE void selectLevel(const QString& levelId);
	Q_INVOKABLE void refreshModelCatalog();

	// ---- 会话 / 工作区（转调 core/ClientRuntime）----
	QString currentSessionId() const { return m_currentSessionId; }
	QString currentSessionTitle() const;
	Q_INVOKABLE void selectSession(const QString& sessionId);
	Q_INVOKABLE void refreshSessions();
	Q_INVOKABLE void createSession(const QString& workspaceId = QString());
	Q_INVOKABLE void createWorkspace();               // 选目录在桥里（QFileDialog 是控件层的活），RPC 在 runtime
	Q_INVOKABLE void deleteSession(const QString& sessionId);
	Q_INVOKABLE void deleteWorkspace(const QString& workspaceId);
	// 清空会话（侧栏那颗 danger 按钮）：业务全在 core，这里只转调
	Q_INVOKABLE void clearAllSessions();

	// ---- 会话收发 ----
	Q_INVOKABLE void sendPrompt(const QString& text);
	Q_INVOKABLE void cancelPrompt();

	// ---- 初始化状态（值在 core/ClientRuntime）----
	bool initializing() const;
	QString statusText() const;
	QString serverState() const;

	// ---- 主题 / 模型设置 ----
	Q_INVOKABLE void toggleTheme();
	bool themeSwitching() const;
	// ---- 工具过滤器（面板开关在桥里，数据全在 ToolsFilter）----
	ToolsFilter* toolsFilter() const { return m_toolsFilter; }
	bool toolsFilterOpen() const { return m_toolsFilterOpen; }
	bool panelBarOpen() const { return m_panelBarOpen; }
	// 顶栏 ☰：开着就收起，否则打开并拉一次该会话的工具目录
	Q_INVOKABLE void openToolsFilter();
	Q_INVOKABLE void closeToolsFilter();
	// 右侧栏：顶栏当开关用（toggle），面板自己的 ✕ 直接 close
	Q_INVOKABLE void togglePanelBar();
	Q_INVOKABLE void openPanelBar();
	Q_INVOKABLE void closePanelBar();
	// 旧壳是 app 内设置窗口（Settings(m_api)）；QML 侧等 M6 再做等价页面，这条通路先解决"配不了模型"
	Q_INVOKABLE void openModelSettings();
	// QML 侧诊断出口：console.log 不落盘，诊断信息统一从这里走项目 Logger（qInfo → logs/general/info.log）
	Q_INVOKABLE void probe(const QString& tag, const QString& detail);

signals:
	void currentSessionChanged();
	void initializingChanged();
	void statusTextChanged();
	void serverStateChanged();
	void messageCountChanged();
	void streamingChanged();
	void lastTurnEmptyChanged();
	void statsLineChanged();
	void modelCatalogChanged();
	void themeSwitchingChanged();
	void toolsFilterOpenChanged();
	// 右侧栏开合（顶栏 ◫ / 面板 ✕）
	void panelBarOpenChanged();
	void interactionChanged();
	void olderChanged();
	void olderPrepended();
	void sessionCreateFailed(const QString& message);  // 新建会话失败（QML 用 Toast 显示：statusText 只在初始化遮罩里可见）

private:
	explicit QmlBridge(QObject* parent = nullptr);
	QmlBridge(const QmlBridge&) = delete;
	QmlBridge& operator=(const QmlBridge&) = delete;

	// 接线：把 core 与业务层的信号接到本类/QML 面（init 里调用；只做 connect，不做业务）
	void wireRuntime();
	void wireChat();

	ClientRuntime* m_runtime = nullptr;
	ChatController* m_chat = nullptr;
	ThemeController* m_theme = nullptr;
	ToolsFilter* m_toolsFilter = nullptr;
	SessionListModel* m_sessions = nullptr;
	SettingsController* m_settings = nullptr;
	PluginMarketController* m_plugins = nullptr;
	ExtensionManagerController* m_extensions = nullptr;
	CommandController* m_commands = nullptr;
	QString m_currentSessionId;   // 纯界面状态：当前会话（消息区/跟随流都按它走）
	bool m_toolsFilterOpen = false;   // 纯界面状态：工具过滤面板是否打开
	bool m_panelBarOpen = false;  // 纯界面状态：右侧栏是否展开（不落盘，暂不跨会话记忆）
};
