// 见 NativeWindowFilter.h：QML 壳的无边框窗口原生行为（旧控件壳的 WindowFrame.cpp 已于 2026-10-01 删除）。

#include "common/platform/win/NativeWindowFilter.h"

#include <QCoreApplication>
#include <QCursor>
#include <QDebug>
#include <QGuiApplication>
#include <QPoint>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSizeF>
#include <QWindow>

#ifdef Q_OS_WIN
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#  include <dwmapi.h>
#  pragma comment(lib, "dwmapi.lib")

// 老 SDK 的 dwmapi.h 缺这两个常量
#  ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#    define DWMWA_WINDOW_CORNER_PREFERENCE 33
#  endif
#  ifndef DWMWCP_DONOTROUND
#    define DWMWCP_DONOTROUND 1
#  endif
#endif

namespace {
	constexpr int kResizeBorder = 5;

	// 读原生句柄：**只有平台窗口已经存在时才读**（QWindow::winId() 在没有平台窗口时会强制创建它 ——
	// 窗口关闭/销毁之后再调就变成"反复重建 → CreateWindowEx 失败"的风暴，表现为关不掉的窗口）。
	// QWindow::handle() 是公开 API，只做非创建式查询。
	quintptr safeHandle(const QWindow* window)
	{
		if (!window || !window->handle())
			return 0;
		return static_cast<quintptr>(window->winId());
	}
	// 贴屏幕边缘（最大化/全屏）：此时不留边缘缩放热区
	bool isEdgeToEdge(const QWindow* window)
	{
		if (!window)
			return false;
		const Qt::WindowStates states = window->windowStates();
		return states.testFlag(Qt::WindowMaximized) || states.testFlag(Qt::WindowFullScreen);
	}

	// 把 item 的几何换算到窗口客户区坐标。
	// mapToItem(nullptr, ...) = 映射到场景坐标，而场景坐标原点就是根 item（窗口 contentItem）的原点。
	QRectF itemRectInWindow(const QQuickItem* item)
	{
		if (!item || !item->isVisible())
			return QRectF();
		const QPointF origin = item->mapToItem(nullptr, QPointF(0.0, 0.0));
		return QRectF(origin, QSizeF(item->width(), item->height()));
	}
}

NativeWindowFilter& NativeWindowFilter::instance()
{
	static NativeWindowFilter filter;
	return filter;
}

void NativeWindowFilter::install()
{
	if (m_installed)
		return;

	if (QCoreApplication* app = QCoreApplication::instance()) {
		app->installNativeEventFilter(this);
		m_installed = true;
	}
}

void NativeWindowFilter::applyNativeStyle(QWindow* window)
{
	if (!window)
		return;

	WindowEntry* entry = nullptr;  // 记名（原生消息要靠句柄反查回 QWindow）并刷新句柄缓存
	for (WindowEntry& candidate : m_windows) {
		if (candidate.window == window) {
			entry = &candidate;
			break;
		}
	}
	if (!entry) {
		WindowEntry fresh;
		fresh.window = QPointer<QWindow>(window);
		m_windows.append(fresh);
		entry = &m_windows.last();
	}

	// 这里是**唯一**允许 winId() 强制创建平台窗口的地方：本函数的目的就是给已存在的窗口补样式位。
	// 其余任何地方查句柄都必须先看 handle()（见 findByHwnd / maximizedContentInset 的说明）。
	entry->hwnd = static_cast<quintptr>(window->winId());

#ifdef Q_OS_WIN
	HWND hwnd = reinterpret_cast<HWND>(entry->hwnd);
	if (!hwnd)
		return;

	// 一次性把 DPI/几何事实写进日志：命中测试、最大化补偿、QML 尺寸都建立在这些数上，
	// 出问题时不用再靠猜（Debug/Release 都打，量极小）
	{
		RECT windowRect{};
		RECT clientRect{};
		GetWindowRect(hwnd, &windowRect);
		GetClientRect(hwnd, &clientRect);
		qInfo().noquote() << QStringLiteral("[NativeWindow] size(logical)=%1x%2 dpr=%3 "
			"windowRect(physical)=%4x%5 clientRect(physical)=%6x%7")
			.arg(window->size().width()).arg(window->size().height())
			.arg(window->devicePixelRatio())
			.arg(windowRect.right - windowRect.left).arg(windowRect.bottom - windowRect.top)
			.arg(clientRect.right - clientRect.left).arg(clientRect.bottom - clientRect.top);
	}

	LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
	// 幂等：重复 SetWindowPos 会打断最大化动画
	if ((style & (WS_THICKFRAME | WS_CAPTION)) == (WS_THICKFRAME | WS_CAPTION))
		return;

	// FramelessWindowHint 会摘掉 WS_CAPTION/WS_THICKFRAME，这里补回来才能保留投影、贴边吸附、
	// 边缘缩放与系统窗口菜单（右键标题栏）
	style |= WS_THICKFRAME | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX;
	SetWindowLongPtrW(hwnd, GWL_STYLE, style);

	// Win11 默认的 8px 系统圆角会裁掉自绘 12px 描边的四角，故关掉只留投影
	const BOOL doNotRound = DWMWCP_DONOTROUND;
	DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &doNotRound, sizeof(doNotRound));

	SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
		SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
