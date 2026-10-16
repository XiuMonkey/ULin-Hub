#pragma once
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QMetaObject>
#include <QPointer>
#include <QStringList>
#include <qplugin.h>
#include <memory>

// VirtualCommon.h —— 宿主 → 插件方向唯一的 C++ 接口面：纯虚接口，无 .cpp，宿主与插件各编一份。
// 取对象：qobject_cast<VirtualTheme*>(DshHost::findObject(DshHostIndex::kThemeManager))
// 接口 → 宿主实现（登记名）：
//   VirtualTheme → ThemeManager (kThemeManager)          VirtualMain → DshApiClient (kMainWindow)
//   VirtualConnectionManager → ConnectionManager (kConnectionManager)
//   VirtualTranslation → TranslationNotifier (kTranslationNotifier)   VirtualUiHost → HostUi (kUiHost)
//   VirtualPanelHost → HostUi (kUiHost，与 VirtualUiHost 同一个单例 —— 不单列 index)
// 登记名见 HostIndex.h，取址入口见 HostExports.h，IID 见文件末尾。
// ⚠️ 跨模块只能 qobject_cast，不能 dynamic_cast（RTTI 不跨 DLL，恒 nullptr）；取接口与调用都只在 GUI 线程。
// ⚠️ ABI：虚方法只许末尾追加（不改名 / 不换签名 / 不插中间 / 不删），接口只能增（新类 + 新 IID）——
//    vtable 槽位 = 声明顺序，插中间会让旧插件调到错槽位，不报错、只在调用时崩。末尾追加的标了【末尾追加】。
// 扩展开发全貌见 assets/resources/docs/EXTENSION_DEV.zh-CN.md（界面注入 §11.3、ABI §11.5）。

class QWidget;   // 只出现在 VirtualTheme / VirtualMain 的历史签名里；QML 壳已无 QWidget

class VirtualTheme {
public:
	virtual ~VirtualTheme() = default;

	virtual void ExternalApplyToWindow(QWidget* window) = 0;       // 无操作桩：M8 起 QSS 已删，只为不破签名留着
	virtual bool ExternalReloadStyles() = 0;                       // 重读 <exe>/ui 的 theme-<mode>.json + metrics.json 装 QPalette；只重载调色板、不重挂窗口
	virtual bool ExternalIsDarkMode() = 0;                         // 【末尾追加】扩展自绘的设置界面显示"当前：浅色 / 深色"
	virtual void ExternalSwitchTheme(QWidget* currentWindow) = 0;  // 【末尾追加】只切模式、不重建窗口；传 null 安全（QML 壳无 QWidget 主窗口）
};

// VirtualMain —— 主窗口宿主接口：收扩展的顶层弹窗 + 收扩展灌入的会话 / 工作区数据。
// 实现类 = DshApiClient（登记名 kMainWindow 是历史名，别改）；12 个 Handle* 只是把参数 emit 成 DshApiClient 的同名信号。
// 正常路径由信号驱动，后端被接管后信号没人发，扩展才改调这里。必须在 GUI 线程同步调；方法名与签名即 ABI。
class VirtualMain {
public:
	virtual ~VirtualMain() = default;

	virtual void ExternalShowOverlay(QWidget* popup) = 0;  // 无操作桩：宿主未实现居中遮罩，扩展自建窗口照常显示、只是不吃宿主遮罩
	virtual void ExternalHideOverlay(QWidget* popup) = 0;  // 无操作桩：同上

	virtual void HandleConnected() = 0;
	virtual void ForwardMuxFrame(const QJsonObject& frame) = 0;
	virtual void HandleTransportError(const QString& context, const QString& message) = 0;
	virtual void HandleSessionSnapshot(const QString& sessionId, int cursor, const QJsonArray& records, bool hasMore) = 0;
	virtual void HandleSessionProjections(const QString& sessionId, int asOfSeq, const QJsonObject& values) = 0;
	virtual void HandleSessionControlBaseline(const QJsonObject& projectionsBySession) = 0;
	virtual void HandleSessionProjectionChanged(const QString& sessionId, const QString& key, const QJsonValue& value, int seq) = 0;
	virtual void HandleWorkspaceSnapshot(const QJsonArray& items, const QJsonArray& archivedSessionIds) = 0;
	virtual void HandleWorkspaceUpserted(const QJsonObject& workspace) = 0;
	virtual void HandleWorkspaceRemoved(const QString& workspaceId) = 0;
	virtual void HandleWorkspaceReordered(const QStringList& workspaceIds) = 0;
	virtual void HandleWorkspaceArchiveChanged(const QJsonArray& archivedSessionIds) = 0;

