#pragma once
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QMetaObject>
#include <QPointer>
#include <QStringList>
#include <qplugin.h>
#include <memory>

// 参数只按指针传递，前向声明就够 —— 宿主与插件各编一份，互不依赖对方 exe 里的符号
class QWidget;

// ⚠️ 所有虚方法只许在**末尾追加**：vtable 槽位 = 声明顺序，插件 DLL 独立编译，插中间会让旧插件调错位置。
// (2026-10-01) 已删的接口：VirtualTopBar / VirtualSidebar（实现类随 M8 删 src/ui 一起没了，kTopBar / kSidebar
// 从未 AddToRegistry ⇒ 插件 findObject 必得空）、VirtualShell（架空原 UI，实现类 DSHHub 与 UiStage 都随 M8 删除，
// 且它全程按 QWidget / QMainWindow 设计，现在的宿主是 QQmlApplicationEngine + QML Window，结构上不可能再成立）、
// VirtualMessageHost（收放"正在载入会话"提示，实现类 MessageHost 已随 M8 删除，kMessageHost 也从没登记）。
// 要往侧栏 / 顶栏挂 UI，走 QML 时代的 VirtualUiHost（见 docs/style-pack.md）。

class VirtualTheme {
public:
	virtual ~VirtualTheme() = default;

	virtual void ExternalApplyToWindow(QWidget* window) = 0;
	virtual bool ExternalReloadStyles() = 0;  // 重读 exe 同目录 styles/*.qss 与 theme-*.json 并挂回所有顶层窗口
	// 【末尾追加】扩展自绘的设置界面要显示"当前：浅色/深色"
	virtual bool ExternalIsDarkMode() = 0;
	// 【末尾追加】(2026-10-01 QML 壳) 只切模式、**不重建窗口**（见 ThemeManager::ExternalSwitchTheme），
	// currentWindow 传 null 也安全 —— QML 壳里没有 QWidget 主窗口；旧壳"销毁并重建主窗口 ⇒ 扩展挂在旧窗口上的
	// 控件 / 弹窗随之消失"那条副作用已不成立。
	virtual void ExternalSwitchTheme(QWidget* currentWindow) = 0;
};

// 主窗口宿主接口：接收扩展的顶层窗口当宿主弹窗，以及扩展灌入的会话 / 工作区数据。
// (2026-10-01 QML 壳) **不再由"主窗口"实现** —— 实现类是 DshApiClient（登记名仍是 kMainWindow，为了不改扩展侧的
// ABI / 字符串契约）：那 13 个入站注入方法只是把参数转成 DshApiClient 的同名信号，而 QML 壳已把这些信号接到真处理器
// （ShellController.cpp 的接线区、ChatController / SessionCatalog / 状态行）。两个 QWidget* 成员
// （ExternalShow/HideOverlay）在 QML 壳里没有对应物，先留无操作桩（迁移计划 D1）。
class VirtualMain {
public:
	virtual ~VirtualMain() = default;

	// 铺遮罩 + 居中显示，两步背靠背；调用前 popup 尺寸必须已定好
	virtual void ExternalShowOverlay(QWidget* popup) = 0;
	// 收遮罩，须与 ExternalShowOverlay 成对：遮罩只留一层，最后一个 release 才真隐藏
	virtual void ExternalHideOverlay(QWidget* popup) = 0;

	// 入站数据注入（后端接管专用）：12 个方法是 DshApiClient 同名信号的替代入口 —— 正常路径由信号驱动，接管后
	// 信号没人发，扩展改调这里。**必须在 GUI 线程同步调用**；方法名与签名一发布就是 ABI。

