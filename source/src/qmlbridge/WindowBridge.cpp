// 见 WindowBridge.h：QML 壳的窗口桥（意图入口 + 可绑定状态）。

#include "qmlbridge/WindowBridge.h"
#include "common/platform/win/NativeWindowFilter.h"

#include "common/appearance/ThemeManager.h"

#include <QDebug>
#include <QEvent>
#include <QGuiApplication>
#include <QMouseEvent>
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

	// 指针位置只在"有窗口"之后才有意义，所以过滤器挂在这里而不是构造函数里
	// （构造函数可能在 QGuiApplication 之前跑）。app 级过滤器是**旁观**的，见头文件。
	if (auto* app = QGuiApplication::instance())
		app->installEventFilter(this);

	refreshFrameState();
}

bool WindowBridge::eventFilter(QObject* watched, QEvent* event)
{
	// 只记"按下了 / 松开了"，不看位置变化：弹窗的起点要的是**这次点击落在哪**，
	// 而按下与抬起之间指针可能已经挪动过（拖拽选择之类），记按下那一刻更贴近"点哪儿了"。
	switch (event->type()) {
	case QEvent::MouseButtonPress:
	case QEvent::MouseButtonRelease: {
		// 过滤器挂在 QGuiApplication 上 ⇒ watched 是**接收者**。Qt Quick 的鼠标事件投给
		// QQuickWindow（QML 控件那一层是场景图内部的命中测试，不走 QObject 事件），但也可能
		// 投给某个 QQuickItem；两种都认，别的一律不算（将来多窗 / 原生弹层不该被记进来）。
		const bool fromUs = m_window
			&& (watched == m_window.data()
				|| (qobject_cast<QQuickItem*>(watched) && qobject_cast<QQuickItem*>(watched)->window() == m_window.data()));
		if (!fromUs)
			break;

		// ⚠️ 必须用 scenePosition()，不能用 position()：
		//    Qt Quick 的鼠标事件可能投给 QQuickWindow，也可能投给某个 QQuickItem（QML 控件），
		//    投给 item 时 position() 是**该 item 的局部坐标**（实测点侧栏 ⚙ 时得到 19,6，
		//    正好落在那个 32×32 的按钮里），而 QML 侧按"场景坐标"去 mapFromItem ⇒
		//    起点算到左上角，表现就是"覆盖页卡片从左上角长出来"。
		//    scenePosition() 两种投递下都是同一套场景坐标（窗口内容坐标、逻辑像素）。
		auto* mouse = static_cast<QMouseEvent*>(event);
		m_pointerPos = mouse->scenePosition();
		m_pointerAge.start();   // QElapsedTimer::start() 就是"重启"，重复调用取最近一次
		break;
	}
	default:
		break;
	}
	return QObject::eventFilter(watched, event);
}

QVariant WindowBridge::recentPointerPos(int maxAgeMs) const
{
	if (!m_pointerAge.isValid())
		return QVariant();   // 窗口起来之后还没见过鼠标点击（纯键盘操作）
	if (maxAgeMs > 0 && m_pointerAge.elapsed() > maxAgeMs)
		return QVariant();   // 上一次点击太久了，跟这次弹出没关系
	return m_pointerPos;
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

// ---- 拖动跟手用的两个纯查询：一律转发 NativeWindowFilter（原生判定归它，见那边的说明）----
QPoint WindowBridge::cursorScreenPos() const
{
	return NativeWindowFilter::cursorScreenPos();
}

bool WindowBridge::leftButtonDown() const
{
	return NativeWindowFilter::leftButtonDown();
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
