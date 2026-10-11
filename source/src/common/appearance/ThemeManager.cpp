#include "common/appearance/ThemeManager.h"

#include "common/appearance/UiAbi.h"   // kStylePackUiAbi：旧 ABI 的副本按陈旧处理
#include "common/appearance/CardShadow.h"
#include "common/settings/ClientSettings.h"
#include "common/util/CommonRegistry.h"
// (M8) removed: #include "core/DSHHub.h"
#include "common/util/HostIndex.h"
// (M8) removed: #include "ui/ShadowPanel.h"
// (M8) removed: #include "ui/SpinnerWidget.h"
#include <QAbstractScrollArea>
#include <QApplication>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QEvent>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QList>
#include <QObject>
#include <QPalette>
#include <QRegularExpression>
#include <QResource>
#include <QScreen>
#include <QScrollBar>
#include <QStyle>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>
#include <future>
#include <utility>

// 常量走函数式 static，避免静态初始化期构造 QString（Qt 老坑）
QString ThemeManager::modeKey(Mode mode)
{
	return mode == Mode::Dark ? QStringLiteral("dark") : QStringLiteral("light");
}

QString ThemeManager::token(const QHash<QString, QString>& palette, const QString& key, const QString& fallback)
{
	const auto it = palette.constFind(key);
	return it == palette.constEnd() ? fallback : it.value();
}

// 把色板落成 QPalette（未覆盖的文字/底色在暗色下会退化成 Qt 亮色）；换全局调色板有副作用，
// 故留在 private，外部只能经 setMode()/reload() 间接触发
void ThemeManager::installPaletteFor(const QHash<QString, QString>& palette)
{
	QPalette pal = QApplication::palette();
	// 色值可能是 CSS rgba(...)，QColor 认不出；一律走 CardShadow::parseColor
	const QColor window(CardShadow::parseColor(token(palette, QStringLiteral("windowBg"), QStringLiteral("#FFFFFF"))));
	const QColor panel(CardShadow::parseColor(token(palette, QStringLiteral("panelBg"), QStringLiteral("#FFFFFF"))));
	const QColor text(CardShadow::parseColor(token(palette, QStringLiteral("textPrimary"), QStringLiteral("#000000"))));
	const QColor textDim(CardShadow::parseColor(token(palette, QStringLiteral("textSecondary"), QStringLiteral("#666666"))));
	const QColor accent(CardShadow::parseColor(token(palette, QStringLiteral("accent"), QStringLiteral("#4C8BF5"))));
	const QColor onAccent(CardShadow::parseColor(token(palette, QStringLiteral("textOnAccent"), QStringLiteral("#FFFFFF"))));
	const QColor border(CardShadow::parseColor(token(palette, QStringLiteral("border"), QStringLiteral("#E5E7EB"))));

	const auto apply = [&pal](QPalette::ColorRole role, const QColor& color) {
		// 显式给全部组上色：仅 Disabled 组亮化一档，其余组同色
		for (int i = 0; i < QPalette::NColorGroups; ++i) {
			pal.setColor(static_cast<QPalette::ColorGroup>(i), role,
				i == QPalette::Disabled ? color.lighter(150) : color);
		}
	};
	apply(QPalette::Window, window);
	apply(QPalette::WindowText, text);
	apply(QPalette::Base, panel);
	apply(QPalette::AlternateBase, border);
	apply(QPalette::Text, text);
	apply(QPalette::Button, window);
	apply(QPalette::ButtonText, text);
	apply(QPalette::PlaceholderText, textDim);
	apply(QPalette::Highlight, accent);
	apply(QPalette::HighlightedText, onAccent);
	apply(QPalette::ToolTipBase, panel);
	apply(QPalette::ToolTipText, text);

	if (qApp)
		qApp->setPalette(pal);
}

ThemeManager& ThemeManager::instance()
{
	// 刻意不析构：已登记进 CommonRegistry，静态析构顺序无从保证
	static ThemeManager* const inst = new ThemeManager();
	return *inst;
}

ThemeManager::ThemeManager() = default;

ThemeManager::~ThemeManager() = default;

QString ThemeManager::paletteFileFor(Mode m) const
{
	return m == Mode::Dark
		? QStringLiteral("theme-dark.json")
		: QStringLiteral("theme-light.json");
}

