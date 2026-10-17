// ULin Hub 启动装配（M8：旧控件壳已删除，只剩 QML 壳）。
//
// 顺序：日志 → 外观设置 → 翻译 → 主题（释放风格包 + 读 palette）→ 原生窗口过滤器 →
//       风格包/QML token → 会话数据面 → 注册 QML 单例 → 装载 qrc:/qml/Main.qml。

#include "common/settings/ClientSettings.h"
#include "common/util/Logger.h"
#include "common/appearance/ThemeManager.h"
#include "common/util/ConnectionManager.h"
#include "common/util/CommonRegistry.h"
#include "common/util/HostIndex.h"
#include "ExtensionSystem/HostUi.h"
#include "ExtensionSystem/ClientExtension.h"
#include "common/appearance/TranslationManager.h"
#include "common/platform/win/NativeWindowFilter.h"
#include "common/PluginMarket/PluginMarketController.h"
#include "common/appearance/StyleResolver.h"
#include "common/session/SessionListModel.h"
#include "common/settings/SettingsController.h"
#include "common/shell/PanelDockModel.h"
#include "qmlbridge/PanelBarLayout.h"
#include "qmlbridge/UrlInputBridge.h"
#include "common/session/ToolsFilter.h"
#include "qmlbridge/QmlBridge.h"
#include "qmlbridge/TerminalBridge.h"
#include "qmlbridge/TerminalVtModel.h"
#include "qmlbridge/ThemeBridge.h"
#include "qmlbridge/WindowBridge.h"

#include <QCoreApplication>
#include <QFont>
#include <QQmlApplicationEngine>
#include <QStyleHints>
#include <QUrl>
#include <QtQml/qqml.h>
#include <QtWebEngineQuick/QtWebEngineQuick>
#include <QtWidgets/QApplication>

