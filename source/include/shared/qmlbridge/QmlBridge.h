#pragma once

// QmlBridge —— **QML 唯一的门**（QML 单例名 `Hub`）。
// 分层规矩：
//  · common/** = 领域与纯逻辑（解析、策略、状态机、模型本体）：不 import QML 概念，能脱离 QML 引擎单测。
//  · qmlbridge/** = 只做接线：属性/信号、Q_INVOKABLE 转发、QAbstractListModel 的角色名映射，**不放算法**。
//  留在本目录的接线对象：QmlBridge（本类）、ThemeBridge、WindowBridge、UrlInputBridge。
//  带逻辑的模型与控制器（SessionListModel / CommandController / SettingsController / PanelBarDockModel /
//  PluginMarketController / StyleResolver）在 common/**，算法在那边、QML 面在这里。
// ⚠️ 本类的职责被刻意收窄成**只有接线**：
//  · 把核心层（core/ClientRuntime、core/ServerManager）与业务层（common/session/**、common/appearance/**）
//  的对象与信号，接到 QML 能用的形态上：Q_PROPERTY / Q_INVOKABLE / 信号转发；
//  · 持有"当前会话 id"这类**纯界面状态**，并在它们变化时发对应信号；
//  · 把 QML 的动作转成一次调用（`Hub.xxx` → 某个核心/业务对象的方法）。
// ❌ 本类**不许**出现的东西（出现就说明逻辑又漏回来了）：
//  · 网络/RPC 细节（QNetworkAccessManager、DshApiClient::callMethod、JSON 解析）
//  · 服务端生命周期（起/停/重启）—— 在 core/ClientRuntime
//  · 发送策略、自愈状态机、模型目录判断 —— 在 common/session/ChatController
//  · 主题切换流程、落盘 —— 在 qmlbridge/ThemeController（它虽是 QML 面驱动，但流程本身不是接线）
//  · 会自己改数据模型的业务分支（目录更新、归档集合写回）—— 在 core/ClientRuntime
// 分层依赖方向（自上而下，不许反向）：
//  qmlbridge → core / common/session / common/appearance → common/util / network

#include "common/PluginMarket/PluginMarketController.h"
#include "common/session/CommandController.h"
#include "common/session/SessionListModel.h"
#include "common/settings/SettingsController.h"
#include "qmlbridge/ExtensionManagerController.h"

#include <QObject>
#include <QHash>
#include <QString>
#include <QVariantMap>

