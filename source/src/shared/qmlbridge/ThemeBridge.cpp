// 见 ThemeBridge.h：QML 壳的 A 层 token。
// 内置默认值取自 docs/qss-token-archaeology.md（12 份 QSS + C++ 布局常量的频次统计），
// 与 source/qml/metrics.json 逐键一致；metrics.json 只做覆盖，所以即使它被删/写坏，界面也不会崩。
// 值若与现有实现不一致，注释里会点明 —— 删后这些注释就是唯一的出处记录。

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
	// 解析 theme-*.json 里的颜色：⚠️ rgb()/rgba() 函数式写法必须自己处理 —— QML 的 color 值类型不接受它
	// 而 QSS 时代它合法）：这些字符串塞进 QML 后绑定被丢弃、属性退回默认值 —— 阴影就成了 MultiEffect 默认的
	// 不透明黑、悬停底色同理，所以这里统一转成 QColor（带 alpha）再交给 QML。
	// 支持 #RGB / #RRGGBB / #AARRGGBB / SVG 颜色名 / transparent，外加 rgb(r,g,b) 与 rgba(r,g,b,a)（a 可为 0..1 浮点或 0..255 整数）。
	// 调色板颜色：解析交给 util/ColorText.h（rgb()/rgba() 与 #RRGGBB 等同一份实现）；
	// 解析失败这里给品红占位，方便一眼看出 theme-*.json 写坏了。
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
	connect(&ThemeManager::instance(), &ThemeManager::themeChanged,
		this, &ThemeBridge::reload);
	rebuild();
}

void ThemeBridge::reload()
{
	rebuild();
}

void ThemeBridge::rebuild()
{
	// 颜色：调色板由 ThemeManager 读 theme-<mode>.json 得到（键与旧 QSS 的 {{key}} 完全一致，老风格包里的
	// theme-*.json 可原样搬过来）；⚠️ 必须转成 QColor —— 里面的 rgb()/rgba() 字符串 QML 不接受（见 parsePaletteColor）。
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

	// 语法高亮的色值也走主题包（syntax* 键）：推给高亮器，换肤后代码块才能按新配色重渲染。
	// 高亮器自己解析这些键（去掉 "syntax" 前缀 == 规则里的角色名），所以这里整包塞过去即可。
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