// Linux 平台层：程序入口。
// 顺序与 Windows 版一致：日志 → 外观设置 → 翻译 → 主题 → 风格包/QML token
// → 注册 QML 单例 → 装载 qrc:/qml/Main.qml → 启动客户端扩展（.so）。
// 差异：无 .ico/.rc、无 dependence 私有程序集、无 QtWebEngineProcess.exe 兜底、
// 无 NativeWindowFilter（自绘标题栏由 WindowBridge 用 Qt6 系统移动/缩放接管）。

#include "common/settings/ClientSettings.h"
#include "common/ClientInfo.h"
#include "common/util/Logger.h"
#include "common/util/EnvPath.h"
#include "common/util/RuntimeDirs.h"
#include "common/util/WebEngineEnv.h"
#include "common/appearance/ThemeManager.h"
#include "common/util/ConnectionManager.h"
#include "common/util/CommonRegistry.h"
#include "common/util/HostIndex.h"
#include "ExtensionSystem/HostUi.h"
#include "ExtensionSystem/ClientExtension.h"
#include "common/appearance/TranslationManager.h"
#include "common/agent/dsh/pluginmarket/PluginMarketController.h"
#include "common/appearance/StyleResolver.h"
#include "common/session/SessionListModel.h"
#include "common/settings/SettingsController.h"
#include "common/shell/PanelBarDockModel.h"
#include "qmlbridge/FileBrowserBridge.h"
#include "qmlbridge/UrlInputBridge.h"
#include "common/agent/ToolsFilter.h"
#include "qmlbridge/QmlBridge.h"
#include "qmlbridge/ExtensionsBridge.h"
#include "qmlbridge/TerminalBridge.h"
#include "qmlbridge/TerminalVtModel.h"
#include "qmlbridge/ThemeBridge.h"
#include "qmlbridge/WindowBridge.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QStyleHints>
#include <QUrl>
#include <QtQml/qqml.h>
#include <QtWebEngineQuick/QtWebEngineQuick>
#include <QtWidgets/QApplication>