class ChatController;
class ClientRuntime;
class DshApiClient;
class QTimer;
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
	Q_PROPERTY(bool questionActive READ questionActive NOTIFY interactionChanged)
	Q_PROPERTY(QVariantList questions READ questions NOTIFY interactionChanged)
	Q_PROPERTY(QVariantList answerSelections READ answerSelections NOTIFY answerSelectionsChanged)
	Q_PROPERTY(bool approvalActive READ approvalActive NOTIFY interactionChanged)
	Q_PROPERTY(QString approvalToolName READ approvalToolName NOTIFY interactionChanged)
	Q_PROPERTY(QString approvalReason READ approvalReason NOTIFY interactionChanged)
	Q_PROPERTY(bool olderAvailable READ olderAvailable NOTIFY olderChanged)
	Q_PROPERTY(bool loadingOlder READ loadingOlder NOTIFY olderChanged)
	Q_PROPERTY(QAbstractItemModel* messages READ messages CONSTANT)
	// 轨迹视图的记录表：QML 走 Hub.trace（模型本体在 common/session/TraceModel）
	Q_PROPERTY(QAbstractItemModel* trace READ trace CONSTANT)
	// 轨迹顶部概览。单独开一条属性而不是让 QML 写 Hub.trace.summary：
	// 属性的静态类型是 QAbstractItemModel*，QML 在编译期查不到 TraceModel 的自定义属性，
	// 只有走这里才是类型安全的。
	Q_PROPERTY(QVariantMap traceSummary READ traceSummary NOTIFY traceSummaryChanged)
	Q_PROPERTY(int messageCount READ messageCount NOTIFY messageCountChanged)
	Q_PROPERTY(bool streaming READ streaming NOTIFY streamingChanged)
	// 已发出中止、正在等回合收尾（输入区的停止键据此显示"停止中"）
	Q_PROPERTY(bool cancelling READ cancelling NOTIFY cancellingChanged)
	Q_PROPERTY(bool lastTurnEmpty READ lastTurnEmpty NOTIFY lastTurnEmptyChanged)
	Q_PROPERTY(QString currentSessionId READ currentSessionId NOTIFY currentSessionChanged)
	Q_PROPERTY(QString currentSessionTitle READ currentSessionTitle NOTIFY currentSessionChanged)
	// 当前会话所属工作区的显示名（"未分组" = 空串）。顶栏面包屑/输入框上下文带用；
	// 工作区目录由 SessionCatalog 持有，工作区重命名后下一次 currentSessionChanged 刷新。
	Q_PROPERTY(QString currentWorkspaceTitle READ currentWorkspaceTitle NOTIFY currentSessionChanged)
	Q_PROPERTY(QString currentWorkspaceId READ currentWorkspaceId NOTIFY currentSessionChanged)
	// 当前工作区根目录（文件浏览面板用；空 = 未分组/未知）
	Q_PROPERTY(QString currentWorkspacePath READ currentWorkspacePath NOTIFY currentSessionChanged)
	Q_PROPERTY(bool initializing READ initializing NOTIFY initializingChanged)
	// 已选中会话、首帧历史还没到：加载期间 QML 不显示任何空状态占位。
	Q_PROPERTY(bool sessionLoading READ sessionLoading NOTIFY sessionLoadingChanged)
	Q_PROPERTY(QString statusText READ statusText NOTIFY statusTextChanged)
	Q_PROPERTY(QString serverState READ serverState NOTIFY serverStateChanged)
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
	// 用 Hub.toolsFilterOpen 决定面板是否装载；面板里的关闭走 closeToolsFilter。
	Q_PROPERTY(ToolsFilter* toolsFilter READ toolsFilter CONSTANT)
	Q_PROPERTY(bool toolsFilterOpen READ toolsFilterOpen NOTIFY toolsFilterOpenChanged)
	// Agent 过滤器里的“技能管理”页：官方 skills/list 的投影（只读目录 + 打开指令文件）。
	Q_PROPERTY(QVariantList skills READ skills NOTIFY skillsChanged)
	Q_PROPERTY(bool skillsBusy READ skillsBusy NOTIFY skillsChanged)
	Q_PROPERTY(QString skillsStatus READ skillsStatus NOTIFY skillsChanged)
	// 当前会话的权限预设（read-only / workspace-write / danger-full-access）。
	// 数据来自 session/projections 的 values.permissions.currentValue；切换走 /permission 命令。
	Q_PROPERTY(QString permissionPreset READ permissionPreset NOTIFY permissionPresetChanged)
	// 右侧栏（面板坞）开合：纯界面状态，QML 用它决定面板是否装载 / 压窄多少；
	// 顶栏那颗 ◫ 走 togglePanelBar，面板自己的 ✕ 走 closePanelBar。
	Q_PROPERTY(bool panelBarOpen READ panelBarOpen NOTIFY panelBarOpenChanged)
	Q_PROPERTY(QVariantList peerTabs READ peerTabs NOTIFY peerTabsChanged)
	Q_PROPERTY(QString peerRootId READ peerRootId NOTIFY peerTabsChanged)
	Q_PROPERTY(QString peerRootTitle READ peerRootTitle NOTIFY peerTabsChanged)
	Q_PROPERTY(int subagentCount READ subagentCount NOTIFY peerTabsChanged)
	Q_PROPERTY(bool subagentRunning READ subagentRunning NOTIFY peerTabsChanged)
	Q_PROPERTY(QVariantList jobs READ jobs NOTIFY jobsChanged)
	Q_PROPERTY(int jobCount READ jobCount NOTIFY jobsChanged)
	Q_PROPERTY(bool jobRunning READ jobRunning NOTIFY jobsChanged)

