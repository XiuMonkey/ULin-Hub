// Linux 平台层：程序入口。
// 顺序与 Windows 版一致：日志 → 外观设置 → 翻译 → 主题 → 风格包/QML token
// → 注册 QML 单例 → 装载 qrc:/qml/Main.qml → 启动客户端扩展（.so）。
// 差异：无 .ico/.rc、无 dependence 私有程序集、无 QtWebEngineProcess.exe 兜底、
// 无 NativeWindowFilter（自绘标题栏由 WindowBridge 用 Qt6 系统移动/缩放接管）。

#include "common/settings/ClientSettings.h"
#include "common/ClientInfo.h"
#include "common/util/Logger.h"
#include "common/appearance/ThemeManager.h"
#include "common/util/ConnectionManager.h"
#include "common/util/CommonRegistry.h"
#include "common/util/HostIndex.h"
#include "ExtensionSystem/HostUi.h"
#include "ExtensionSystem/ClientExtension.h"
#include "common/appearance/TranslationManager.h"
#include "common/pluginmarket/PluginMarketController.h"
#include "common/appearance/StyleResolver.h"
#include "common/session/SessionListModel.h"
#include "common/settings/SettingsController.h"
#include "common/shell/PanelBarDockModel.h"
#include "qmlbridge/FileBrowserBridge.h"
#include "qmlbridge/UrlInputBridge.h"
#include "common/agent/ToolsFilter.h"
#include "qmlbridge/QmlBridge.h"
#include "qmlbridge/ExtensionManagerController.h"
#include "qmlbridge/TerminalBridge.h"
#include "qmlbridge/TerminalVtModel.h"
#include "qmlbridge/ThemeBridge.h"
#include "qmlbridge/WindowBridge.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QStyleHints>
#include <QUrl>
#include <QUuid>
#include <QtQml/qqml.h>
#include <QtWebEngineQuick/QtWebEngineQuick>
#include <QtWidgets/QApplication>

