#pragma once

// 宿主登记名（DshHostIndex）的中性词汇表：宿主登记与插件查询必须用同一份。
// 单独成文件是为了分层 —— network / common/appearance 这些下层也要用它，若留在
// ExtensionSystem/HostExports.h 里，下层就会反向依赖 ExtensionSystem。
// 纯字符串无编译期检查，拼错只会静默拿到空对象（与"没登记"无从区分）。
namespace DshHostIndex
{
	inline constexpr const char* kMainWindow = "mainWindow";
	// 左/顶栏与消息区的宿主 QML 对象登记名：由 QML 根在 Component.onCompleted 里调
	// HostUi.registerItem(名字, this) 登记；topBar 的根是 Row（注入即排末尾），其余位置由扩展自己 anchors。
	inline constexpr const char* kShell = "shell";
	inline constexpr const char* kTitleBar = "titleBar";
	inline constexpr const char* kTopBar = "topBar";
	inline constexpr const char* kSessionBar = "sessionBar";
	inline constexpr const char* kComposer = "composer";
	// Composer 的命令 UI 扩展位：commandButton 是 Row（追加按钮），commandPanel 是自由 Item（注入 Popup/面板）。
	inline constexpr const char* kComposerCommandButton = "composer.commandButton";
	inline constexpr const char* kComposerCommandPanel = "composer.commandPanel";
	// 消息区 / 设置页 / 右栏根：与上面同级的 QML 宿主对象，扩展可直接注入。
	inline constexpr const char* kMessageList = "messageList";
	inline constexpr const char* kSettingsPage = "settingsPage";
	inline constexpr const char* kPanelBar = "panelBar";
	// 扩展 UI 宿主单例（HostUi）：转成 VirtualUiHost* 拿上面这些对象、挂载/卸载扩展自己的 QML；
	// 同一个单例也实现 VirtualBlockBackendHost*（顶替面板坞某个块的后端），所以不再单列 index。
	inline constexpr const char* kUiHost = "uiHost";
	// 样式表管理单例（ThemeManager），init 末尾登记；插件转成 VirtualTheme* 调
	// ExternalApplyToWindow / ExternalReloadStyles / ExternalIsDarkMode / ExternalSwitchTheme（切换免重启）。
	inline constexpr const char* kThemeManager = "themeManager";
	// 多语言单例（TranslationNotifier），Translation::init 里登记；插件转成 VirtualTranslation* 读/改当前语言。
	inline constexpr const char* kTranslationNotifier = "translationNotifier";
	// 信号槽登记表单例（ConnectionManager）：插件只能匿名接管或订阅白名单内信号，宿主的 RegisterConnection 不在其上。
	inline constexpr const char* kConnectionManager = "connectionManager";
	// DSH API 客户端（DshApiClient），随 ClientRuntime 创建、与进程同寿；插件转成 VirtualApiHost* 调
	// Takenover / CompleteCall / FailCall。每次 attachHost 仍建议重新取（旧 QPointer 只会变空，不会变野）。
	inline constexpr const char* kApiClient = "apiClient";
	inline constexpr const char* kApiSink = "apiSink";
	// 右栏面板坞的块模型本体（PanelBarDockModel；QML 里的 `PanelBarDock` 是同一实例）：登记一次即可调它的
	// 全部 Q_INVOKABLE（open / openUrl / closeBlock / blockIds / close / activate / showHome）并直接 connect 其信号；
	// 与 VirtualBlockBackendHost 的 OpenBlockUrl / CloseBlock 并存，那条适合只要稳定 ABI 的扩展。
	inline constexpr const char* kPanelBarDock = "panelBarDock";

	// 业务对象：与上面同级的单例；顺序固定，新增只追加，扩展侧按同一份 HostIndex 查询。
	inline constexpr const char* kSessionList = "sessionList";
	inline constexpr const char* kSettingsController = "settingsController";
	inline constexpr const char* kChatController = "chatController";
	inline constexpr const char* kCommandController = "commandController";
	// ULinCore 执行器注册宿主（UlinCoreMasterAgent）。
	inline constexpr const char* kUlinCoreExecutorHost = "ulincoreExecutorHost";
	inline constexpr const char* kPluginMarket = "pluginMarket";
	inline constexpr const char* kFileBrowser = "fileBrowser";
	inline constexpr const char* kExtensionManager = "extensionManager";
}