public:
	static QmlBridge& instance();  // 程序级唯一实例；刻意不析构 —— QML 单例注册要求它活过引擎
	void init();  // 启动期调用（QML 装载之前）：建核心对象、接线、拉起内置服务端

	SessionListModel* sessions() const { return m_sessions; }
	SettingsController* settings() const { return m_settings; }
	PluginMarketController* plugins() const { return m_plugins; }
	ExtensionManagerController* extensions() const { return m_extensions; }
	CommandController* commands() const { return m_commands; }

	bool questionActive() const;
	QVariantList questions() const;
	QVariantList answerSelections() const;
	bool approvalActive() const;
	QString approvalToolName() const;
	QString approvalReason() const;
	Q_INVOKABLE void submitAnswers(const QVariantList& answers);
	Q_INVOKABLE void toggleAnswerOption(int questionIndex, const QString& label);
	Q_INVOKABLE void setAnswerCustom(int questionIndex, const QString& text);
	Q_INVOKABLE void submitAnswerSelections();
	Q_INVOKABLE void respondApproval(bool allow);
	bool olderAvailable() const;
	bool loadingOlder() const;
	Q_INVOKABLE void loadOlder();
	// 轨迹视图：用户往回翻时暂停记录表追加（恢复时补一次重建）
	Q_INVOKABLE void setTracePaused(bool paused);
	QAbstractItemModel* messages() const;
	QAbstractItemModel* trace() const;
	QVariantMap traceSummary() const;
	int messageCount() const;
	bool streaming() const;
	bool cancelling() const;
	bool lastTurnEmpty() const;
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

	QString currentSessionId() const { return m_currentSessionId; }
	QString currentSessionTitle() const;
	QString currentWorkspaceTitle() const;
	QString currentWorkspaceId() const;
	QString currentWorkspacePath() const;
	Q_INVOKABLE void selectSession(const QString& sessionId);
	Q_INVOKABLE void refreshSessions();
	// 轻量：只刷 running（子代理页签/列表的"运行中"）。顶栏在打开「子智能体」下拉前调它，
	// 保证看到的是当下的状态，而不是启动时那份 session.list 快照。
	Q_INVOKABLE void refreshSessionStates();
	// 权限预设：读当前会话的 projection；切换通过命令 /permission <preset> 走引擎的官方写路径。
	Q_INVOKABLE void refreshPermissionPreset();
	Q_INVOKABLE void setPermissionPreset(const QString& preset);
	QString permissionPreset() const { return m_permissionPreset; }
	Q_INVOKABLE void createSession(const QString& workspaceId = QString());
	Q_INVOKABLE void createWorkspace();               // 选目录在桥里（QFileDialog 是控件层的活），RPC 在 runtime
	Q_INVOKABLE void deleteSession(const QString& sessionId);
	Q_INVOKABLE void deleteWorkspace(const QString& workspaceId);
	// 清空会话（侧栏那颗 danger 按钮）：业务全在 core，这里只转调
	Q_INVOKABLE void clearAllSessions();

	Q_PROPERTY(QVariantList pendingAttachments READ pendingAttachments NOTIFY pendingAttachmentsChanged)
	// 任一附件字节到位就 +1：QML 把它放进 Image.source 的表达式里触发重取
	Q_PROPERTY(int attachmentRevision READ attachmentRevision NOTIFY attachmentRevisionChanged)
	QVariantList pendingAttachments() const;
	int attachmentRevision() const { return m_attachmentRevision; }

	Q_INVOKABLE void addAttachments(const QVariantList& urls);
	// 原生文件对话框（避免依赖 QtQuick.Dialogs 部署）
	Q_INVOKABLE void chooseImageFiles();
	Q_INVOKABLE void removeAttachment(int index);
	Q_INVOKABLE QString attachmentSource(const QString& attachmentId);

	Q_INVOKABLE void sendPrompt(const QString& text);
	Q_INVOKABLE void cancelPrompt();

	bool initializing() const;
	bool sessionLoading() const;
	QString statusText() const;
	QString serverState() const;

	Q_INVOKABLE void toggleTheme();
	bool themeSwitching() const;
	ToolsFilter* toolsFilter() const { return m_toolsFilter; }
	bool toolsFilterOpen() const { return m_toolsFilterOpen; }
	QVariantList skills() const { return m_skills; }
	bool skillsBusy() const { return m_skillsBusy; }
	QString skillsStatus() const { return m_skillsStatus; }
	bool panelBarOpen() const { return m_panelBarOpen; }
	QVariantList peerTabs() const;
	// 页签组根：当前会话的父会话（父目录已知时），否则当前会话自己。
	// ⚠️ 它是 m_peerRootId 的读法，**不是**遍历全量 subagentCatalog —— 页签行只属于这一个组。
	QString peerRootId() const { return m_peerRootId; }
	QString peerRootTitle() const;
	int subagentCount() const;
	bool subagentRunning() const;
	QVariantList jobs() const;
	int jobCount() const;
	bool jobRunning() const;
	// 顶栏 ☰：开着就收起，否则打开并拉一次该会话的工具目录
	Q_INVOKABLE void openToolsFilter();
	Q_INVOKABLE void closeToolsFilter();
	Q_INVOKABLE void refreshSkills(const QString& sessionId = QString());
	// 应用内查看 SKILL.md：读文本给 QML 详情页，不再交给系统 .md 关联。
	Q_INVOKABLE QString readSkillText(const QString& path) const;
	Q_INVOKABLE void openSkillPath(const QString& path);
	// 右侧栏：顶栏当开关用（toggle），面板自己的 ✕ 直接 close
	Q_INVOKABLE void togglePanelBar();
	Q_INVOKABLE void openPanelBar();
	Q_INVOKABLE void closePanelBar();
	// 当前是 app 内设置窗口（Settings(m_api)）；QML 侧等 M6 再做等价页面，这条通路先解决"配不了模型"
	Q_INVOKABLE void openModelSettings();
	// Markdown 展示：C++ 侧用 QTextDocument/md4c 解析成单文档，QML 的 MarkdownBody
	// 只负责把 TextEdit.textDocument 交过来（跨段落/表格的选择因此成立）。
	// 增量流式：稳定段落用 fragment 追加，只有末尾未闭合段落在变，避免每个 token 重排整篇。
	Q_INVOKABLE void markdownAttach(QObject* quickDocument, const QVariantMap& colors);
	Q_INVOKABLE void markdownUpdate(QObject* quickDocument, const QString& markdown);
	Q_INVOKABLE void markdownDetach(QObject* quickDocument);
	// QML 的 QTextDocument 不画 cell border；表格横向分隔线由 QML Canvas 覆盖层绘制。
	// 返回 [{ x, y, w }]（文档坐标）。
	Q_INVOKABLE QVariantList markdownTableLines(QObject* quickDocument);
	// QTextDocument 不支持 inline-code 的圆角/内边距；这里返回每个 inline-code 片段的文档矩形，
	// 由 MarkdownBody 在其背后画圆角底色（正文本身仍由 TextEdit 选中/换行）。
	Q_INVOKABLE QVariantList markdownInlineCodeRects(QObject* quickDocument);
	// CodeBlock 在主题切换后重新取一次高亮 HTML：消息模型里的 html 是装载时烘死的旧色，
	// 这里按当前主题现算（CodeHighlighter 内部有语言+代码+亮暗的缓存）。
	Q_INVOKABLE QString highlightCode(const QString& language, const QString& code) const;
	// QML 侧诊断出口：console.log 不落盘，诊断信息统一从这里走项目 Logger（qInfo → logs/general/info.log）
	Q_INVOKABLE void probe(const QString& tag, const QString& detail);