// 外部优先、qrc 兜底
QHash<QString, QString> ThemeManager::loadPaletteFor(Mode m) const
{
	QHash<QString, QString> result;

	const QString fileName = paletteFileFor(m);
	const QByteArray raw = loadText(fileName).toUtf8();
	QJsonParseError parseError;
	const QJsonDocument doc = QJsonDocument::fromJson(raw, &parseError);
	if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
		qWarning().noquote() << "[Theme] palette parse error:" << fileName
			<< parseError.errorString();
		return result;
	}

	const QJsonObject obj = doc.object();
	for (auto it = obj.constBegin(); it != obj.constEnd(); ++it)
		result.insert(it.key(), it.value().toString());
	return result;
}

// 全局兜底：init 时装一次；新控件漏调 repolishScrollArea 也不会退回原生样式
namespace
{
	class ScrollAreaShowFilter : public QObject
	{
	public:
		using QObject::QObject;

	protected:
		bool eventFilter(QObject* watched, QEvent* event) override
		{
			if (event->type() == QEvent::Show) {
				if (auto* area = qobject_cast<QAbstractScrollArea*>(watched)) {
					// Show 派发期间重解析会搅乱事件流，故排到本轮之后；控件销毁后调用自动作废
					QMetaObject::invokeMethod(area, [area]() {
						ThemeManager::instance().repolishScrollArea(area);
						}, Qt::QueuedConnection);
				}
			}
			return QObject::eventFilter(watched, event);
		}
	};
} // namespace

