#pragma once

// 切主题编排已下沉到 common/appearance/ThemeManager。
// 本类只保留 QML 侧的转发：toggle -> ThemeManager::toggleTheme，
// switching -> ThemeManager::themeSwitching。

#include <QObject>

class ThemeController : public QObject
{
	Q_OBJECT

public:
	explicit ThemeController(QObject* parent = nullptr);

	void toggle();
	bool switching() const;

signals:
	void switchingChanged();

private:
	QMetaObject::Connection m_switchingConnection;
};
