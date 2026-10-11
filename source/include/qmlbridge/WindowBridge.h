#pragma once

// QML 壳的窗口桥：QML 侧只发意图（拖动/最小化/最大化/关闭/缩放），窗口级状态与原生判定都落在
// NativeWindowFilter 里；本类只是"能被 QML 调到的那一面" + 可绑定属性。
//
// 与旧控件壳的分工一致（TitleBar 只发信号、落地逻辑在后面；旧壳那份 WindowFrame 已删除），只是跨了 QML/C++ 边界。

#include <QObject>
#include <QPointer>
#include <QString>

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

	QPointer<QQuickWindow> m_window;
	QString m_hoveredControl;
};
