#include "qmlbridge/WindowBridge.h"

#include "common/appearance/ThemeManager.h"
#include "qmlbridge/ThemeBridge.h"

#include <QCursor>
#include <QElapsedTimer>
#include <QEvent>
#include <QGuiApplication>
#include <QMouseEvent>
#include <QQuickItem>
#include <QQuickWindow>
#include <QWindow>

WindowBridge& WindowBridge::instance()
{
	// QML 单例：qmlRegisterSingletonInstance 注册的就是这一个实例，首次取用时才构造
	static WindowBridge bridge;
	return bridge;
}

WindowBridge::WindowBridge(QObject* parent)
	: QObject(parent)
{
	m_pointerAge.start();
	// 全局只装一个过滤器：QML 里所有可拖区域共用这同一份"最近按键"状态
	qApp->installEventFilter(this);

	// 换肤后无需原生重刷（Linux 是自绘标题栏），保留订阅以维持与 Windows 版一致的行为契约。
	connect(&ThemeBridge::instance(), &ThemeBridge::changed, this, &WindowBridge::themeChanged);
}

bool WindowBridge::eventFilter(QObject*, QEvent* event)
{
	// 记下每次按键的位置与时间：QML 开始拖动时会问"最近指针在哪"，据此对齐起点
	if (event->type() == QEvent::MouseButtonPress
		|| event->type() == QEvent::MouseButtonRelease) {
		auto* me = static_cast<QMouseEvent*>(event);
		m_pointerPos = me->position();
		m_pointerValid = true;
		m_pointerAge.restart();
	}
	return false;   // 只旁观，不拦截
}

void WindowBridge::attachWindow(QObject* window)
{
	// 只认 QQuickWindow：QML 里误传 item 或重复 attach 同一个窗口都直接忽略
	auto* quickWindow = qobject_cast<QQuickWindow*>(window);
	if (!quickWindow || m_window == quickWindow)
		return;
	m_window = quickWindow;
	// 换了 attach 目标后必须重接：maximized / fullScreen 属性要跟着新窗口的状态变
	connect(m_window, &QWindow::windowStateChanged, this, &WindowBridge::frameStateChanged);
	emit frameStateChanged();
}

namespace
{
// QML 传进来的既可能是窗口也可能是 item（自绘标题栏里是 MouseArea），统一解析成能动的窗口
QWindow* resolveWindow(QObject* obj, QPointer<QQuickWindow> fallback)
{
	if (auto* w = qobject_cast<QWindow*>(obj))
		return w;
	if (auto* item = qobject_cast<QQuickItem*>(obj))
		return item->window();
	return fallback.data();
}
}

// 自绘标题栏的拖动：交给平台的系统移动，不在 QML 里自己算窗口几何
void WindowBridge::startWindowMove(QObject* window)
{
	if (auto* w = resolveWindow(window, m_window))
		w->startSystemMove();
}

void WindowBridge::startWindowResize(QObject* window, int edges)
{
	// edges 是 Qt::Edges 的位掩码，由 QML 按拖的是哪条边 / 哪个角传进来
	if (auto* w = resolveWindow(window, m_window))
		w->startSystemResize(static_cast<Qt::Edges>(edges));
}

void WindowBridge::startSystemMove()
{
	// 无参版：直接动 attach 的那个窗口，QML 里最常用的调用形状
	if (m_window)
		m_window->startSystemMove();
}

void WindowBridge::startSystemResize(int edges)
{
	// 同上，edges 由调用方按拖的是哪条边给出
	if (m_window)
		m_window->startSystemResize(static_cast<Qt::Edges>(edges));
}

void WindowBridge::minimize()
{
	if (m_window)
		m_window->showMinimized();
}

void WindowBridge::toggleMaximize()
{
	// 交给窗口管理器：已最大化就还原，否则最大化
	if (!m_window)
		return;
	if (m_window->windowStates() & Qt::WindowMaximized)
		m_window->showNormal();
	else
		m_window->showMaximized();
}

void WindowBridge::closeWindow()
{
	if (m_window)
		m_window->close();
}

void WindowBridge::auxMinimize() { minimize(); }
void WindowBridge::auxToggleMaximize() { toggleMaximize(); }   // Linux 上辅助窗口就是主窗口，直接转调

// 窗口状态读自 QQuickWindow::windowStates()：没 attach 到窗口时一律返回 false
bool WindowBridge::isMaximized() const
{
	return m_window && (m_window->windowStates() & Qt::WindowMaximized);
}

bool WindowBridge::isFullScreen() const
{
	return m_window && (m_window->windowStates() & Qt::WindowFullScreen);
}

bool WindowBridge::isDark() const
{
	return ThemeManager::instance().isDark();
}

QPoint WindowBridge::cursorScreenPos() const { return QCursor::pos(); }

bool WindowBridge::leftButtonDown() const
{
	// QML 拖动时据此判断按键是否已释放：释放了就该停止继续移动窗口
	return (QGuiApplication::mouseButtons() & Qt::LeftButton) != 0;
}

QVariant WindowBridge::recentPointerPos(int maxAgeMs) const
{
	// 没按过键或太久以前按的都不作数：返回无效值，QML 侧据此退回系统默认拖动
	if (!m_pointerValid || m_pointerAge.elapsed() > maxAgeMs)
		return QVariant();
	return QVariant(m_pointerPos);
}
