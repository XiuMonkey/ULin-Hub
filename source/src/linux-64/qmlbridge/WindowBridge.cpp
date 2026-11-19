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
	static WindowBridge bridge;
	return bridge;
}

WindowBridge::WindowBridge(QObject* parent)
	: QObject(parent)
{
	m_pointerAge.start();
	qApp->installEventFilter(this);

	// 换肤后无需原生重刷（Linux 是自绘标题栏），保留订阅以维持与 Windows 版一致的行为契约。
	connect(&ThemeBridge::instance(), &ThemeBridge::changed,
		this, &WindowBridge::themeChanged);
}

bool WindowBridge::eventFilter(QObject*, QEvent* event)
{
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
	auto* quickWindow = qobject_cast<QQuickWindow*>(window);
	if (!quickWindow || m_window == quickWindow)
		return;
	m_window = quickWindow;
	connect(m_window, &QWindow::windowStateChanged,
		this, &WindowBridge::frameStateChanged);
	emit frameStateChanged();
}

namespace
{
QWindow* resolveWindow(QObject* obj, QPointer<QQuickWindow> fallback)
{
	if (auto* w = qobject_cast<QWindow*>(obj))
		return w;
	if (auto* item = qobject_cast<QQuickItem*>(obj))
		return item->window();
	return fallback.data();
}
}

void WindowBridge::startWindowMove(QObject* window)
{
	if (auto* w = resolveWindow(window, m_window))
		w->startSystemMove();
}

void WindowBridge::startWindowResize(QObject* window, int edges)
{
	if (auto* w = resolveWindow(window, m_window))
		w->startSystemResize(static_cast<Qt::Edges>(edges));
}

void WindowBridge::startSystemMove()
{
	if (m_window)
		m_window->startSystemMove();
}

void WindowBridge::startSystemResize(int edges)
{
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
void WindowBridge::auxToggleMaximize() { toggleMaximize(); }

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
	return (QGuiApplication::mouseButtons() & Qt::LeftButton) != 0;
}

QVariant WindowBridge::recentPointerPos(int maxAgeMs) const
{
	if (!m_pointerValid || m_pointerAge.elapsed() > maxAgeMs)
		return QVariant();
	return QVariant(m_pointerPos);
}
