#pragma once

// QML 壳的 A 层 token（见 docs/ui-qml-migration-plan.md §2.4）：
//   颜色 …… theme-<mode>.json（ThemeManager 负责释放与解析，本类只取现成的调色板）
//   尺寸/圆角/字号/动效 …… <exe>/styles/metrics.json（用户可改；缺失或键缺失就用内置默认值）
//
// QML 侧读法：Theme.color.cardBg / Theme.radius.card / Theme.font.title /
//            Theme.spacing.columnClearance / Theme.size.sidebarWidth / Theme.motion.fast
// 值都是 QVariantMap，QML 里用点号逐层取；reload() 后发 changed()，绑定自动重绑。

#include <QObject>
#include <QString>
#include <QVariantMap>

class ThemeBridge : public QObject
{
	Q_OBJECT
	Q_PROPERTY(QVariantMap color READ color NOTIFY changed)
	Q_PROPERTY(QVariantMap radius READ radius NOTIFY changed)
	Q_PROPERTY(QVariantMap font READ font NOTIFY changed)
	Q_PROPERTY(QVariantMap spacing READ spacing NOTIFY changed)
	Q_PROPERTY(QVariantMap size READ size NOTIFY changed)
	Q_PROPERTY(QVariantMap shadow READ shadow NOTIFY changed)
	Q_PROPERTY(QVariantMap motion READ motion NOTIFY changed)
	Q_PROPERTY(bool isDark READ isDark NOTIFY changed)

public:
	// 程序级唯一实例；刻意不析构 —— QML 单例注册要求它活过引擎
	static ThemeBridge& instance();

	// 启动期调用（ThemeManager::init 之后 —— 调色板由它释放/解析）：装配 token
	void init(const QString& stylesDir);

	// 主题/风格包变化后重装 token 并发 changed()
	Q_INVOKABLE void reload();

	QVariantMap color() const { return m_color; }
	QVariantMap radius() const { return m_radius; }
	QVariantMap font() const { return m_font; }
	QVariantMap spacing() const { return m_spacing; }
	QVariantMap size() const { return m_size; }
	QVariantMap shadow() const { return m_shadow; }
	QVariantMap motion() const { return m_motion; }
	bool isDark() const;

signals:
	void changed();

private:
	explicit ThemeBridge(QObject* parent = nullptr);

	void rebuild();

	QString m_stylesDir;
	QVariantMap m_color;
	QVariantMap m_radius;
	QVariantMap m_font;
	QVariantMap m_spacing;
	QVariantMap m_size;
	QVariantMap m_shadow;
	QVariantMap m_motion;
};