#endif
}

void NativeWindowFilter::setCaptionItem(QQuickItem* item)
{
	m_captionItem = item;
}

void NativeWindowFilter::addWindowControl(QQuickItem* item, int kind)
{
	if (!item)
		return;

	for (ControlEntry& entry : m_windowControls) {
		if (entry.item == item) {
			entry.kind = kind;  // 同 item 重复登记是覆盖语义（QML 可能重建 delegate）
			return;
		}
	}

	ControlEntry entry;
	entry.item = item;
	entry.kind = kind;
	m_windowControls.append(entry);
}

void NativeWindowFilter::removeWindowControl(QQuickItem* item)
{
	if (!item)
		return;

	for (int i = m_windowControls.size() - 1; i >= 0; --i) {
		if (m_windowControls.at(i).item == item)
			m_windowControls.removeAt(i);
	}
}

void NativeWindowFilter::setSystemWindowButtons(bool enabled)
{
	if (m_systemWindowButtons == enabled)
		return;

	m_systemWindowButtons = enabled;
	if (!enabled)
		updateHoveredKind(0);
}

void NativeWindowFilter::setHoveredKindChanged(std::function<void(int)> callback)
{
	m_hoveredKindChanged = std::move(callback);
}

int NativeWindowFilter::maximizedContentInset(const QWindow* window) const
{
	int inset = 0;
#ifdef Q_OS_WIN
	// 句柄一律走缓存：这个函数会被 QML 绑定反复求值（anchors.margins），绝不能在这里调 winId()
	// —— 那会在窗口关闭后又把它创建出来（见 safeHandle 的说明）
	quintptr cachedHandle = 0;
	for (const WindowEntry& entry : m_windows) {
		if (entry.window == window) {
			cachedHandle = entry.hwnd;
			break;
		}
	}

	// 最大化矩形比工作区多一圈不可见缩放边框；客户区不能再动、带 WS_MAXIMIZE 的窗口又没法用
	// SetWindowPos 挪，故在内容层补回来
	if (cachedHandle != 0 && window && window->windowStates().testFlag(Qt::WindowMaximized)
		&& !window->windowStates().testFlag(Qt::WindowFullScreen)) {
		if (HWND hwnd = reinterpret_cast<HWND>(cachedHandle)) {
			RECT windowRect{};
			MONITORINFO info{};
			info.cbSize = sizeof(info);
			if (GetWindowRect(hwnd, &windowRect)
				&& GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &info)) {
				const int overflow = qMin(qMin(info.rcWork.left - static_cast<int>(windowRect.left),
					info.rcWork.top - static_cast<int>(windowRect.top)),
					qMin(static_cast<int>(windowRect.right) - info.rcWork.right,
						static_cast<int>(windowRect.bottom) - info.rcWork.bottom));
				const qreal scale = window->devicePixelRatio() > 0.0 ? window->devicePixelRatio() : 1.0;
				inset = qMax(0, qRound(overflow / scale));
			}
		}
	}