	virtual void ExternalRefreshSessionTitles() = 0;  // 无操作桩：刻意不接 —— session/list 会重建 SessionCatalog、切走用户正看的会话
};

// VirtualConnectionManager —— 信号槽登记表（插件经 kConnectionManager 取到后 qobject_cast）。
// 只有两个入口：接管走 TakeoverConnection（匿名，只凭 index），新增订阅走 ProtectedRegisterConnection（白名单）。
// 刻意没有 RegisterConnection —— 它要调用方自供 sender + signal，等于交出宿主整套信号面。
class VirtualConnectionManager {
public:
	struct ConnectionGroup {
		QPointer<QObject> Sender;   // 两端都用弱引用：对象先死就自动置空，不留野指针
		QPointer<QObject> Receiver;
		QByteArray Signal;
		QByteArray Slot;            // == "Lambda" 表示槽没有名字（lambda / 成员函数指针）⇒ 不能按名字重连
		QMetaObject::Connection Handle;
		bool HasHandle = false;     // Handle 当前是不是一条活连接
		QString TakenOverBy;        // 接管者记账名；卸载扩展时 ReleaseTakeoversFor 靠它整批交还
		std::shared_ptr<ConnectionGroup> Prev;   // 被接管前的整行配方，链式栈：接管几层就能回卷几层；槽是 "Lambda" 时不建底 ⇒ nullptr
	};   // ⚠️ 别用聚合初始化 {a,b,c,d}：按成员位置对位，插字段就整体错位，一律具名赋值

	virtual ~VirtualConnectionManager() = default;

	virtual void PublicRemoveConnection(QString index) = 0;
	virtual void SuspendConnection(QString index) = 0;
	virtual void TakeoverConnection(QString index, QObject* receiver, QByteArray slot) = 0;
	virtual void Reconnect(QString index) = 0;
	// 新增订阅宿主公开信号：signal 是原生签名串（"2clearRequested()"），不在白名单会被拒
	virtual void ProtectedRegisterConnection(QString index, const QObject* sender, QByteArray signal, const QObject* receiver, const char* slot) = 0;
	// 【末尾追加】交还接管：还原成接管前的样子（QML 登记项解闸、宿主内部连接只回卷一层）；没有接管者 / 没这条连接 ⇒ false
	virtual bool ReleaseTakeover(QString index) = 0;
};

// VirtualTranslation —— 多语言宿主接口：让扩展自绘的设置界面也能读 / 改语言，与设置窗口「外观」页的语言下拉框同一套实现。
// 实现类 = TranslationNotifier 单例（登记名 kTranslationNotifier）。
class VirtualTranslation {
public:
	virtual ~VirtualTranslation() = default;

	virtual QString ExternalLanguage() = 0;                         // 当前语言代码；空串 = 跟随系统（与设置里存的值同义）
	virtual QStringList ExternalLanguageCodes() = 0;                // 可选语言代码（不含"跟随系统"，它在界面里是单独一项）
	virtual QString ExternalLanguageName(const QString& code) = 0;  // 语言代码 → 展示名（该语言自己的写法，如 "English" / "日本語"）；未知代码回空串
	virtual bool ExternalSetLanguage(const QString& code) = 0;      // 立即切换语言（免重启）并写进设置；⚠️ 缺 .qm 时退回源语言并返回 false，调用方应显示给用户
};

// VirtualUiHost —— 扩展 UI 宿主接口（QML 时代）：把扩展自己的 QML 注入到宿主界面的指定对象下面。
// 沿用旧控件壳的直觉 —— 那时是 new QToolButton(hostObject) + layout->addWidget，现在换成
// qobject_cast<QQuickItem*>(DshHost::findObject("topBar")) 之后 setParentItem(host)。实现类 = HostUi 单例（kUiHost）。
// 可挂名字（稳定，加名字跟版本一起发）：shell / titleBar / sidebar / composer 是自由 Item，位置由扩展 QML 自己写；
// topBar / sidebar.extDock 根是 Row（位置器），注入进去自动排末尾，不用算 x/y。
class VirtualUiHost {
public:
	virtual ~VirtualUiHost() = default;