// ---- QML 风格包（M8）----
// 以前这里释放的是各板块 *.qss 模板；现在对象换成 QML 风格包：qrc:/qml 整棵树 + theme-*.json。
// 规则不变：**按文件补缺，已存在的绝不覆盖**。
void ThemeManager::ensureStylePack()
{
	if (m_stylesDir.isEmpty()) {
		qWarning().noquote() << "[Theme] ensureStylePack before init, skipped";
		return;
	}

	// 2026-10-01 两处改动：
	//   ① 释放目录从 <exe>/styles 改成 <exe>/ui（用户在 <exe>/ui 里划定的新界面目录）；
	//   ② 规则从"缺什么补什么、绝不覆盖"改成**增量同步** —— 旧的 fill-only 规则会让内置界面的
	//      更新永远进不去副本（实测：重编之后点扩展按钮还是旧提示，只有按"重置"才刷新）。
	// 增量同步靠同目录的 release-manifest.json（相对路径 → 上次释放时的内容哈希）做三方比对：
	//   包内没有                             → 释放
	//   有、但清单没记过（旧副本/用户放的）    → 不动，把包内现状记为基线
	//   有、包内哈希 == 清单（用户没改过）
	//      且 内置哈希 != 清单（内置更新了）   → 覆盖
	//   其余（用户改过 / 内置没变）           → 不动
	static const QString kBuiltin = QStringLiteral(":/qml");
	QDir builtin(kBuiltin);
	if (!builtin.exists()) {
		qWarning().noquote() << "[Theme] 内置 QML 风格包缺失:" << kBuiltin;
		return;
	}

	const auto giveWritePermission = [](const QString& path) {
		QFile::setPermissions(path,
			QFileDevice::ReadOwner | QFileDevice::WriteOwner
			| QFileDevice::ReadUser | QFileDevice::WriteUser
			| QFileDevice::ReadGroup | QFileDevice::ReadOther);
	};
	const auto hashOf = [](const QString& path) -> QString {
		QFile file(path);
		if (!file.open(QIODevice::ReadOnly))
			return QString();
		return QString::fromLatin1(QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha1).toHex());
	};

	// 上次释放的基线
	// 包里还留着旧 ABI 的 pack.json ⇒ 那个副本已经不可用（StyleResolver 会整包拒载），
	// 直接当陈旧处理：下面所有内置文件都覆盖一遍，省掉"让用户手工删目录"。
	bool forceFresh = false;
	{
		QFile packFile(m_stylesDir + QStringLiteral("/pack.json"));
		if (packFile.open(QIODevice::ReadOnly)) {
			const int abi = QJsonDocument::fromJson(packFile.readAll()).object()
				.value(QStringLiteral("uiAbi")).toInt(-1);
			forceFresh = (abi != kStylePackUiAbi);
			if (forceFresh)
				qInfo().noquote() << "[Theme] 界面包 uiAbi=" << abi << "与本版本" << kStylePackUiAbi
					<< "不符，按陈旧处理并整份重放:" << m_stylesDir;
		}
	}

	const QString manifestPath = m_stylesDir + QStringLiteral("/release-manifest.json");
	QHash<QString, QString> baseline;
	{
		QFile file(manifestPath);
		if (file.open(QIODevice::ReadOnly)) {
			const QJsonObject files = QJsonDocument::fromJson(file.readAll()).object()
				.value(QStringLiteral("files")).toObject();
			for (auto it = files.constBegin(); it != files.constEnd(); ++it)
				baseline.insert(it.key(), it.value().toString());
		}
	}

	// 要释放的清单 = 内置树里的每个文件 + 两个调色板模板（不在 :/qml 前缀下，单独补）
	QList<QPair<QString, QString>> sources;   // (qrc 源, 包内相对路径)
	const int prefixLength = kBuiltin.length();
	QDirIterator it(kBuiltin, QDir::Files, QDirIterator::Subdirectories);
	while (it.hasNext()) {
		const QString source = it.next();
		sources.append({ source, source.mid(prefixLength + 1) });
	}
	for (const QString& palette : { QStringLiteral(":/DSHHub/styles/theme-light.json"),
									QStringLiteral(":/DSHHub/styles/theme-dark.json") }) {
		if (QFile::exists(palette))
			sources.append({ palette, QFileInfo(palette).fileName() });
	}

	int released = 0;
	int updated = 0;
	QHash<QString, QString> nextBaseline;
	for (const auto& pair : sources) {
		const QString& source = pair.first;
		const QString& relative = pair.second;
		const QString target = m_stylesDir + QLatin1Char('/') + relative;
		const QString builtinHash = hashOf(source);
		if (builtinHash.isEmpty())
			continue;

		if (!QFile::exists(target)) {
			QDir().mkpath(QFileInfo(target).absolutePath());
			if (QFile::copy(source, target)) {
				giveWritePermission(target);   // qrc 资源是只读的，这份要给用户改
				nextBaseline.insert(relative, builtinHash);
				++released;
			}
			else {
				qWarning().noquote() << "[Theme] 释放内置 QML 失败:" << relative;
			}
			continue;
		}

		if (forceFresh) {
			if (QFile::remove(target) && QFile::copy(source, target)) {
				giveWritePermission(target);
				nextBaseline.insert(relative, builtinHash);
				++updated;
			}
			continue;
		}

		const QString recorded = baseline.value(relative);
		const QString current = hashOf(target);
		if (recorded.isEmpty()) {
			// 没有基线（旧副本 / 用户手工放的文件）：不动它，把包内现状记为基线，
			// 这样以后再比时，只有内置真的变了才会覆盖。
			if (!current.isEmpty())
				nextBaseline.insert(relative, current);
			continue;
		}
		if (current == recorded && builtinHash != recorded) {
			// 用户没改过、内置更新了 → 覆盖（本次新增的规则）
			if (QFile::remove(target) && QFile::copy(source, target)) {
				giveWritePermission(target);
				nextBaseline.insert(relative, builtinHash);
				++updated;
			}
			else {
				qWarning().noquote() << "[Theme] 覆盖内置 QML 失败:" << relative;
				nextBaseline.insert(relative, recorded);
			}
			continue;
		}
		nextBaseline.insert(relative, recorded);  // 用户改过、或内置没变 → 保持不动，基线也不动
	}

	// 写回基线（即使这次什么都没释放也写：保证下次有得比）
	{
		QJsonObject files;
		for (auto it2 = nextBaseline.constBegin(); it2 != nextBaseline.constEnd(); ++it2) {
			if (!it2.value().isEmpty())
				files.insert(it2.key(), it2.value());
		}
		QJsonObject root;
		root.insert(QStringLiteral("files"), files);
		QFile out(manifestPath);
		if (out.open(QIODevice::WriteOnly | QIODevice::Truncate))
			out.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
	}

	if (released > 0 || updated > 0)
		qInfo().noquote() << "[Theme] 内置界面已同步到" << m_stylesDir
			<< "新释放" << released << "个，更新" << updated << "个";
}

