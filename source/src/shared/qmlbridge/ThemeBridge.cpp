// 实现 ThemeBridge.h：把主题数据摊平成 QML 壳的 A 层 token。
// 内置默认值在 common/appearance/ThemeTokens，source/qml/metrics.json 只做覆盖，缺失或解析失败都不会崩。

#include "qmlbridge/ThemeBridge.h"

#include "common/appearance/ThemeManager.h"
#include "common/appearance/ThemeTokens.h"
#include "common/util/CodeHighlighter.h"
#include "common/util/ColorText.h"

#include <QColor>
#include <QDebug>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QDir>
#include <QFile>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QRegularExpression>

namespace
{
	// 调色板颜色统一转成 QColor：QML 的 color 类型不接受 rgb()/rgba() 字符串（QSS 时代合法），
	// 否则绑定被丢弃、属性退回默认值——阴影变 MultiEffect 默认的不透明黑、悬停底色同理。
	// 解析走 util/ColorText.h；失败时给品红占位，便于一眼看出 theme-*.json 写坏了。
	QColor parsePaletteColor(const QString& text, bool* ok)
	{
		const QColor color = parseColorText(text, ok);
		if (!color.isValid()) {
			if (ok)
				*ok = false;
			return QColor(Qt::magenta);
		}
		return color;
	}
}

ThemeBridge& ThemeBridge::instance()
{
	static ThemeBridge bridge;
	return bridge;
}

ThemeBridge::ThemeBridge(QObject* parent)
	: QObject(parent)
{
}

void ThemeBridge::init(const QString& stylesDir)
{
	m_stylesDir = stylesDir;
	connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, &ThemeBridge::reload);
	rebuild();
}

void ThemeBridge::reload()
{
	rebuild();
}

void ThemeBridge::rebuild()
{
	// 调色板键与旧 QSS 的 {{key}} 一致，老风格包的 theme-*.json 可原样沿用；这里必须转成 QColor。
	const QHash<QString, QString> palette = ThemeManager::instance().palette();
	m_color.clear();
	for (auto it = palette.constBegin(); it != palette.constEnd(); ++it) {
		bool ok = false;
		const QColor parsed = parsePaletteColor(it.value(), &ok);
		if (!ok) {
			qWarning().noquote() << "[Theme] 调色板里的颜色解析失败，暂用品红占位（请修 theme-*.json）："
				<< it.key() << "=" << it.value();
		}
		m_color.insert(it.key(), parsed);
	}

	// syntax* 键同样来自主题包，整包推给高亮器（它自己按 "syntax" 前缀映射角色名），换肤后代码块按新配色重渲染。
	CodeHighlighter::instance().setThemeColors(palette, isDark());

	m_radius = ThemeTokens::radius();  // 尺寸/圆角/字号/间距/阴影/动效：common 默认值 + metrics.json 覆盖
	m_font = ThemeTokens::font();
	m_spacing = ThemeTokens::spacing();
	m_size = ThemeTokens::size();
	m_shadow = ThemeTokens::shadow();
	m_motion = ThemeTokens::motion();

	const QString path = m_stylesDir + QStringLiteral("/metrics.json");
	QFile file(path);
	if (file.open(QIODevice::ReadOnly)) {
		QJsonParseError parseError{};
		const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
		if (parseError.error == QJsonParseError::NoError && doc.isObject()) {
			const QJsonObject root = doc.object();
			ThemeTokens::applyMetrics(m_radius, root, QStringLiteral("radius"));
			ThemeTokens::applyMetrics(m_font, root, QStringLiteral("font"));
			ThemeTokens::applyMetrics(m_spacing, root, QStringLiteral("spacing"));
			ThemeTokens::applyMetrics(m_size, root, QStringLiteral("size"));
			ThemeTokens::applyMetrics(m_shadow, root, QStringLiteral("shadow"));
			ThemeTokens::applyMetrics(m_motion, root, QStringLiteral("motion"));
		}
		else {
			qWarning().noquote() << "[Theme] metrics.json 解析失败，用内置默认值：" << parseError.errorString();
		}
	}

	emit changed();
}

bool ThemeBridge::isDark() const
{
	return ThemeManager::instance().isDark();
}