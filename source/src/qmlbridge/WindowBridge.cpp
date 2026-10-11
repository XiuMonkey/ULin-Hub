// 见 WindowBridge.h：QML 壳的窗口桥（意图入口 + 可绑定状态）。

#include "qmlbridge/WindowBridge.h"
#include "common/platform/win/NativeWindowFilter.h"

#include "common/appearance/ThemeManager.h"

#include <QDebug>
#include <QQuickItem>
#include <QQuickWindow>
#include <QScreen>
#include <QWindow>

namespace
{
	// QML 侧用字符串避免在 QML 里写魔数（见 TitleBar/WindowButton.qml）
	int controlKindFromName(const QString& name)
	{
		if (name == QLatin1String("min"))
			return NativeWindowFilter::ControlMinimize;
		if (name == QLatin1String("max"))
			return NativeWindowFilter::ControlMaximize;
		if (name == QLatin1String("close"))
			return NativeWindowFilter::ControlClose;
		return NativeWindowFilter::ControlOther;
	}

	QString controlNameFromKind(int kind)
	{
		switch (kind) {
		case NativeWindowFilter::ControlMinimize:
			return QStringLiteral("min");
		case NativeWindowFilter::ControlMaximize:
			return QStringLiteral("max");
		case NativeWindowFilter::ControlClose:
			return QStringLiteral("close");
		default:
			return QString();
		}
	}
}

WindowBridge& WindowBridge::instance()
{
	static WindowBridge bridge;
	return bridge;
}

WindowBridge::WindowBridge(QObject* parent)
	: QObject(parent)
{
	// 悬停态来自原生命中测试：交给系统处理的窗口按钮不会给 QML 发鼠标事件
	NativeWindowFilter::instance().setHoveredKindChanged([this](int kind) {
		const QString name = controlNameFromKind(kind);
		if (m_hoveredControl == name)
			return;

		m_hoveredControl = name;
		emit hoveredControlChanged();
	});
}

void WindowBridge::attachWindow(QObject* window)
{
	auto* quickWindow = qobject_cast<QQuickWindow*>(window);
	if (!quickWindow) {
		qWarning().noquote() << "[WindowBridge] attachWindow 只接受 QQuickWindow，收到:" << window;
		return;
	}

	if (m_window == quickWindow)
		return;   // 幂等：风格包重载只重挂场景，根 Window 不重建

	m_window = quickWindow;

	// 样式位必须在窗口已原生创建之后补；winId() 会强制创建原生窗口
	NativeWindowFilter::instance().applyNativeStyle(quickWindow);

	connect(quickWindow, &QWindow::windowStateChanged, this,
		[this](Qt::WindowState) { refreshFrameState(); });
	connect(quickWindow, &QWindow::screenChanged, this,
		[this](QScreen*) { refreshFrameState(); });

	refreshFrameState();
}

void WindowBridge::setCaptionItem(QObject* item)
{
	NativeWindowFilter::instance().setCaptionItem(qobject_cast<QQuickItem*>(item));
}

void WindowBridge::addWindowControl(QObject* item, const QString& kind)
{
	NativeWindowFilter::instance().addWindowControl(qobject_cast<QQuickItem*>(item),
		controlKindFromName(kind));
}

void WindowBridge::removeWindowControl(QObject* item)
{
	NativeWindowFilter::instance().removeWindowControl(qobject_cast<QQuickItem*>(item));
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

	if (m_window->windowStates().testFlag(Qt::WindowMaximized))
		m_window->showNormal();
	else
		m_window->showMaximized();
}

void WindowBridge::closeWindow()
{
	if (m_window)
		m_window->close();
}

void WindowBridge::startSystemMove()
{
	if (m_window)
		m_window->startSystemMove();
}

void WindowBridge::startSystemResize(int edges)
{
	// QML 侧传 Qt.Edge 位（Qt.LeftEdge | Qt.TopEdge ...）
	if (m_window)
		m_window->startSystemResize(Qt::Edges(edges));
}

void WindowBridge::notifyThemeChanged()
{
	emit themeChanged();
}

bool WindowBridge::isMaximized() const
{
	return m_window && m_window->windowStates().testFlag(Qt::WindowMaximized);
}

bool WindowBridge::isFullScreen() const
{
	return m_window && m_window->windowStates().testFlag(Qt::WindowFullScreen);
}

int WindowBridge::contentInset() const
{
	return NativeWindowFilter::instance().maximizedContentInset(m_window.data());
}

bool WindowBridge::nativeWindowButtons() const
{
	return NativeWindowFilter::instance().systemWindowButtons();
}

void WindowBridge::setNativeWindowButtons(bool enabled)
{
	if (nativeWindowButtons() == enabled)
		return;

	NativeWindowFilter::instance().setSystemWindowButtons(enabled);
	emit nativeWindowButtonsChanged();
}

bool WindowBridge::isDark() const
{
	return ThemeManager::instance().isDark();
}

void WindowBridge::refreshFrameState()
{
	emit frameStateChanged();
}