int main(int argc, char* argv[])
{
	// WebEngine 资源目录：Qt 只找 <exe>/resources/，所以指到 resources/webengine/。
	// 收进 util/WebEngineEnv.h，和 Windows 入口同一份实现（含路径编码处理）。
	if (argc > 0)
		applyBundledWebEngineEnv(QFileInfo(QString::fromLocal8Bit(argv[0])).absolutePath());

	// 必须在 QApplication 之前：WebEngine 要在这里建立共享的 OpenGL 上下文。
	QtWebEngineQuick::initialize();

	QApplication app(argc, argv);

	// 本地数据契约（<exe>/data 持久化、<exe>/temp/run-<uuid> 临时化）收进 util/RuntimeDirs.h。
	(void)prepareRuntimeDirs();

	QCoreApplication::setApplicationVersion(ClientInfo::kVersion);

	Logger::init();   // 日志先起：后续每一步都往时间线上打点
	TimingLogger::mark(QStringLiteral("Logger init (baseline)"));

	// 连接层与 UI 宿主先登记进 CommonRegistry：扩展通过 HostConnections / HostUi 桥按 index 取到它们
	CommonRegistry::instance().AddToRegistry(DshHostIndex::kConnectionManager, &ConnectionManager::instance());
	CommonRegistry::instance().AddToRegistry(DshHostIndex::kUiHost, &HostUi::instance());

	AppearanceSetting::ensureFile();
	Translation::init();

	// ThemeMode::System 时按系统配色走，其余模式按显式选择
	const AppearanceSetting::ThemeMode savedThemeMode = AppearanceSetting::themeMode();
	const bool dark = savedThemeMode == AppearanceSetting::ThemeMode::System
		? app.styleHints()->colorScheme() == Qt::ColorScheme::Dark
		: savedThemeMode == AppearanceSetting::ThemeMode::Dark;

	const QString stylesDir = QCoreApplication::applicationDirPath()
		+ QStringLiteral("/resources/ui");
	ThemeManager::instance().init(stylesDir, dark ? ThemeManager::Mode::Dark : ThemeManager::Mode::Light);

	// 字体不写死：QML 侧用 Theme.font.uiFamily（运行期挑选系统 UI 字体）。
	QObject::connect(&app, &QCoreApplication::aboutToQuit, &app, []() {
		HostUi::instance().unmountAll();
		ClientExtension::shutdownForExit();
	});

	StyleResolver::instance().init(stylesDir);
	ThemeBridge::instance().init(stylesDir);

	QmlBridge::instance().init();

	// QML 单例契约：各平台同名同义（见 Windows 版 WindowBridge.h 头注）。
	qmlRegisterSingletonInstance("ULinHub", 1, 0, "WindowBridge", &WindowBridge::instance());
	qmlRegisterSingletonInstance("ULinHub", 1, 0, "Theme", &ThemeBridge::instance());
	qmlRegisterSingletonInstance("ULinHub", 1, 0, "Style", &StyleBridge::instance());
	qmlRegisterSingletonInstance("ULinHub", 1, 0, "Hub", &QmlBridge::instance());
	qmlRegisterSingletonInstance("ULinHub", 1, 0, "HostConnections", &HostConnectionsBridge::instance());
	qmlRegisterSingletonInstance("ULinHub", 1, 0, "HostUi", &HostUiBridge::instance());
	qmlRegisterSingletonInstance("ULinHub", 1, 0, "UrlInput", &UrlInputBridge::instance());
	qmlRegisterSingletonInstance("ULinHub", 1, 0, "PanelBarDock", &PanelBarDockBridge::instance());
	CommonRegistry::instance().AddToRegistry(DshHostIndex::kPanelBarDock, &PanelBarDockModel::instance());

	PanelBarDockModel::instance().setWebEngineAvailable(true);   // 上面已初始化 WebEngine，面板坞的网页块可用

	qmlRegisterSingletonInstance("ULinHub", 1, 0, "Terminal", &TerminalBridge::instance());
	qmlRegisterSingletonInstance("ULinHub", 1, 0, "FileBrowser", &FileBrowserBridge::instance());

	qmlRegisterUncreatableType<TerminalVtModel>("ULinHub", 1, 0, "TerminalVtModel",
		QStringLiteral("由 Terminal.bindView(blockId) 提供，不要在 QML 里创建"));
	qmlRegisterUncreatableType<ToolsFilterBridge>("ULinHub", 1, 0, "ToolsFilterBridge",
		QStringLiteral("由 Hub.toolsFilter 提供，不要在 QML 里创建"));
	qmlRegisterUncreatableType<SessionsBridge>("ULinHub", 1, 0, "SessionsBridge",
		QStringLiteral("由 Hub.sessions 提供，不要在 QML 里创建"));
	qmlRegisterUncreatableType<SettingsBridge>("ULinHub", 1, 0, "SettingsBridge",
		QStringLiteral("由 Hub.settings 提供，不要在 QML 里创建"));
	qmlRegisterUncreatableType<UlinCoreSettingsBridge>("ULinHub", 1, 0, "UlinCoreSettingsBridge",
		QStringLiteral("由 Hub.settings.ulincoreBridge 提供，不要在 QML 里创建"));
	qmlRegisterUncreatableType<PluginMarketBridge>("ULinHub", 1, 0, "PluginMarketBridge",
		QStringLiteral("由 Hub.plugins 提供，不要在 QML 里创建"));
	qmlRegisterUncreatableType<CommandBridge>("ULinHub", 1, 0, "CommandBridge",
		QStringLiteral("由 Hub.commands 提供，不要在 QML 里创建"));
	qmlRegisterUncreatableType<ExtensionsBridge>("ULinHub", 1, 0, "ExtensionsBridge",
		QStringLiteral("由 Hub.extensions 提供，不要在 QML 里创建"));

	const QString mainQml = StyleResolver::instance().url(QStringLiteral("Main.qml"));
	QQmlApplicationEngine engine;

	// 已装扩展的 ui/ 目录加进 QML 导入路径：扩展里的 qml 模块按目录名被 Main.qml 引用
	const QString extensionRoot = ClientExtension::extensionDirectory();
	const QStringList installedExtensions = ClientExtension::installedNames();
	for (const QString& name : installedExtensions)
		engine.addImportPath(extensionRoot + QLatin1Char('/') + name);

	engine.load(QUrl(mainQml));
	if (engine.rootObjects().isEmpty()) {
		qCritical().noquote()
			<< "[QML] Main.qml 加载失败 —— 请确认 QML 模块可解析（发行版需把 Qt 的 qml/ 部署到 exe 同目录）";
		return -1;
	}

	TimingLogger::mark(QStringLiteral("qml shell ready"));

	// 客户端扩展 ABI：扫描 <exe>/extensions，QPluginLoader 装载 main.so。
	const QStringList loadedExtensions = ClientExtension::loadAll();
	TimingLogger::mark(loadedExtensions.isEmpty()
		? QStringLiteral("client extensions: none")
		: QStringLiteral("client extensions loaded: %1").arg(loadedExtensions.join(QLatin1Char(','))));

	return app.exec();
}
