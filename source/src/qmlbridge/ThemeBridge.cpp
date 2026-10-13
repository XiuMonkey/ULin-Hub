// 见 ThemeBridge.h：QML 壳的 A 层 token。
//
// 内置默认值取自 docs/qss-token-archaeology.md（12 份 QSS + C++ 布局常量的频次统计），
// 与 source/qml/metrics.json 逐键一致；metrics.json 只做覆盖，所以即使它被删/写坏，界面也不会崩。
// 值若与旧壳不一致，注释里会点明 —— M8 删旧壳后这些注释就是唯一的出处记录。

#include "qmlbridge/ThemeBridge.h"

#include "common/appearance/ThemeManager.h"

#include <QColor>
#include <QDebug>
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
	// （实测 `color: "rgba(0,0,0,0.12)"` 直接让整个 QML 文件加载失败："Invalid property assignment: color expected"，
	// 而 QSS 时代它合法）：这些字符串塞进 QML 后绑定被丢弃、属性退回默认值 —— 阴影就成了 MultiEffect 默认的
	// 不透明黑、悬停底色同理，所以这里统一转成 QColor（带 alpha）再交给 QML。
	// 支持 #RGB / #RRGGBB / #AARRGGBB / SVG 颜色名 / transparent，外加 rgb(r,g,b) 与 rgba(r,g,b,a)（a 可为 0..1 浮点或 0..255 整数）。
	QColor parsePaletteColor(const QString& text, bool* ok)
	{
		const QString trimmed = text.trimmed();
		if (trimmed.isEmpty()) {
			if (ok)
				*ok = false;
			return QColor(Qt::magenta);
		}

		const bool looksFunctional = trimmed.startsWith(QLatin1String("rgb"), Qt::CaseInsensitive);
		if (!looksFunctional) {
			const QColor direct(trimmed);   // #RRGGBB / #AARRGGBB / 颜色名 / transparent
			if (direct.isValid()) {
				if (ok)
					*ok = true;
				return direct;
			}
			if (ok)
				*ok = false;
			return QColor(Qt::magenta);
		}

		static const QRegularExpression pattern(
			QStringLiteral("^rgba?\\(\\s*(\\d{1,3})\\s*,\\s*(\\d{1,3})\\s*,\\s*(\\d{1,3})"
				"\\s*(?:,\\s*([0-9]*\\.?[0-9]+)\\s*)?\\)$"),
			QRegularExpression::CaseInsensitiveOption);

		const QRegularExpressionMatch match = pattern.match(trimmed);
		if (!match.hasMatch()) {
			if (ok)
				*ok = false;
			return QColor(Qt::magenta);
		}

		const int red = qBound(0, match.captured(1).toInt(), 255);
		const int green = qBound(0, match.captured(2).toInt(), 255);
		const int blue = qBound(0, match.captured(3).toInt(), 255);

		int alpha = 255;
		const QString alphaText = match.captured(4);
		if (!alphaText.isEmpty()) {
			const bool fractional = alphaText.contains(QLatin1Char('.'));   // 0..1 浮点；否则 0..255 整数
			alpha = qBound(0, fractional ? qRound(alphaText.toDouble() * 255.0) : alphaText.toInt(), 255);
		}

		if (ok)
			*ok = true;
		return QColor(red, green, blue, alpha);
	}

	QVariantMap defaultRadius()
	{
		QVariantMap map;
		map.insert(QStringLiteral("window"), 12);      // main-window.qss #dshhubCentral { border-radius: 12px }
		map.insert(QStringLiteral("panel"), 12);       // chat #chatPanel/#agentBubble/#userUnit/#interactionPanel、sidebar #sidebar、topbar #topBar（14 处）
		map.insert(QStringLiteral("card"), 10);        // topbar #toolsFilterDirectory、model-list #modelListEntry/#modelListForm、settings #agentPresetButton、plugin-market 卡片（17 处 10px）
		map.insert(QStringLiteral("inner"), 9);        // 卡片内部面（#modelListEntryHeader 等）恒为「所在卡片圆角 − 1」，与 card 成对改
		map.insert(QStringLiteral("control"), 8);      // defaults #dshhubCentral QLineEdit/QPushButton、chat #codeBlockView、sidebar 按钮（31 处 8px）
		map.insert(QStringLiteral("input"), 8);        // defaults #dshhubCentral QLineEdit、settings #serverUrlEdit、model-list #modelListField
		map.insert(QStringLiteral("small"), 6);        // main-window 三个窗口按钮 6px、popups #popupCloseButton、topbar #toolsFilterToolRow
		map.insert(QStringLiteral("check"), 5);        // chat #interactionPanel QCheckBox::indicator
		map.insert(QStringLiteral("tiny"), 4);         // 勾选框指示器/进度条/滚动条滑块/复制按钮（6 处）
		map.insert(QStringLiteral("dot"), 9);          // chat #interactionPanel QRadioButton::indicator（几何量，与 size.radioIndicator 配套）
		map.insert(QStringLiteral("chip"), 14);        // chat #modelSelectorChip = ModelSelector.cpp kChipHeight 28 / 2
		map.insert(QStringLiteral("pill"), 16);        // chat #sendButton、popups #toastLabel（3 处）
		map.insert(QStringLiteral("popup"), 16);       // popups #popupBody 16px，与 PopupWindow.cpp:29 setRadius(16) 配对
		map.insert(QStringLiteral("menu"), 12);        // chat #modelSelectorMenu（上拉菜单）
		map.insert(QStringLiteral("float"), 20);       // popups #initCard/#themeSwitchBody；Main.cpp:273 setRadius(20)（CardShadow::level3 的 14 被它硬纠正）
		map.insert(QStringLiteral("composer"), 22);    // chat #inputCapsule；与 shadow.inputRadius 绑定
		return map;
	}

	QVariantMap defaultFont()
	{
		QVariantMap map;
		map.insert(QStringLiteral("title"), 14);        // topbar #topBarTitle、model-list #modelListTitle、settings #settingsNavButton（15 处 14px）
		map.insert(QStringLiteral("body"), 13);         // 24 处 13px（含标题栏 #windowTitleText，所以标题栏用它而不是 title）
		map.insert(QStringLiteral("caption"), 12);      // 31 处 12px：#sessionStatsLine、#codeBlockLang、#tooltipText
		map.insert(QStringLiteral("small"), 11);        // 7 处 11px：chip 折角、badge、类别标签
		map.insert(QStringLiteral("glyph"), 12);        // main-window 窗口按钮字形 font-size: 12px
		map.insert(QStringLiteral("lead"), 16);         // chat #inputCapsule QPlainTextEdit、popups #popupTitle（3 处）
		map.insert(QStringLiteral("display"), 18);      // popups #initLabel（全项目唯一 18px）
		map.insert(QStringLiteral("weightBold"), 600);  // 11 处
		map.insert(QStringLiteral("weightMedium"), 500); // 6 处
		return map;
	}

	QVariantMap defaultSpacing()
	{
		QVariantMap map;
		map.insert(QStringLiteral("windowMargin"), 24);        // src/ui/Main.cpp kWindowMargin
		map.insert(QStringLiteral("columnClearance"), 16);     // kColumnSideClearance
		map.insert(QStringLiteral("panelGap"), 2);             // kPanelGap
		map.insert(QStringLiteral("contentSideInset"), 9);     // kContentSideInset
		map.insert(QStringLiteral("cardPadding"), 16);         // QML 侧取值（旧壳最接近的是 extension-manager #extPopupList 8px）
		map.insert(QStringLiteral("sectionGap"), 12);          // ChatInputWidget.cpp kCardGap
		map.insert(QStringLiteral("buttonGap"), 6);            // kRowBottomPadding / Tooltip.cpp kGap
		map.insert(QStringLiteral("buttonPadY"), 8);           // QSS padding: 8px 14px（8 处）
		map.insert(QStringLiteral("buttonPadX"), 14);
		map.insert(QStringLiteral("fieldPadY"), 6);            // QSS padding: 6px 10px（6 处）
		map.insert(QStringLiteral("fieldPadX"), 10);
		map.insert(QStringLiteral("fieldPadWideY"), 8);        // QSS padding: 8px 12px（4 处）
		map.insert(QStringLiteral("fieldPadWideX"), 12);
		map.insert(QStringLiteral("labelGap"), 8);             // 单选/复选行、#toolsFilterToolRow 的 spacing
		map.insert(QStringLiteral("editorPadTop"), 4);         // ChatInputWidget.cpp kEditorVerticalPadding
		return map;
	}

	QVariantMap defaultSize()
	{
		QVariantMap map;
		map.insert(QStringLiteral("sidebarWidth"), 240);            // Main.cpp kSidebarWidth
		// 右侧栏（面板坞）宽度。⚠️ 新 token 必须**先在这里**登记：metrics.json 只是覆盖层，
		// 未知键会被 overlayGroup 直接忽略 —— 只加 metrics.json 的话运行时读到 undefined，
		// 表现是"面板不出现、压窄量算成 NaN"（踩过一次，日志/布局都正常但界面毫无反应）。
		map.insert(QStringLiteral("rightSidebarWidth"), 300);
		// 右侧栏拖拽下限：再窄标题和关闭键就挤在一起了；上限由 Shell 按"会话区最小宽度"现算
		map.insert(QStringLiteral("rightSidebarMinWidth"), 260);
		map.insert(QStringLiteral("titleBarHeight"), 42);           // TitleBar::kHeight
		map.insert(QStringLiteral("titleBarPadLeft"), 14);          // TitleBar.cpp:56 布局左边距
		map.insert(QStringLiteral("windowButtonWidth"), 46);        // kButtonWidth
		map.insert(QStringLiteral("windowButtonHeight"), 32);       // kButtonHeight
		map.insert(QStringLiteral("windowButtonTopMargin"), 5);     // kButtonTopMargin（给缩放热区让位）
		map.insert(QStringLiteral("windowButtonRightMargin"), 6);   // kButtonRightMargin
		map.insert(QStringLiteral("logoHeight"), 20);               // kLogoHeight
		map.insert(QStringLiteral("logoTextGap"), 8);               // kLogoTextGap
		map.insert(QStringLiteral("statsLineHeight"), 14);          // SessionStatsLine::kHeight
		map.insert(QStringLiteral("minWindowWidth"), 720);          // QML 侧取值（旧壳 setMinimumWidth 是算出来的）
		map.insert(QStringLiteral("minWindowHeight"), 480);         // Main.cpp:59 qBound 下限
		map.insert(QStringLiteral("preferredWindowWidth"), 1440);   // kPreferredWindowWidth
		map.insert(QStringLiteral("preferredWindowHeight"), 880);   // kPreferredWindowHeight（另加标题栏高度）
		map.insert(QStringLiteral("minConversationWidth"), 320);    // kMinConversationColumnWidth
		map.insert(QStringLiteral("sendButton"), 32);               // chat #sendButton 四边 32px
		map.insert(QStringLiteral("selectorChipHeight"), 28);       // ModelSelector.cpp kChipHeight
		map.insert(QStringLiteral("scrollbarThickness"), 8);        // scrollbars.qss QScrollBar 宽/高
		map.insert(QStringLiteral("scrollbarHandleMin"), 20);       // scrollbars.qss ::handle min-height
		map.insert(QStringLiteral("editorMinHeight"), 40);          // ChatInputWidget.cpp kEditorMinHeight
		map.insert(QStringLiteral("editorMaxHeight"), 336);         // kEditorMaxHeight
		map.insert(QStringLiteral("dropdownArrowWidth"), 18);       // #settingsLanguageCombo::drop-down 宽度
		map.insert(QStringLiteral("comboMinWidth"), 200);           // #settingsLanguageCombo min-width
		map.insert(QStringLiteral("checkbox"), 18);                 // chat #interactionPanel QCheckBox::indicator
		map.insert(QStringLiteral("checkboxCompact"), 16);          // model-list #modelListField::indicator（topbar 还有第三套 14px，建议收敛到 16，不为它建键）
		map.insert(QStringLiteral("radioIndicator"), 18);           // chat QRadioButton::indicator（与 radius.dot 配套）
		map.insert(QStringLiteral("listItemMinHeight"), 34);        // settings #agentPresetList::item
		map.insert(QStringLiteral("entryHeaderMinHeight"), 48);     // ModelListPanel.cpp kEntryHeaderMinHeight
		map.insert(QStringLiteral("directoryHeaderMinHeight"), 44); // TopBar.cpp kDirectoryHeaderMinHeight（同为卡片表头却 44 vs 48）
		map.insert(QStringLiteral("switchTrackWidth"), 34);         // TopBar.cpp kTrackWidth
		map.insert(QStringLiteral("switchTrackHeight"), 18);        // kTrackHeight
		map.insert(QStringLiteral("initCardWidth"), 400);           // Main.cpp:229 initCard 400x240
		map.insert(QStringLiteral("initCardHeight"), 240);
		map.insert(QStringLiteral("tooltipMaxWidth"), 360);         // Tooltip.cpp kMaxTextWidth
		map.insert(QStringLiteral("menuMinWidth"), 300);            // ModelSelector.cpp kMenuWidth
		map.insert(QStringLiteral("menuFallbackWidth"), 220);       // ModelListPanel.cpp kMenuFallbackWidth
		return map;
	}

	QVariantMap defaultShadow()
	{
		QVariantMap map;
		map.insert(QStringLiteral("cardBlur"), 8);        // Main.cpp kSidebarShadow/kTopBarShadow = CardShadow::Spec{8, 2, 12}
		map.insert(QStringLiteral("cardDy"), 2);
		map.insert(QStringLiteral("cardRadius"), 12);     // 必须等于 radius.panel（Main.cpp:134/176 setRadius(12)）
		map.insert(QStringLiteral("inputBlur"), 12);      // ChatInputWidget::shadowSpec() = Spec{12, 3, 22}
		map.insert(QStringLiteral("inputDy"), 3);
		map.insert(QStringLiteral("inputRadius"), 22);    // 必须等于 radius.composer
		map.insert(QStringLiteral("floatBlur"), 24);      // CardShadow.h level3() = Spec{24, 8, 14}
		map.insert(QStringLiteral("floatDy"), 8);
		map.insert(QStringLiteral("floatRadius"), 14);    // ⚠ 与 radius.float(20) 不同：旧壳靠 Main.cpp:273 setRadius(20) 硬纠正 #initCard 的圆角
		map.insert(QStringLiteral("topBarGap"), 8);       // Main.cpp kTopBarShadowGap
		return map;
	}

	QVariantMap defaultMotion()
	{
		QVariantMap map;
		map.insert(QStringLiteral("fast"), 120);      // 旧壳最近锚点：Tooltip.cpp kFadeMs 110
		map.insert(QStringLiteral("normal"), 180);    // 旧壳无对应常量（QSS 无 transition）
		map.insert(QStringLiteral("slow"), 260);      // SmoothWheelScroller.cpp 的 80/220 是滚动动画，不是通用档
		return map;
	}

	// 把 metrics.json 里同名分组的值覆盖进默认表：只认已知的组与已知的键，未知项忽略（前向兼容）
	void overlayGroup(QVariantMap& target, const QJsonObject& root, const QString& group)
	{
		const QJsonValue value = root.value(group);
		if (!value.isObject())
			return;

		const QJsonObject object = value.toObject();
		for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
			if (!target.contains(it.key()))
				continue;   // 不认识就忽略，别把垃圾塞进 token 表
			if (it.value().isDouble())
				target.insert(it.key(), it.value().toDouble());
			else if (it.value().isString())
				target.insert(it.key(), it.value().toString());
		}
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

	m_radius = defaultRadius();  // 尺寸/圆角/字号/间距/阴影/动效：内置默认值 + metrics.json 覆盖
	m_font = defaultFont();
	m_spacing = defaultSpacing();
	m_size = defaultSize();
	m_shadow = defaultShadow();
	m_motion = defaultMotion();

	const QString path = m_stylesDir + QStringLiteral("/metrics.json");
	QFile file(path);
	if (file.open(QIODevice::ReadOnly)) {
		QJsonParseError parseError{};
		const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
		if (parseError.error == QJsonParseError::NoError && doc.isObject()) {
			const QJsonObject root = doc.object();
			overlayGroup(m_radius, root, QStringLiteral("radius"));
			overlayGroup(m_font, root, QStringLiteral("font"));
			overlayGroup(m_spacing, root, QStringLiteral("spacing"));
			overlayGroup(m_size, root, QStringLiteral("size"));
			overlayGroup(m_shadow, root, QStringLiteral("shadow"));
			overlayGroup(m_motion, root, QStringLiteral("motion"));
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
