#pragma once

// 无边框 QQuickWindow 的窗口级原生行为（Windows）：补回 WS_* 样式位、关 Win11 系统圆角、
// 客户区整块（WM_NCCALCSIZE）、边缘缩放热区与标题栏拖动区（WM_NCHITTEST）、最大化工作区溢出量。
// 返回 HTCAPTION/HTMINBUTTON/HTMAXBUTTON/HTCLOSE 让系统处理，才能拿到贴边吸附、双击标题栏最大化
// 与 Win11 Snap Layouts；startSystemMove/startSystemResize 仍作为兜底留在 WindowBridge 上。

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
	void applyNativeStyle(QWindow* window);

	// 自绘标题栏的拖动区（QML 的 TitleBar item），为空 = 没有拖动区。
	// 按 item 所属窗口生效：本类服务多扇窗（主窗口 + 右栏副窗口），登记项只影响自己那扇窗。
	void setCaptionItem(QQuickItem* item);
	// 窗口按钮：落在这些 item 上不交给系统当标题栏。kind 见 WindowControlKind
	void addWindowControl(QQuickItem* item, int kind);
	void removeWindowControl(QQuickItem* item);
	// 三个窗口按钮是否交给系统处理（true = 系统负责点击/悬停/Snap Layouts，QML 只画）。默认 false：
	// 系统路径下关闭要按很多次才生效（Qt 会插手非客户区按钮的鼠标捕获），QML 自己点才是确定的单击；
	// 代价是失去悬停最大化按钮的 Snap Layouts（拖标题栏到屏幕顶边仍能拿到）。
	void setSystemWindowButtons(bool enabled);
	bool systemWindowButtons() const { return m_systemWindowButtons; }

	// 悬停到哪个窗口按钮上（0 = 没有）。QML 靠它画悬停反馈 —— 交给系统处理的按钮收不到鼠标事件
	void setHoveredKindChanged(std::function<void(int)> callback);

	// 最大化时"系统多给的那一圈"（逻辑像素）：客户区要按它内缩，可见内容才落进工作区
	int maximizedContentInset(const QWindow* window) const;

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

	// 记名 + 缓存原生句柄。绝不为查句柄调 QWindow::winId()（会强制创建平台窗口；窗口关闭后
	// 反复重建会形成 CreateWindowEx 风暴，表现为"点关闭关不掉"）。只有 handle 非空时才读句柄。
	struct WindowEntry
	{
		QPointer<QWindow> window;
		quintptr hwnd = 0;
	};

	QWindow* findByHwnd(void* hwnd);
	// 命中该点的窗口按钮（没有则 nullptr）。只算**属于 window 这扇窗**的登记项
	const ControlEntry* windowControlAt(QWindow* window, const QPoint& logicalPos) const;
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
