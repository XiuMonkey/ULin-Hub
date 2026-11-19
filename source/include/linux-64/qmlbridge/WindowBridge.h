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
	void frameStateChanged();
	void nativeWindowButtonsChanged();
	void themeChanged();

private:
	explicit WindowBridge(QObject* parent = nullptr);
	bool eventFilter(QObject* watched, QEvent* event) override;

	QPointer<QQuickWindow> m_window;
	QPointF m_pointerPos;
	QElapsedTimer m_pointerAge;
	bool m_pointerValid = false;
};