void ThemeManager::resetStylePack()
{
	if (m_stylesDir.isEmpty()) {
		qWarning().noquote() << "[Theme] resetStylePack before init";
		return;
	}

	// 只删"内置包里有的"文件（用户自建的文件不动）
	static const QString kBuiltin = QStringLiteral(":/qml");
	const int prefixLength = kBuiltin.length();
	QDirIterator it(kBuiltin, QDir::Files, QDirIterator::Subdirectories);
	int removed = 0;
	while (it.hasNext()) {
		const QString target = m_stylesDir + QLatin1Char('/') + it.next().mid(prefixLength);
		if (QFile::exists(target) && QFile::remove(target))
			++removed;
	}
	for (const QString& name : { QStringLiteral("theme-light.json"), QStringLiteral("theme-dark.json") }) {
		const QString target = m_stylesDir + QLatin1Char('/') + name;
		if (QFile::exists(target) && QFile::remove(target))
			++removed;
	}

	QFile::remove(m_stylesDir + QStringLiteral("/release-manifest.json"));   // 基线一起清，下次整份重放
	ensureStylePack();
	reload();   // 会 emit styleChanged()，QML 侧热重载 token
	qInfo().noquote() << "[Theme] 风格包已重置，删除" << removed << "个已释放文件";
}

// (M8) QSS 合成都已删除：这里只取调色板（token 替换在 QML 运行时完成）。
QHash<QString, QString> ThemeManager::buildFor(Mode m) const { return loadPaletteFor(m); }

void ThemeManager::buildAllCaches()
{
	auto light = std::async(std::launch::async, [this]() { return buildFor(Mode::Light); });
	auto dark = std::async(std::launch::async, [this]() { return buildFor(Mode::Dark); });
	m_paletteCache.insert(modeKey(Mode::Light), light.get());
	m_paletteCache.insert(modeKey(Mode::Dark), dark.get());
}

void ThemeManager::ensureCached(Mode m)
{
	const QString key = modeKey(m);
	if (m_paletteCache.contains(key))
		return;
	m_paletteCache.insert(key, buildFor(m));
}

void ThemeManager::activate(Mode m)
{
	m_mode = m;
	m_palette = m_paletteCache.value(modeKey(m));
}

// (M8) 只用来读风格包目录里的文本（调色板 JSON）；QSS 文本读取已随 QSS 合成一起删除。
QString ThemeManager::loadText(const QString& fileName) const
{
	if (m_stylesDir.isEmpty() || fileName.isEmpty())
		return QString();

	QFile file(m_stylesDir + QLatin1Char('/') + fileName);
	if (!file.open(QIODevice::ReadOnly))
		return QString();
	return QString::fromUtf8(file.readAll());
}

// (M8) QSS 时代"把合成样式表挂到窗口"的入口。QML 壳不用 QSS：这里保留为无操作桩，
// 只为不破坏 VirtualTheme::ExternalApplyToWindow 的既有签名（插件仍可能调它）。
void ThemeManager::applyToWindow(QWidget* window)
{
	Q_UNUSED(window);
}

// (M8) 同上：QSS 时代滚动区需要重新 polish 才会吃到样式；QML 侧没有 QSS，无操作。
void ThemeManager::repolishScrollArea(QWidget* widget)
{
	Q_UNUSED(widget);
}

void ThemeManager::init(const QString& stylesDir, Mode mode)
{
	m_stylesDir = stylesDir;
	ensureStylePack();   // (M8) 释放对象从 QSS 模板换成 QML 风格包

	buildAllCaches();

	setMode(mode);

	static bool showFilterInstalled = false;
	if (!showFilterInstalled && qApp) {
		showFilterInstalled = true;
		qApp->installEventFilter(new ScrollAreaShowFilter(qApp));
	}

	// 登记进全局注册表（插件按 index 取出后转 VirtualTheme*）；必须放最后一步，否则插件会被空样式表闸门挡回
	CommonRegistry::instance().AddToRegistry(DshHostIndex::kThemeManager, this);

	qInfo().noquote() << "[Theme] styles initialized from:" << m_stylesDir
		<< "mode=" << (mode == Mode::Dark ? "Dark" : "Light")
		<< "paletteKeys=" << m_palette.size();
}