	virtual QObject* HostItem(const char* name) = 0;         // 按名字取宿主对象；没有则 nullptr。只能在主线程调
	virtual QStringList HostItemNames() = 0;                 // 现在有哪些可挂的名字（自省 / 诊断）

	// owner = 扩展安装目录名，按 owner 整批卸载；返回句柄（供 UnmountOwner），失败回空串。
	// ⚠️ 装载期（attachHost 里）调必失败 —— 名字是 QML 根在 Component.onCompleted 登记的 ⇒ 延后到 QTimer::singleShot(0)。
	virtual QString MountInto(const char* owner, const char* hostItem, const char* qmlUrl, QObject* context) = 0;
	// ⚠️ 按 owner 整批删：同 owner 既挂入口又挂面板会一起被摘（用后缀分开，如 owner + "/panel"）；运行期"移除扩展"宿主不代劳，须在自己 detachHost() 里摘。
	virtual int UnmountOwner(const char* owner) = 0;
};

// VirtualPanelHost —— 让扩展**顶替**面板坞某个块的**后端**（当前只落地终端块）。
// 与 VirtualUiHost 的分工：UiHost 是"往宿主对象下面**追加**自己的 QML"；本接口是"把某个块背后
// 干活的那个对象换成扩展的" —— **界面仍是宿主的**：终端块照旧由宿主 TerminalPanel + WebEngine +
// xterm.js 渲染（底色跟主题走），扩展只提供喂给页面的传输对象，**不用自带 QML**。
// 实现类 = 同一个 HostUi 单例（登记名 kUiHost）：登记存在它身上，TerminalPanel 建 QWebChannel 时
// 取出来注册给页面；页面（宿主 qrc）对后端的调用契约见 EXTENSION_DEV §11.7。
// 没登记时一切照旧（终端走内置 TerminalBridge + 服务端引擎）。
class VirtualPanelHost {
public:
	virtual ~VirtualPanelHost() = default;

	// 登记顶替：kind = 块的种类（当前只有 "terminal" 落地；browser / files 预留）；
	// backend = 扩展提供的后端对象，必须自带 objectName `ulinterm`，且实现宿主页面认的槽/信号
	// （start / pageReady / pageGone / write / resize / ack / log / pageHello / closeBlock +
	// frameReady / statusChanged）。宿主把它**原样**注册到该块的 QWebChannel 上，页面直连它。
	// 传 nullptr 等于撤销。owner = 扩展安装目录名：宿主按 kind 只记一份，后登记者取得该 kind。
	// ⚠️ 只在 GUI 线程调，且要赶在**该块的面板被创建之前**（即 attachHost() 里）：面板只在创建时
	//    取一次后端，晚了它已经接上内置桥。
	virtual bool SetPanelBackend(const QString& owner, const QString& kind, QObject* backend) = 0;
	// 撤销：仅当该 kind 当前登记的 owner 就是调用方时才撤（免得卸 A 时把 B 顶上去的登记清掉）；
	// 撤掉后该 kind 恢复内置后端。
	virtual void ClearPanelBackend(const QString& owner, const QString& kind) = 0;
	// 该 kind 当前是否已被顶替（扩展自省 / 宿主界面提示用）
	virtual bool HasPanelBackend(const QString& kind) = 0;
};

Q_DECLARE_INTERFACE(VirtualMain, "com.ulinhub.VirtualWindow/1.0")   // IID 里的 VirtualWindow 是历史名，别跟着类名改
Q_DECLARE_INTERFACE(VirtualConnectionManager, "com.ulinhub.VirtualConnectionManager/1.0")
Q_DECLARE_INTERFACE(VirtualTheme, "com.ulinhub.VirtualTheme/1.0")
Q_DECLARE_INTERFACE(VirtualTranslation, "com.ulinhub.VirtualTranslation/1.0")
Q_DECLARE_INTERFACE(VirtualUiHost, "com.ulinhub.VirtualUiHost/1.0")
Q_DECLARE_INTERFACE(VirtualPanelHost, "com.ulinhub.VirtualPanelHost/1.0")

// 已删接口（旧文档 / 旧插件可能还在引用，不要照抄）：VirtualTopBar、VirtualSidebar、VirtualShell、VirtualMessageHost ——
// 实现类均随 M8 删 src/ui 一起没了，findObject 必得空；kTopBar / kSidebar 后来以"宿主 QML 对象登记名"的含义恢复。