	// 连接与传输：让 UI 认为已连接 / 一帧 mux 消息（会话事件、审批、提问，入站主力）/ 传输层错误
	virtual void HandleConnected() = 0;
	virtual void ForwardMuxFrame(const QJsonObject& frame) = 0;
	virtual void HandleTransportError(const QString& context, const QString& message) = 0;
	// 会话：follow 快照（首屏历史 + 游标）/ 快照自带的投影（小灰字）/ control baseline / 实时投影帧
	virtual void HandleSessionSnapshot(const QString& sessionId, int cursor, const QJsonArray& records, bool hasMore) = 0;
	virtual void HandleSessionProjections(const QString& sessionId, int asOfSeq, const QJsonObject& values) = 0;
	virtual void HandleSessionControlBaseline(const QJsonObject& projectionsBySession) = 0;
	virtual void HandleSessionProjectionChanged(const QString& sessionId, const QString& key, const QJsonValue& value, int seq) = 0;
	// 工作区：baseline（清单 + 归档集合）与四个增量帧；order / archived 都是整体替换
	virtual void HandleWorkspaceSnapshot(const QJsonArray& items, const QJsonArray& archivedSessionIds) = 0;
	virtual void HandleWorkspaceUpserted(const QJsonObject& workspace) = 0;
	virtual void HandleWorkspaceRemoved(const QString& workspaceId) = 0;
	virtual void HandleWorkspaceReordered(const QStringList& workspaceIds) = 0;
	virtual void HandleWorkspaceArchiveChanged(const QJsonArray& archivedSessionIds) = 0;

	// 重拉侧栏会话标题但**不重选会话**：refreshSessions 拿到列表后会 emit initialSessionReady（自动选中的是
	// "第一个非 running 的会话"），接管态下再走那条会把用户正在看的会话切走、还会打断流式输出。幂等、可重复调。
	virtual void ExternalRefreshSessionTitles() = 0;
};

// 信号槽登记表（插件经 kConnectionManager 取到后 qobject_cast）。接管走 TakeoverConnection（匿名，只凭 index）；
// 新增订阅走 ProtectedRegisterConnection（白名单）；**刻意不上 RegisterConnection** —— 它要求调用方自供
// sender + signal，等于交出宿主全部信号面。
class VirtualConnectionManager {
public:
	// 一条连接的全部状态。⚠️ 别用聚合初始化 `{a,b,c,d}` 构造（按成员位置对位，插字段就整体错位；而"指针 → bool"
	// 是合法隐式转换 ⇒ 可能编译通过但语义全错）—— 一律具名赋值。
	struct ConnectionGroup {
		QPointer<QObject> Sender;   // 两端都用弱引用：对象先死就自动置空，不留野指针
		QPointer<QObject> Receiver;
		QByteArray Signal;
		QByteArray Slot;            // == "Lambda" 表示槽没有名字（lambda / 成员函数指针）⇒ 不能按名字重连
		QMetaObject::Connection Handle;
		bool HasHandle = false;     // Handle 当前是不是一条活连接
		QString TakenOverBy;        // 接管者记账名；卸载扩展时 ReleaseTakeoversFor 靠它整批交还

		// 被接管前的**整行**配方，链式栈：被接管几次就能逐层回卷几次（每层是堆上的一份拷贝，它自己的 Prev
		// 指向更早那份）。用 shared_ptr 是为了"删行/覆盖时整链自动释放"，且按值拷贝不会双删。
		// 槽是 "Lambda" 时不建底（没名字、重连不回来）⇒ Prev == nullptr。
		std::shared_ptr<ConnectionGroup> Prev;
	};

	virtual ~VirtualConnectionManager() = default;

	virtual void PublicRemoveConnection(QString index) = 0;
	virtual void SuspendConnection(QString index) = 0;
	virtual void TakeoverConnection(QString index, QObject* receiver, QByteArray slot) = 0;
	virtual void Reconnect(QString index) = 0;
	// 新增订阅宿主公开信号：signal 是原生签名串（"2clearRequested()"），不在白名单会被宿主拒绝
	virtual void ProtectedRegisterConnection(QString index, const QObject* sender, QByteArray signal, const QObject* receiver, const char* slot) = 0;
	// 交还接管（2026-10-01 末尾追加）：把 index 还原成接管前的样子 —— QML 登记项解开闸门并断开替接管方接的那条
	// 连接；宿主内部连接还原接管前的接收者 / 槽位（只回卷一层）。没有接管者 / 没有这条连接 ⇒ false 并写日志。
	virtual bool ReleaseTakeover(QString index) = 0;
};

