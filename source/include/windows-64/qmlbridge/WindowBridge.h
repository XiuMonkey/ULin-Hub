#pragma once

// QML 壳的窗口桥：QML 只发意图（拖动/最小化/最大化/关闭/缩放），窗口状态与原生判定全在
// common/platform/win/NativeWindowFilter 里，本类是可被 QML 调到的那一面 + 可绑定属性。
// 唯一例外是 recentPointerPos：弹窗需要"从点击处长出来"的上下文，由事件过滤器旁观鼠标事件记录，
// 不改写、不拦截、不参与命中测试（在 QML 里铺 HoverHandler 会吃掉所有控件的悬停）。

#include <QElapsedTimer>
#include <QObject>
#include <QPointer>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QVariant>

class QQuickWindow;

class WindowBridge : public QObject
{
	Q_OBJECT
		// 最大化/全屏：标题栏要换图标、外壳要切圆角与描边
		Q_PROPERTY(bool maximized READ isMaximized NOTIFY frameStateChanged)
		Q_PROPERTY(bool fullScreen READ isFullScreen NOTIFY frameStateChanged)
		// 最大化时系统多给的那一圈（逻辑像素）：根内容按它内缩，可见内容才落进工作区
		Q_PROPERTY(int contentInset READ contentInset NOTIFY frameStateChanged)
		// 三个窗口按钮是否交给系统处理（true = 系统负责点击/悬停/贴边吸附与 Snap Layouts，QML 只画）。
		// 置 false 时由 QML 自己点、自己画悬停，代价是失去 Snap Layouts —— 仅作调试兜底
		Q_PROPERTY(bool nativeWindowButtons READ nativeWindowButtons WRITE setNativeWindowButtons NOTIFY nativeWindowButtonsChanged)
		// 当前是否深色主题（标题栏图标要用反色版）
		Q_PROPERTY(bool isDark READ isDark NOTIFY themeChanged)

public:
	// 程序级唯一实例；刻意不析构 —— QML 单例注册要求它活过引擎
	static WindowBridge& instance();

	// 根 Window 的 Component.onCompleted 里调一次：记窗口、装原生样式位、接帧状态信号
	Q_INVOKABLE void attachWindow(QObject* window);

	Q_INVOKABLE void attachAuxWindow(QObject* window);
	// 只把窗口补成“任务栏里可见的独立顶层窗口”，不改 m_auxWindow（单块脱出窗口用）。
	Q_INVOKABLE void markAppWindow(QObject* window);
	// 无边框副窗口的拖动/缩放交给系统 sizing loop：不逐帧写 win.x/width，避免 QML 跟手抖动。
	Q_INVOKABLE void startWindowMove(QObject* window);
	Q_INVOKABLE void startWindowResize(QObject* window, int edges);
	// 副窗口（右栏脱出窗口）的窗口行为：几何、最大化/还原、四边缩放。
	// QML 只画热区和按钮，状态与几何都在 C++ 侧维护。
	Q_INVOKABLE void auxApplyGeometry(qreal x, qreal y, qreal w, qreal h);
	Q_INVOKABLE void auxPrepareLift(qreal x, qreal y, qreal w, qreal h);
	Q_INVOKABLE void auxNormalizeLift();
	Q_INVOKABLE void auxMinimize();
	Q_INVOKABLE void auxToggleMaximize();
	Q_INVOKABLE void auxResizeBegin(qreal screenX, qreal screenY);
	Q_INVOKABLE void auxResizeApply(int edges, qreal screenX, qreal screenY, bool handOffToSystem, qreal minWidth, qreal minHeight);

	// 自绘标题栏与窗口按钮登记（语义见 NativeWindowFilter 的同名方法）
	Q_INVOKABLE void setCaptionItem(QObject* item);
	Q_INVOKABLE void addWindowControl(QObject* item, const QString& kind);
	Q_INVOKABLE void removeWindowControl(QObject* item);

	// 窗口动作：原生接管关闭时 QML 才需要调它们；也供扩展自绘窗口条使用
	Q_INVOKABLE void minimize();
	Q_INVOKABLE void toggleMaximize();
	Q_INVOKABLE void closeWindow();
	Q_INVOKABLE void startSystemMove();
	Q_INVOKABLE void startSystemResize(int edges);

	// 主题/外观被外部改过（扩展换风格包、系统深色切换）后由宿主调一次
	Q_INVOKABLE void notifyThemeChanged();

	Q_INVOKABLE QPoint cursorScreenPos() const;
	Q_INVOKABLE bool leftButtonDown() const;

	// 最近 maxAgeMs 毫秒内发生过鼠标按下/抬起 ⇒ 返回那次点击的位置（窗口内容坐标，逻辑像素）；
	// 否则返回无效 QVariant。判"够不够近"是为了区分点击触发与键盘/程序触发的弹出。
	Q_INVOKABLE QVariant recentPointerPos(int maxAgeMs) const;

	bool isMaximized() const;
	bool isFullScreen() const;
	int contentInset() const;
	bool nativeWindowButtons() const;
	void setNativeWindowButtons(bool enabled);
	bool isDark() const;

signals:
	void frameStateChanged();
	void nativeWindowButtonsChanged();
	void themeChanged();

private:
	explicit WindowBridge(QObject* parent = nullptr);

	void refreshFrameState();

	// 旁观鼠标按下 / 抬起，只记位置（见文件头）。永远是"不处理"，事件照原样走下去。
	bool eventFilter(QObject* watched, QEvent* event) override;

	QPointer<QQuickWindow> m_window;
	QPointer<QQuickWindow> m_auxWindow;
	QRectF m_auxWindowedGeometry;
	QRectF m_auxLiftGeometry;
	QRectF m_auxResizePressGeometry;
	QPointF m_auxResizePressScreen;
	bool m_auxResizeHandedOff = false;
	bool m_auxMinimizeWasMaximized = false;

	// 最近一次点击的位置与距今时长（isValid == false = 窗口起来后还没见过点击）；只记主窗口的。
	QPointF m_pointerPos;
	QElapsedTimer m_pointerAge;
};