signals:
	void currentSessionChanged();
	void initializingChanged();
	void sessionLoadingChanged();
	void statusTextChanged();
	void serverStateChanged();
	void messageCountChanged();
	void traceSummaryChanged();
	void streamingChanged();
	void cancellingChanged();
	void lastTurnEmptyChanged();
	void statsLineChanged();
	void modelCatalogChanged();
	void themeSwitchingChanged();
	void toolsFilterOpenChanged();
	void skillsChanged();
	// 当前会话的权限预设变化（read-only / workspace-write / danger-full-access）
	void permissionPresetChanged();
	// 右侧栏开合（顶栏 ◫ / 面板 ✕）
	void panelBarOpenChanged();
	// 子代理页签（当前页签组、子代理目录、根标题）变化
	void peerTabsChanged();
	// 后台任务名册变化（整表替换）
	void jobsChanged();
	void interactionChanged();
	void answerSelectionsChanged();
	void olderChanged();
void pendingAttachmentsChanged();
void attachmentRevisionChanged();
	void olderPrepended();
	void sessionCreateFailed(const QString& message);  // 新建会话失败（QML 用 Toast 显示：statusText 只在初始化遮罩里可见）
	// 操作失败/被拒（删工作区、建工作区...）：以前这里是一条空连接，所有错误都被丢掉了 ——
	// 用户看到的就是"点了没反应"。现在转发到 QML，由侧栏弹 Toast。
	void operationFailed(const QString& message);