#else
	Q_UNUSED(window);
#endif

	return inset;
}

// ---- 拖动跟手用的两个纯查询（见头文件）----
QPoint NativeWindowFilter::cursorScreenPos()
{
	// QCursor::pos() 直接问系统：不受 Qt Quick 的鼠标抓取、窗口激活、焦点影响。
	// 返回的是逻辑像素的虚拟桌面坐标，与 QML 的 mapToGlobal / 窗口 x,y 同一套坐标系。
	return QCursor::pos();
}

bool NativeWindowFilter::leftButtonDown()
{
#ifdef Q_OS_WIN
	// 物理按键状态：跟手期间 WM_LBUTTONUP 很可能投给的是**主窗口**（它握着捕获），
	// 而这次拖动要结束的是另一扇窗里的东西 —— 事件推不出来，只能问系统当前状态。
	if (GetAsyncKeyState(VK_LBUTTON) & 0x8000)
		return true;
#endif
	// 非 Windows（或取不到物理状态时）的兜底：Qt 自己维护的按键状态
	return QGuiApplication::mouseButtons().testFlag(Qt::LeftButton);
}

QWindow* NativeWindowFilter::findByHwnd(void* hwnd)
{
	if (!hwnd)
		return nullptr;

	const quintptr target = reinterpret_cast<quintptr>(hwnd);
	for (WindowEntry& entry : m_windows) {
		QWindow* window = entry.window.data();
		if (!window)
			continue;

		// 常规路径：只比缓存（零成本，且不会碰平台窗口）
		if (entry.hwnd == target)
			return window;

		// 缓存可能过期（平台窗口被重建，例如换屏/系统 DPI 变化）：刷新一次再比。
		// safeHandle 不会创建平台窗口，所以窗口正在销毁时这条路也只是把缓存清 0。
		const quintptr current = safeHandle(window);
		entry.hwnd = current;
		if (current == target)
			return window;
	}
	return nullptr;
}

const NativeWindowFilter::ControlEntry* NativeWindowFilter::windowControlAt(QWindow* window,
	const QPoint& logicalPos) const
{
	for (const ControlEntry& entry : m_windowControls) {
		if (!entry.item || !entry.item->isVisible())
			continue;
		// 登记项按"它实际在哪扇窗"归属：控件表只有一份，但同一份表服务多扇窗
		// （主窗口的自绘标题栏按钮 + 覆盖率页的抓手都在主窗口里；副窗口没有登记项）
		if (entry.item->window() != window)
			continue;
		if (itemRectInWindow(entry.item.data()).contains(logicalPos))
			return &entry;
	}
	return nullptr;
}

