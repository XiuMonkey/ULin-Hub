#pragma once

// 分层（领域归位 2026-10-15）：本类**留在 qmlbridge/** —— 它是窗口意图的接线面（QML 只发意图，
// 窗口级状态与原生判定全在 common/platform/win/NativeWindowFilter 里），正好符合"qmlbridge 只做接线"。
//
// QML 壳的窗口桥：QML 侧只发意图（拖动/最小化/最大化/关闭/缩放），窗口级状态与原生判定都落在
// NativeWindowFilter 里；本类只是"能被 QML 调到的那一面" + 可绑定属性。
//
// 与旧控件壳的分工一致（TitleBar 只发信号、落地逻辑在后面；旧壳那份 WindowFrame 已删除），只是跨了 QML/C++ 边界。
//
// 例外的一处"观察"：最近一次鼠标点击的位置（recentPointerPos）。它既不是意图也不是可绑定状态，
// 而是**弹窗需要的上下文** —— 弹窗没有"触发控件"可言（open() 可能是键盘 / 定时器 / 任何代码调的），
// 要走"从点击处长出来"的动画就只能由窗口层记住那个点。做法是事件过滤器**旁观**鼠标事件：
// 不改写、不拦截、不参与命中测试，所以对现有交互零影响（这也是它必须在这里、而不能在 QML 里做
// 一个铺满窗口的 HoverHandler 的原因：后者会成为最上层接受 hover 的项，把下面所有控件的悬停吃掉）。

#include <QElapsedTimer>
#include <QObject>
#include <QPointer>
#include <QPointF>
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
		// 悬停到哪个窗口按钮上（"" / min / max / close）。交给系统处理的按钮收不到鼠标事件，
		// 悬停反馈只能由原生命中测试回传
		Q_PROPERTY(QString hoveredControl READ hoveredControl NOTIFY hoveredControlChanged)
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

	// ---- 拖动跟手用的两个**纯查询**（转发 NativeWindowFilter，见那边的说明）----
	// 右栏"拿起来"之后内容在另一扇窗里、鼠标捕获还在主窗口 ⇒ 那扇窗收不到鼠标消息，
	// "跟到哪儿了 / 松没松手"只能这样直接问系统。不碰窗口、不改状态，可以在拖动里高频调（~120Hz）。
	Q_INVOKABLE QPoint cursorScreenPos() const;
	Q_INVOKABLE bool leftButtonDown() const;

	// 最近 maxAgeMs 毫秒内发生过鼠标按下 / 抬起 ⇒ 返回那次点击的位置（窗口内容坐标 = QML 场景坐标，
	// 逻辑像素，与 Overlay.overlay 同源）；否则返回**无效 QVariant**（QML 侧拿到 undefined）。
	// 判"够不够近"是为了区分"这次弹出就是这次点击触发的"与"纯键盘 / 程序触发的弹出"：
	// 后者若还拿几分钟前那次点击当起点，卡片会从屏幕上一处毫不相干的地方飞出来。
	Q_INVOKABLE QVariant recentPointerPos(int maxAgeMs) const;

	bool isMaximized() const;
	bool isFullScreen() const;
	int contentInset() const;
	QString hoveredControl() const { return m_hoveredControl; }
	bool nativeWindowButtons() const;
	void setNativeWindowButtons(bool enabled);
	bool isDark() const;

signals:
	void frameStateChanged();
	void hoveredControlChanged();
	void nativeWindowButtonsChanged();
	void themeChanged();

private:
	explicit WindowBridge(QObject* parent = nullptr);

	void refreshFrameState();

	// 旁观鼠标按下 / 抬起，只记位置（见文件头）。永远是"不处理"，事件照原样走下去。
	bool eventFilter(QObject* watched, QEvent* event) override;

	QPointer<QQuickWindow> m_window;
	QString m_hoveredControl;

	// 最近一次点击的位置与它距今多久（isValid() == false = 窗口起来之后还没见过点击）
	QPointF m_pointerPos;
	QElapsedTimer m_pointerAge;
};