private:
	explicit QmlBridge(QObject* parent = nullptr);
	QmlBridge(const QmlBridge&) = delete;
	QmlBridge& operator=(const QmlBridge&) = delete;

	// 接线：把 core 与业务层的信号接到本类/QML 面（init 里调用；只做 connect，不做业务）
	void wireRuntime();
	void wireChat();

	// 当前会话 → 页签组根（父目录后到时把组从"子代理自己"纠正回真正的父会话）。
	// 只改 m_peerRootId，不发信号（调用方照旧 emit peerTabsChanged，避免同一次刷新发两遍）。
	void refreshPeerRoot();

	ClientRuntime* m_runtime = nullptr;
	int m_attachmentRevision = 0;
	ChatController* m_chat = nullptr;
	// 子代理活动 → 轻量重拉 running 的去抖计时器（wireChat 里起、refreshSessionStates 里用）
	QTimer* m_peerStateTimer = nullptr;	ThemeController* m_theme = nullptr;
	// 增量流式渲染状态：每个 MarkdownBody 的 TextEdit 文档一份。
	struct MarkdownDocumentState
	{
		QString text;
		QVariantMap colors;
		int stableOffset = 0;   // 已提交稳定段的 markdown 字符位置
		int stablePos = 0;      // 已提交内容在 QTextDocument 里的游标位置
	};
	QHash<QObject*, MarkdownDocumentState> m_markdownDocuments;

	ToolsFilter* m_toolsFilter = nullptr;
	SessionListModel* m_sessions = nullptr;
	SettingsController* m_settings = nullptr;
	PluginMarketController* m_plugins = nullptr;
	ExtensionManagerController* m_extensions = nullptr;
	CommandController* m_commands = nullptr;
	QString m_currentSessionId;   // 纯界面状态：当前会话（消息区/跟随流都按它走）
	QString m_permissionPreset;   // 当前会话权限预设；空 = 还没读到投影
	// 页签组的"主会话"（纯界面状态）：当前会话是某会话的子代理时，它指向父会话；否则就是当前会话。
	// 顶栏把父标题画成第一颗页签；**只有当前会话是子代理时**才画第二颗（当前子代理），
	// 这样"父 ↔ 子"可来回切；父会话本身只显示一颗标题。
	QString m_peerRootId;
	bool m_toolsFilterOpen = false;   // 纯界面状态：Agent 过滤器面板是否打开
	QVariantList m_skills;            // 技能管理页：{name, description, whenToUse, modelInvocable, path}
	bool m_skillsBusy = false;
	QString m_skillsStatus;
	bool m_panelBarOpen = false;  // 纯界面状态：右侧栏是否展开（不落盘，暂不跨会话记忆）
};