int main(int argc, char* argv[])
{
	// WebEngine 资源目录：Qt 只找 <exe>/resources/，所以指到 resources/webengine/。
	const auto bundledWebEngineResources = [](const QString& appDir) {
		if (appDir.isEmpty())
			return QString();
		const QString dir = appDir + QStringLiteral("/resources/webengine");
		return QFileInfo::exists(dir + QStringLiteral("/qtwebengine_resources.pak")) ? dir : QString();
	};
	if (argc > 0) {
		const QString webEngineResources = bundledWebEngineResources(
			QFileInfo(QString::fromLocal8Bit(argv[0])).absolutePath());
		if (!webEngineResources.isEmpty())
			qputenv("QTWEBENGINE_RESOURCES_PATH", webEngineResources.toUtf8());
	}

	// 必须在 QApplication 之前：WebEngine 要在这里建立共享的 OpenGL 上下文。
	QtWebEngineQuick::initialize();

	QApplication app(argc, argv);

	// 本地数据契约：<exe>/data 持久化，<exe>/temp/run-<uuid> 临时化。
	{
		const QString dataRoot = QCoreApplication::applicationDirPath() + QStringLiteral("/data");
		QDir().mkpath(dataRoot + QStringLiteral("/client"));
		QDir().mkpath(dataRoot + QStringLiteral("/ulincore"));
		const QString tempRoot = QCoreApplication::applicationDirPath() + QStringLiteral("/temp");
		QDir().mkpath(tempRoot);
		const QString runDir = tempRoot + QStringLiteral("/run-")
			+ QUuid::createUuid().toString(QUuid::WithoutBraces);
		QDir().mkpath(runDir);
		qputenv("ULINHUB_TEMP_RUN", runDir.toUtf8());

		const QFileInfoList stale = QDir(tempRoot).entryInfoList(
			{ QStringLiteral("run-*") }, QDir::Dirs | QDir::NoDotAndDotDot);
		const QString currentRun = QDir(runDir).absolutePath();
		for (const QFileInfo& info : stale) {
			if (info.absoluteFilePath() != currentRun)
				QDir(info.absoluteFilePath()).removeRecursively();
		}
	}

	QCoreApplication::setApplicationVersion(ClientInfo::kVersion);

	Logger::init();
	TimingLogger::mark(QStringLiteral("Logger init (baseline)"));

	CommonRegistry::instance().AddToRegistry(DshHostIndex::kConnectionManager,
		&ConnectionManager::instance());
	CommonRegistry::instance().AddToRegistry(DshHostIndex::kUiHost,
		&HostUi::instance());

	AppearanceSetting::ensureFile();
	Translation::init();

	const AppearanceSetting::ThemeMode savedThemeMode = AppearanceSetting::themeMode();
	const bool dark = savedThemeMode == AppearanceSetting::ThemeMode::System
		? app.styleHints()->colorScheme() == Qt::ColorScheme::Dark
		: savedThemeMode == AppearanceSetting::ThemeMode::Dark;

	const QString stylesDir = QCoreApplication::applicationDirPath()
		+ QStringLiteral("/resources/ui");
	ThemeManager::instance().init(stylesDir,
		dark ? ThemeManager::Mode::Dark : ThemeManager::Mode::Light);

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
	qmlRegisterSingletonInstance("ULinHub", 1, 0, "Style", &StyleResolver::instance());
	qmlRegisterSingletonInstance("ULinHub", 1, 0, "Hub", &QmlBridge::instance());
	qmlRegisterSingletonInstance("ULinHub", 1, 0, "HostConnections", &ConnectionManager::instance());
	qmlRegisterSingletonInstance("ULinHub", 1, 0, "HostUi", &HostUi::instance());
	qmlRegisterSingletonInstance("ULinHub", 1, 0, "UrlInput", &UrlInputBridge::instance());
	qmlRegisterSingletonInstance("ULinHub", 1, 0, "PanelBarDock", &PanelBarDockModel::instance());
	CommonRegistry::instance().AddToRegistry(DshHostIndex::kPanelBarDock,
		&PanelBarDockModel::instance());

	PanelBarDockModel::instance().setWebEngineAvailable(true);

	qmlRegisterSingletonInstance("ULinHub", 1, 0, "Terminal", &TerminalBridge::instance());
	qmlRegisterSingletonInstance("ULinHub", 1, 0, "FileBrowser", &FileBrowserBridge::instance());

	qmlRegisterUncreatableType<TerminalVtModel>("ULinHub", 1, 0, "TerminalVtModel",
		QStringLiteral("由 Terminal.bindView(blockId) 提供，不要在 QML 里创建"));
	qmlRegisterUncreatableType<ToolsFilter>("ULinHub", 1, 0, "ToolsFilter",
		QStringLiteral("由 Hub.toolsFilter 提供，不要在 QML 里创建"));
	qmlRegisterUncreatableType<SessionListModel>("ULinHub", 1, 0, "SessionListModel",
		QStringLiteral("由 Hub.sessions 提供，不要在 QML 里创建"));
	qmlRegisterUncreatableType<SettingsController>("ULinHub", 1, 0, "SettingsController",
		QStringLiteral("由 Hub.settings 提供，不要在 QML 里创建"));
	qmlRegisterUncreatableType<PluginMarketController>("ULinHub", 1, 0, "PluginMarketController",
		QStringLiteral("由 Hub.plugins 提供，不要在 QML 里创建"));
	qmlRegisterUncreatableType<ExtensionManagerController>("ULinHub", 1, 0,
		"ExtensionManagerController",
		QStringLiteral("由 Hub.extensions 提供，不要在 QML 里创建"));

	const QString mainQml = StyleResolver::instance().url(QStringLiteral("Main.qml"));
	QQmlApplicationEngine engine;

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