void ThemeManager::setMode(Mode mode)
{
	ensureCached(mode);
	activate(mode);
	installPaletteFor(m_palette);
	qInfo().noquote() << "[Theme] set mode:" << (m_mode == Mode::Dark ? "Dark" : "Light");
}

// 把合成样式表装到单个窗口及其子树；各顶层窗口自行调用
void ThemeManager::reload()
{
	buildAllCaches();
	activate(m_mode);
	installPaletteFor(m_palette);

	const QList<QWidget*> topLevels = QApplication::topLevelWidgets();
	for (QWidget* window : topLevels)
		applyToWindow(window);
	qInfo().noquote() << "[Theme] styles reloaded from:" << m_stylesDir;
}

bool ThemeManager::isDark() const
{
	return m_mode == Mode::Dark;
}

QString ThemeManager::color(const QString& key) const
{
	const auto it = m_palette.constFind(key);
	if (it != m_palette.constEnd())
		return it.value();
	return QStringLiteral("#000000");
}

QString ThemeManager::windowBg() const { return color(QStringLiteral("windowBg")); }
QString ThemeManager::border() const { return color(QStringLiteral("border")); }
QString ThemeManager::textPrimary() const { return color(QStringLiteral("textPrimary")); }
QString ThemeManager::textSecondary() const { return color(QStringLiteral("textSecondary")); }
QString ThemeManager::inputBg() const { return color(QStringLiteral("inputBg")); }
QString ThemeManager::accent() const { return color(QStringLiteral("accent")); }

// 滚动条建在基类构造期，那时 objectName 未设，匹配不到的 QSS 规则会被 QStyleSheetStyle 缓存下来；
// 故挂两个只跑一次的补丁：首次 Show 后、首次真有范围时各重解析一次
namespace
{
	class ScrollBarRepolisher : public QObject
	{
	public:
		explicit ScrollBarRepolisher(QAbstractScrollArea* area)
			: QObject(area)
			, m_area(area)
		{
			area->installEventFilter(this);
			for (QScrollBar* bar : bars()) {
				if (!bar)
					continue;
				m_connections.append(connect(bar, &QScrollBar::rangeChanged, this, [this, bar]() {
					if (m_ranged || bar->maximum() <= bar->minimum())
						return;
					m_ranged = true;
					ThemeManager::instance().repolishScrollArea(m_area);
					retireIfDone();
				}));
			}
		}

	protected:
		bool eventFilter(QObject* watched, QEvent* event) override
		{
			if (event->type() == QEvent::Show && !m_shown) {
				m_shown = true;
				// Show 期间重解析会搅乱事件流，挪到本轮事件之后
				QMetaObject::invokeMethod(this, [this]() {
					ThemeManager::instance().repolishScrollArea(m_area);
					retireIfDone();
				}, Qt::QueuedConnection);
			}
			return QObject::eventFilter(watched, event);
		}

	private:
		QList<QScrollBar*> bars() const
		{
			return { m_area->horizontalScrollBar(), m_area->verticalScrollBar() };
		}

		void retireIfDone()
		{
			if (!m_shown || !m_ranged)
				return;
			for (const QMetaObject::Connection& connection : m_connections)
				disconnect(connection);
			m_connections.clear();
			m_area->removeEventFilter(this);
			deleteLater();
		}

		QAbstractScrollArea* m_area = nullptr;
		QList<QMetaObject::Connection> m_connections;
		bool m_shown = false;
		bool m_ranged = false;
	};
} // namespace

// (M8) 旧壳已删：这里原本是 [过渡卡片 -> 重建主窗口] 的控件壳做法。QML 壳不重建窗口，
// 切模式后由 ThemeBridge::reload() 热重载 token（过渡卡片是 QML 的 ThemeSwitchOverlay.qml）。
// 保留该入口只为不改头文件签名。
void ThemeManager::switchTheme(QWidget* currentWindow)
{
	Q_UNUSED(currentWindow);

	const Mode nextMode = isDark() ? Mode::Light : Mode::Dark;
	AppearanceSetting::setThemeMode(nextMode == Mode::Dark ? AppearanceSetting::ThemeMode::Dark
		: AppearanceSetting::ThemeMode::Light);
	setMode(nextMode);
}