qintptr NativeWindowFilter::hitTest(QWindow* window, const QPoint& logicalPos, int* controlKind) const
{
	if (controlKind)
		*controlKind = ControlOther;

#ifdef Q_OS_WIN
	if (!window)
		return HTCLIENT;

	// 四周边缘 = 缩放热区（最大化/全屏时不可缩放）
	if (!isEdgeToEdge(window)) {
		const QSize size = window->size();
		const bool left = logicalPos.x() < kResizeBorder;
		const bool right = logicalPos.x() >= size.width() - kResizeBorder;
		const bool top = logicalPos.y() < kResizeBorder;
		const bool bottom = logicalPos.y() >= size.height() - kResizeBorder;
		if (top && left)
			return HTTOPLEFT;
		if (top && right)
			return HTTOPRIGHT;
		if (bottom && left)
			return HTBOTTOMLEFT;
		if (bottom && right)
			return HTBOTTOMRIGHT;
		if (left)
			return HTLEFT;
		if (right)
			return HTRIGHT;
		if (top)
			return HTTOP;
		if (bottom)
			return HTBOTTOM;
	}

	// 自绘标题栏：整块是拖动区，窗口按钮区域除外 —— 否则鼠标落在按钮上会被系统当成标题栏拖动，
	// 按钮永远收不到点击。
	// ⚠️ 只有 caption item **本身在这扇窗里**时才算：本类服务多扇窗（主窗口 + 右栏副窗口），
	//    而 caption 是主窗口的自绘标题栏；拿它的矩形去判副窗口会算到"别的窗口的场景坐标"里去，
	//    副窗口整块都可能被判成 HTCAPTION（副窗口不能整块拖动）。
	if (m_captionItem && m_captionItem->window() == window
		&& itemRectInWindow(m_captionItem.data()).contains(logicalPos)) {
		const ControlEntry* control = windowControlAt(window, logicalPos);
		if (!control)
			return HTCAPTION;

		if (controlKind)
			*controlKind = control->kind;

		// 交给系统处理三个窗口按钮：点击、悬停与 Win11 Snap Layouts 都靠它
		if (m_systemWindowButtons) {
			switch (control->kind) {
			case ControlMinimize:
				return HTMINBUTTON;
			case ControlMaximize:
				return HTMAXBUTTON;
			case ControlClose:
				return HTCLOSE;
			default:
				break;
			}
		}
		return HTCLIENT;  // 其它挂在标题栏上的控件（搜索框/扩展按钮）一律当客户区
	}
#else
	Q_UNUSED(window);
	Q_UNUSED(logicalPos);
	Q_UNUSED(controlKind);
#endif

	return HTCLIENT;
}

void NativeWindowFilter::updateHoveredKind(int kind)
{
	if (m_hoveredKind == kind)
		return;

	m_hoveredKind = kind;
	if (m_hoveredKindChanged)
		m_hoveredKindChanged(m_hoveredKind);
}

bool NativeWindowFilter::nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result)
{
#ifdef Q_OS_WIN
	if (eventType != QByteArrayLiteral("windows_generic_MSG")
		&& eventType != QByteArrayLiteral("windows_dispatcher_MSG")) {
		return false;
	}

	MSG* msg = static_cast<MSG*>(message);
	if (!msg)
		return false;

	QWindow* window = findByHwnd(reinterpret_cast<void*>(msg->hwnd));
	if (!window)
		return false;

	switch (msg->message) {
	case WM_NCCALCSIZE:
		if (msg->wParam == TRUE) {
			// 返回 0：客户区 = 整个窗口。这个等式必须"永远"成立：最大化时若把客户区钉到工作区，
			// Qt 的重绘目标就落到可视区之外、窗口表面再也不更新
			if (result)
				*result = 0;
			return true;
		}
		break;
	case WM_NCHITTEST: {
		// WM_NCHITTEST 的 lParam 是**屏幕物理像素**，QML 几何是逻辑像素，必须先换算，
		// 否则整块窗口都成了缩放热区。
		// 缩放比不取 QWindow::devicePixelRatio()：那个值受平台/感知设置影响，实测与本窗口的
		// 物理客户区对不上；直接用"窗口自身的物理客户区宽度 / 逻辑宽度"自校准，永远一致。
		const POINTS point = MAKEPOINTS(msg->lParam);
		POINT nativePoint{ point.x, point.y };
		ScreenToClient(msg->hwnd, &nativePoint);

		RECT clientRect{};
		GetClientRect(msg->hwnd, &clientRect);
		const int logicalWidth = qMax(1, window->size().width());
		const qreal scale = static_cast<qreal>(clientRect.right - clientRect.left) / logicalWidth;
		const QPoint logicalPos(qRound(nativePoint.x / scale), qRound(nativePoint.y / scale));

		int controlKind = ControlOther;
		const qintptr hit = hitTest(window, logicalPos, &controlKind);
		updateHoveredKind(controlKind);

		if (result)
			*result = hit;
		return true;
	}
	case WM_MOUSELEAVE:
		updateHoveredKind(ControlOther);  // 鼠标离开窗口后不会再收到命中测试，悬停态要自己收
		break;
	default:
		break;
	}
#else
	Q_UNUSED(eventType);
	Q_UNUSED(message);
	Q_UNUSED(result);
#endif

	return false;
}
