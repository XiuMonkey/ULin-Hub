#pragma once

// 宿主登记名（DshHostIndex）的**中性词汇表**：宿主侧"要登记自己的类"与插件侧"查询"必须用同一份。
// 单独成文件是为了分层 —— network / common/appearance 这些**下层**也要用它，若留在
// ExtensionSystem/HostExports.h 里，下层就会被迫反向依赖 ExtensionSystem（见那边的"取址"半）。

// index 常量：宿主登记与插件查询必须用同一份 —— 纯字符串无编译期检查，拼错只会静默拿到空对象
// （与"没登记""不在主线程"无从区分）。
namespace DshHostIndex
{
	inline constexpr const char* kMainWindow = "mainWindow";
	// 2026-10-01 删：kSidebar / kTopBar / kMessageHost（当时的实现类是旧控件壳的 QWidget，已随 src/ui 删除）。
	// 2026-10-01 **恢复 + 扩容**：QML 时代的做法确定为"不做槽位，拿到宿主对象直接注入"
	// （见 VirtualCommon.h 的 VirtualUiHost）—— 所以这几个名字回来了，含义变成**宿主 QML 对象的登记名**，
	// 由对应的 QML 根在 Component.onCompleted 里用 HostUi.registerItem(名字, this) 登记：
	//   topBar 的根是 Row（位置器）⇒ 扩展注入进去自动排在末尾；其余是自由 Item ⇒ 位置由扩展自己写 anchors。
	// 这些是**稳定契约**（字符串无编译期检查，拼错只会静默拿到空对象）；加名字要跟版本一起发。
	inline constexpr const char* kShell = "shell";
	inline constexpr const char* kTitleBar = "titleBar";
	inline constexpr const char* kTopBar = "topBar";
	inline constexpr const char* kSidebar = "sidebar";
	inline constexpr const char* kComposer = "composer";
	// 扩展 UI 宿主单例（HostUi）：转成 VirtualUiHost* 拿上面这些对象、挂载/卸载扩展自己的 QML。
	inline constexpr const char* kUiHost = "uiHost";
	// 样式表管理单例（ThemeManager），init() 末尾登记；插件转成 VirtualTheme* 调
	// ExternalApplyToWindow / ExternalReloadStyles，以及末尾追加的 ExternalIsDarkMode /
	// ExternalSwitchTheme（后者与侧栏主题按钮同一条路：只切模式 + token 热重载，不重建窗口）。
	inline constexpr const char* kThemeManager = "themeManager";
	// 多语言单例（TranslationNotifier），Translation::init() 里登记；插件转成 VirtualTranslation*
	// 读/改当前语言（与宿主设置窗口「外观」页那个语言下拉框同一套实现，切换免重启）。
	inline constexpr const char* kTranslationNotifier = "translationNotifier";
	// 信号槽登记表单例（ConnectionManager），启动期登记；插件只能匿名接管（TakeoverConnection）
	// 或订阅白名单内信号（ProtectedRegisterConnection），宿主内部的 RegisterConnection 不在其上。
	inline constexpr const char* kConnectionManager = "connectionManager";
	// DSH API 客户端（DshApiClient），随 ClientRuntime 创建、与进程同寿（切主题不重建窗口 ⇒ 不换对象）；
	// 后端接管的宿主侧接口就在它上面 —— 插件转成 VirtualClass/VirtualApiTakeover.h 的 VirtualApiHost* 调
	// Takenover / CompleteCall / FailCall。每次 attachHost() 里仍建议重新取（旧 QPointer 只会变空，不会变野）。
	inline constexpr const char* kApiClient = "apiClient";
	inline constexpr const char* kApiSink = "apiSink";
	// 消息区宿主（MessageHost）那条 index 已于 2026-10-01 删除：MessageHost 是旧控件壳的类，
	// M8 已随 src/ui 一起删除，index 也从没登记过。
}