// 多语言宿主接口：让扩展自绘的设置界面也能读 / 改语言 —— 与宿主设置窗口「外观」页那个语言下拉框走同一套实现
// （Translation 命名空间的自由函数），所以行为一致：换 translator → Qt 给所有控件发 LanguageChange（接了
// changeEvent 的自己刷新）→ 剩下的固定文案按快照就地换 → 再通知非控件对象。
// 实现类 = TranslationNotifier 单例（登记名 kTranslationNotifier，见 DshHostIndex）。
class VirtualTranslation {
public:
	virtual ~VirtualTranslation() = default;

	virtual QString ExternalLanguage() = 0;                         // 当前语言代码；**空串 = 跟随系统**（与设置里存的值同义）
	virtual QStringList ExternalLanguageCodes() = 0;                // 可选语言代码（不含"跟随系统"，它在界面里是单独一项）
	virtual QString ExternalLanguageName(const QString& code) = 0;  // 语言代码 → 展示名（该语言自己的写法，如 "English" / "日本語"）；未知代码回空串
	// 立即切换语言（免重启）并写进设置；返回是否真的换了。
	// ⚠️ 缺 .qm 时会退回源语言并返回 false —— 调用方应把这件事**显示给用户**（不是弹错误框）。
	virtual bool ExternalSetLanguage(const QString& code) = 0;
};

// 扩展 UI 宿主接口（QML 时代）：让客户端扩展把**自己的 QML** 注入到宿主界面的指定对象下面。沿用旧控件壳的做法
// （那时是 findObject(kTopBar) → new QToolButton(hostObject) → layout->addWidget），只把宿主对象从 QWidget 换成
// QQuickItem —— QML item 本身就是 QObject：qobject_cast<QQuickItem*>(DshHost::findObject("topBar")) 之后
// setParentItem(host)，等价于 addWidget。
// 名字契约（稳定；加名字要跟版本一起发）：shell / titleBar / topBar / sidebar / composer —— topBar 的根是 Row
// （位置器）⇒ 注入进去会自动排在末尾；其余四个是自由 Item ⇒ 位置由扩展自己的 QML 写 anchors / x,y。
// 实现类 = HostUi 单例（登记名 kUiHost；同时是 QML 单例 HostUi）。
class VirtualUiHost {
public:
	virtual ~VirtualUiHost() = default;

	// 按名字取宿主对象；没有这个名字返回 nullptr。只能在主线程调（注册表不加锁，装的是 UI 对象）
	virtual QObject* HostItem(const char* name) = 0;
	// 现在有哪些可挂的名字（自省 / 诊断）
	virtual QStringList HostItemNames() = 0;
	// 载入扩展自己的 QML → 把 context 注入成它的属性（QML 侧写 required property var context）→
	// setParentItem 到宿主那一项下面。返回句柄（卸载用）；失败返回空串并写日志。owner = 扩展名（按 owner 整批卸载）。
	virtual QString MountInto(const char* owner, const char* hostItem, const char* qmlUrl, QObject* context) = 0;
	// 摘掉某个扩展注入的全部 item；返回摘掉的数量
	virtual int UnmountOwner(const char* owner) = 0;
};

Q_DECLARE_INTERFACE(VirtualMain, "com.DSH_HUB.VirtualWindow/1.0")
Q_DECLARE_INTERFACE(VirtualConnectionManager, "com.DSH_HUB.VirtualConnectionManager/1.0")
Q_DECLARE_INTERFACE(VirtualTheme, "com.DSH_HUB.VirtualTheme/1.0")
Q_DECLARE_INTERFACE(VirtualTranslation, "com.DSH_HUB.VirtualTranslation/1.0")
Q_DECLARE_INTERFACE(VirtualUiHost, "com.DSH_HUB.VirtualUiHost/1.0")
