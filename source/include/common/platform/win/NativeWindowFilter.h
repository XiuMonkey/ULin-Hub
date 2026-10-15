#pragma once

// 无边框 QQuickWindow 的"窗口级原生行为"（Windows）：补回 WS_* 样式位 + 关 Win11 系统圆角 + 客户区整块
// （WM_NCCALCSIZE）+ 边缘缩放热区与标题栏拖动区（WM_NCHITTEST）+ 最大化时的工作区溢出量。
//
// 为什么不用 QWindow::startSystemMove()/startSystemResize()：本类返回 HTCAPTION/HTMINBUTTON/
// HTMAXBUTTON/HTCLOSE 让系统自己处理，才拿得到 Windows 的贴边吸附、双击标题栏最大化、
// 以及 Win11 悬停最大化按钮弹出的 Snap Layouts —— 这三件事自绘拖动都换不来。
// startSystemMove/startSystemResize 仍作为兜底保留在 WindowBridge 上（扩展自绘窗口条时用）。
//
// 旧控件壳那套 common/appearance/WindowFrame（绑 QWidget）已于 2026-10-01 随 M8 清理删除，本类绑 QWindow（QML 壳用）。

#include <QAbstractNativeEventFilter>
#include <QList>
#include <QPointer>
#include <QRectF>
#include <functional>

class QPoint;
class QQuickItem;
class QWindow;

class NativeWindowFilter final : public QAbstractNativeEventFilter
{
public:
	// 程序级唯一实例；刻意不析构（它被 QCoreApplication 持有到退出）
	static NativeWindowFilter& instance();

	// 注册成应用级原生事件过滤器（幂等）。必须在任何窗口创建之前调用 —— Qt 的平台插件会把
	// 消息先交给已安装的过滤器，装晚了就赶不上窗口创建期的 WM_NCCALCSIZE。
	void install();

	// 根窗口创建后调一次：补回 WS_THICKFRAME/WS_CAPTION 等样式位、关掉 Win11 系统圆角（幂等）。
	// 返回值无意义，仅为调用方便。
	void applyNativeStyle(QWindow* window);

	// 自绘标题栏：窗口内的拖动区（QML 的 TitleBar item）。为空 = 没有拖动区
	void setCaptionItem(QQuickItem* item);
	// 窗口按钮：落在这些 item 上不交给系统当标题栏。kind 见 WindowControlKind
	void addWindowControl(QQuickItem* item, int kind);
	void removeWindowControl(QQuickItem* item);
	// 三个窗口按钮是否交给系统处理（true = 系统负责点击/悬停/Snap Layouts，QML 只画）。
	// ⚠️ 默认 **false**：实测交给系统时关闭要按下很多次才生效 —— Windows 的非客户区按钮要在
	// "按下→移动→抬起"的模态循环里由 DefWindowProc 判定，而 Qt 会在这条路径上插手鼠标捕获，
	// 结果时中时不中。改为 QML 自己点（返回 HTCLIENT）后是确定性的单击生效；
	// 代价是失去"悬停最大化按钮弹 Snap Layouts"这个入口（拖标题栏到屏幕顶边仍能拿到 Snap Layouts）。
	void setSystemWindowButtons(bool enabled);
	bool systemWindowButtons() const { return m_systemWindowButtons; }

	// 悬停到哪个窗口按钮上（0 = 没有）。QML 靠它画悬停反馈 —— 交给系统处理的按钮收不到鼠标事件
	void setHoveredKindChanged(std::function<void(int)> callback);

	// 最大化时"系统多给的那一圈"（逻辑像素）：客户区要按它内缩，可见内容才落进工作区
	int maximizedContentInset(const QWindow* window) const;

	// ---- 拖动跟手用的两个**纯查询**（不碰任何窗口、不改任何状态）----
	// 场景：拖右栏那三个点时，内容已经被 reparent 到另一扇窗里（"同一份实例"是硬要求），
	// 而鼠标捕获留在按下时那扇窗（Windows 的捕获是线程级、消息只投给捕获窗口）⇒ 搬出去的那扇窗
	// 收不到任何鼠标消息。于是"跟到哪儿了 / 松没松手"只能直接问系统，不能靠事件推。
	// 光标屏幕坐标（逻辑像素，与 QML 的 mapToGlobal / 窗口 x,y 同一坐标系）
	static QPoint cursorScreenPos();
	// 左键是否**物理**按着（GetAsyncKeyState：与"谁收到 WM_LBUTTONUP"无关）
	static bool leftButtonDown();

	bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) override;

	// WindowControlKind 的取值（QML 侧用字符串，见 WindowBridge::addWindowControl）
	enum WindowControlKind
	{
		ControlOther = 0,
		ControlMinimize = 1,
		ControlMaximize = 2,
		ControlClose = 3
	};

private:
	NativeWindowFilter() = default;
	NativeWindowFilter(const NativeWindowFilter&) = delete;
	NativeWindowFilter& operator=(const NativeWindowFilter&) = delete;

	struct ControlEntry
	{
		QPointer<QQuickItem> item;
		int kind = ControlOther;
	};

	// 记名 + 缓存原生句柄。
	// ⚠️ **绝不为了查句柄去调 QWindow::winId()**：它会强制创建平台窗口。窗口被关闭后平台窗口已销毁，
	// 若此时还有人调 winId()，Qt 就会重建它 —— 重建失败再重试，形成 CreateWindowEx 风暴，
	// 表现就是"点关闭关不掉、要按很多次"。所以：只有 QWindow::handle() 非空（平台窗口已在）时才读句柄。
	struct WindowEntry
	{
		QPointer<QWindow> window;
		quintptr hwnd = 0;
	};

	QWindow* findByHwnd(void* hwnd);
	// 命中该点的窗口按钮（没有则 nullptr）
	const ControlEntry* windowControlAt(const QPoint& logicalPos) const;
	// 返回 WM_NCHITTEST 的命中码；controlKind 回填命中的窗口按钮角色
	qintptr hitTest(QWindow* window, const QPoint& logicalPos, int* controlKind) const;
	void updateHoveredKind(int kind);

	QList<WindowEntry> m_windows;
	QList<ControlEntry> m_windowControls;

	QPointer<QQuickItem> m_captionItem;
	std::function<void(int)> m_hoveredKindChanged;
	int m_hoveredKind = 0;
	bool m_installed = false;
	bool m_systemWindowButtons = false;
};