int main(int argc, char* argv[])
{
	// ⚠️ 必须在 QApplication **之前**：WebEngine 要在这里设置与主上下文共享的 OpenGL 上下文，
	//    放到后面（或漏掉）会在运行期报 "WebEngineContext used before initialize" 并直接崩。
	QtWebEngineQuick::initialize();

	QApplication app(argc, argv);

	Logger::init();
	TimingLogger::mark(QStringLiteral("Logger init (baseline)"));

	CommonRegistry::instance().AddToRegistry(DshHostIndex::kConnectionManager, &ConnectionManager::instance());
	// 扩展 UI 宿主：扩展"拿到宿主对象直接注入"的入口（VirtualUiHost，QML 侧是单例 HostUi）
	CommonRegistry::instance().AddToRegistry(DshHostIndex::kUiHost, &HostUi::instance());
	TimingLogger::mark(QStringLiteral("connection manager init"));

	AppearanceSetting::ensureFile();
	Translation::init();
	TimingLogger::mark(QStringLiteral("translation init"));

	const AppearanceSetting::ThemeMode savedThemeMode = AppearanceSetting::themeMode();
	const bool dark = savedThemeMode == AppearanceSetting::ThemeMode::System
		? app.styleHints()->colorScheme() == Qt::ColorScheme::Dark
		: savedThemeMode == AppearanceSetting::ThemeMode::Dark;

	// 界面副本目录：<exe>/ui（新的界面目录；旧的 <exe>/styles 已退役）
	const QString stylesDir = QCoreApplication::applicationDirPath() + QStringLiteral("/ui");
	ThemeManager::instance().init(stylesDir, dark ? ThemeManager::Mode::Dark : ThemeManager::Mode::Light);
	TimingLogger::mark(QStringLiteral("theme styles init"));

	QFont font(QStringLiteral("Microsoft YaHei UI"));
	font.setPointSize(10);
	app.setFont(font);

	QObject::connect(&app, &QCoreApplication::aboutToQuit, &app, []() {
		HostUi::instance().unmountAll();   // 先摘扩展注入项，再让插件退场
		ClientExtension::shutdownForExit();
		});

	NativeWindowFilter::instance().install();
	StyleResolver::instance().init(stylesDir);
	ThemeBridge::instance().init(stylesDir);
	TimingLogger::mark(QStringLiteral("style pack ready"));

	QmlBridge::instance().init();
	TimingLogger::mark(QStringLiteral("qml bridge ready"));

	qmlRegisterSingletonInstance("ULinHub", 1, 0, "WindowBridge", &WindowBridge::instance());
	qmlRegisterSingletonInstance("ULinHub", 1, 0, "Theme", &ThemeBridge::instance());
	qmlRegisterSingletonInstance("ULinHub", 1, 0, "Style", &StyleResolver::instance());
	qmlRegisterSingletonInstance("ULinHub", 1, 0, "Hub", &QmlBridge::instance());   // QML 单例名仍是 Hub（契约）
	// 统一连接登记表：QML 侧用 HostConnections.RegisterConnection 由宿主建连（不用自己写 Connections）
	// 名字不叫 Connections —— 那是 QtQuick 里的类型名，会撞。
	qmlRegisterSingletonInstance("ULinHub", 1, 0, "HostConnections", &ConnectionManager::instance());
	// 扩展 UI 宿主（QML 面）：HostUi.registerItem / item / mountInto / unmountOwner
	qmlRegisterSingletonInstance("ULinHub", 1, 0, "HostUi", &HostUi::instance());
	// 地址栏输入规范化（纯字符串规则在 common/util/UrlInput）：QML 侧 UrlInput.normalize(text)
	qmlRegisterSingletonInstance("ULinHub", 1, 0, "UrlInput", &UrlInputBridge::instance());
	// 右栏面板坞的块（页签）列表：QML 侧 Dock.open/close/activate/showHome + Repeater { model: Dock }
	qmlRegisterSingletonInstance("ULinHub", 1, 0, "Dock", &PanelDockModel::instance());
	// 面板坞的运行时能力：本构建带 WebEngine（上面已 QtWebEngineQuick::initialize()，vcxproj 也列了
	// webenginequick）⇒ 网页/终端块给真视图。缺了它这两个块会降级成说明页，而不是去建 WebEngineView 崩掉。
	PanelDockModel::instance().setWebEngineAvailable(true);
	// 右侧栏宽度的钳制策略（纯数值规则）：QML 侧 PanelBarLayout.widthFromDrag(...) 等
	qmlRegisterSingletonInstance("ULinHub", 1, 0, "PanelBarLayout", &PanelBarLayout::instance());
	// 右栏「终端」块的接线层：QML 侧 Terminal.bindView(blockId)/closeBlock(...) 拿会话与网格模型。
	// init(api) 已在 QmlBridge::init() 里调过（出站 RPC 只走那一个 DshApiClient）
	qmlRegisterSingletonInstance("ULinHub", 1, 0, "Terminal", &TerminalBridge::instance());
	// 终端网格模型（TerminalVtModel）：QML 侧由 Terminal.bindView(blockId) 提供，
	// 视图（TerminalView.qml）只读它的属性/调它的 INvokable —— 登记类型是为了属性有静态类型可查。
	qmlRegisterUncreatableType<TerminalVtModel>("ULinHub", 1, 0, "TerminalVtModel",
		QStringLiteral("由 Terminal.bindView(blockId) 提供，不要在 QML 里创建"));
	// 工具过滤器（data + 摊平的行模型）：QML 侧 Hub.toolsFilter 就是这个对象
	qmlRegisterUncreatableType<ToolsFilter>("ULinHub", 1, 0, "ToolsFilter",
		QStringLiteral("由 Hub.toolsFilter 提供，不要在 QML 里创建"));
	qmlRegisterUncreatableType<SessionListModel>("ULinHub", 1, 0, "SessionListModel",
		QStringLiteral("由 Hub.sessions 提供，不要在 QML 里创建"));
	qmlRegisterUncreatableType<SettingsController>("ULinHub", 1, 0, "SettingsController",
		QStringLiteral("由 Hub.settings 提供，不要在 QML 里创建"));
	qmlRegisterUncreatableType<PluginMarketController>("ULinHub", 1, 0, "PluginMarketController",
		QStringLiteral("由 Hub.plugins 提供，不要在 QML 里创建"));
	qmlRegisterUncreatableType<ExtensionManagerController>("ULinHub", 1, 0, "ExtensionManagerController",
		QStringLiteral("由 Hub.extensions 提供，不要在 QML 里创建"));

	const QString mainQml = StyleResolver::instance().url(QStringLiteral("Main.qml"));

	QQmlApplicationEngine engine;

	// 扩展自带的 QML：把每个已安装扩展的目录加进 import path —— 扩展包里的 ui/ 因此可以
	// import 自己的兄弟组件与子目录（纯相对 import 本来就行；加上它是为了能用带 qmldir 的模块式目录）。
	// 注意这是引擎级全局路径：扩展目录名会进入所有 QML 的解析范围（扩展之间以及宿主都能看见）。
	{
		const QString extensionRoot = ClientExtension::extensionDirectory();
		const QStringList installedExtensions = ClientExtension::installedNames();
		for (const QString& name : installedExtensions)
			engine.addImportPath(extensionRoot + QLatin1Char('/') + name);
		if (!installedExtensions.isEmpty())
			TimingLogger::mark(QStringLiteral("extension import paths: %1")
								   .arg(installedExtensions.join(QLatin1Char(','))));
	}

	engine.load(QUrl(mainQml));
	if (engine.rootObjects().isEmpty()) {
		qCritical().noquote()
			<< "[QML] Main.qml 加载失败 —— 先确认 QML 模块可解析（开发机可设 "
			   "QML_IMPORT_PATH=<Qt>/qml，发行版需要把 Qt 的 qml/ 目录部署到 exe 同目录）";
		return -1;
	}

	TimingLogger::mark(QStringLiteral("qml shell ready"));

	// 启动装载客户端扩展（DLL 扩展的运行入口）。此前只有退出路径调过 ClientExtension，
	// 启动路径漏了 ⇒ 装了 DLL 扩展也不会被装载（纯 QML 扩展的装载随后由扩展层负责）。
	// 位置刻意在 engine.load() **之后**：DLL 的 attachHost() 要用的宿主对象（主题 / 多语言 /
	// 连接注册表，以及将来的 kUiHost）到这时才齐；放在 load() 之前会让插件拿到空对象。
	{
		const QStringList loadedExtensions = ClientExtension::loadAll();
		if (loadedExtensions.isEmpty())
			TimingLogger::mark(QStringLiteral("client extensions: none"));
		else
			TimingLogger::mark(QStringLiteral("client extensions loaded: %1")
								   .arg(loadedExtensions.join(QLatin1Char(','))));
	}

	return app.exec();
}