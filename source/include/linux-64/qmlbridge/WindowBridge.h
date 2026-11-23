#pragma once
// Linux 平台层：QML 壳的窗口桥。与 Windows 版保持同名同义的 QML 契约
// （单例名 WindowBridge + 下列 Q_PROPERTY / Q_INVOKABLE），实现只用 Qt6。
#include <QElapsedTimer>
#include <QObject>
#include <QPointF>
#include <QPointer>
#include <QVariant>

class QQuickWindow;

class WindowBridge : public QObject
{
	Q_OBJECT
	Q_PROPERTY(bool maximized READ isMaximized NOTIFY frameStateChanged)
	Q_PROPERTY(bool fullScreen READ isFullScreen NOTIFY frameStateChanged)
	Q_PROPERTY(int contentInset READ contentInset NOTIFY frameStateChanged)
	Q_PROPERTY(bool nativeWindowButtons READ nativeWindowButtons WRITE setNativeWindowButtons NOTIFY nativeWindowButtonsChanged)
	Q_PROPERTY(bool isDark READ isDark NOTIFY themeChanged)

public:
	static WindowBridge& instance();

	Q_INVOKABLE void attachWindow(QObject* window);
	Q_INVOKABLE void attachAuxWindow(QObject* window) { attachWindow(window); }
	// 空实现（markAppWindow / aux* / setCaptionItem / addWindowControl 等）是契约占位：Windows 版要
	// 处理原生辅助窗口与标题栏控件，Qt6 自绘壳没有这些概念，留空体只为让同一份 QML 原样跑通（参数名省掉，避免未使用告警）。
	Q_INVOKABLE void markAppWindow(QObject*) {}
	Q_INVOKABLE void startWindowMove(QObject* window);
	Q_INVOKABLE void startWindowResize(QObject* window, int edges);
	Q_INVOKABLE void auxApplyGeometry(qreal, qreal, qreal, qreal) {}
	Q_INVOKABLE void auxPrepareLift(qreal, qreal, qreal, qreal) {}
	Q_INVOKABLE void auxNormalizeLift() {}
	Q_INVOKABLE void auxMinimize();
	Q_INVOKABLE void auxToggleMaximize();
	Q_INVOKABLE void auxResizeBegin(qreal, qreal) {}
	Q_INVOKABLE void auxResizeApply(int, qreal, qreal, bool, qreal, qreal) {}
	Q_INVOKABLE void setCaptionItem(QObject*) {}
	Q_INVOKABLE void addWindowControl(QObject*, const QString&) {}
	Q_INVOKABLE void removeWindowControl(QObject*) {}
	Q_INVOKABLE void minimize();
	Q_INVOKABLE void toggleMaximize();
	Q_INVOKABLE void closeWindow();
	Q_INVOKABLE void startSystemMove();
	Q_INVOKABLE void startSystemResize(int edges);
	Q_INVOKABLE void notifyThemeChanged() {}
	// 指针状态查询：自绘标题栏的拖动 / 缩放靠它判断起点与按键，QML 侧不必自己记事件
	Q_INVOKABLE QPoint cursorScreenPos() const;
	Q_INVOKABLE bool leftButtonDown() const;
	Q_INVOKABLE QVariant recentPointerPos(int maxAgeMs) const;

	bool isMaximized() const;
	bool isFullScreen() const;
	int contentInset() const { return 0; }
	bool nativeWindowButtons() const { return false; }
	void setNativeWindowButtons(bool) {}
	bool isDark() const;

signals:
	// QML 的属性绑定靠这几个 NOTIFY 信号刷新：窗口状态 / 标题栏按钮 / 主题
	void frameStateChanged();
	void nativeWindowButtonsChanged();
	void themeChanged();

private:
	explicit WindowBridge(QObject* parent = nullptr);
	// 全局鼠标事件过滤器：只记录最近一次按键位置，返回 false 从不拦截事件
	bool eventFilter(QObject* watched, QEvent* event) override;

	QPointer<QQuickWindow> m_window;
	// 最近一次鼠标按键的位置与时间戳：recentPointerPos 据此判断拖拽起点是否还新鲜
	QPointF m_pointerPos;
	QElapsedTimer m_pointerAge;
	bool m_pointerValid = false;
};
