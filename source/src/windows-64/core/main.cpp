// 顺序：日志 → 外观设置 → 翻译 → 主题（释放风格包 + 读 palette）→ 原生窗口过滤器 →
// 风格包/QML token → 会话数据面 → 注册 QML 单例 → 装载 qrc:/qml/Main.qml。

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
#include "common/platform/win/NativeWindowFilter.h"
#include "common/agent/dsh/pluginmarket/PluginMarketController.h"
#include "common/appearance/StyleResolver.h"
#include "common/session/SessionListModel.h"
#include "common/settings/SettingsController.h"
#include "common/shell/PanelBarDockModel.h"
#include "qmlbridge/FileBrowserBridge.h"
#include "qmlbridge/UrlInputBridge.h"
#include "common/agent/ToolsFilter.h"
#include "qmlbridge/QmlBridge.h"
#include "qmlbridge/TerminalBridge.h"
#include "qmlbridge/TerminalVtModel.h"
#include "qmlbridge/ThemeBridge.h"
#include "qmlbridge/WindowBridge.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QFont>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QStyleHints>
#include <QUrl>
#include <QtQml/qqml.h>
#include <QtWebEngineQuick/QtWebEngineQuick>
#include <QtWidgets/QApplication>

int main(int argc, char* argv[])
{
	// WebEngine 数据目录（icudtl.dat / qtwebengine_resources*.pak / v8_context_snapshot.bin）放在
	// resources\webengine\：Qt 只找 <exe>\resources\，必须用 QTWEBENGINE_RESOURCES_PATH 指过去。
	// 首次建上下文时读走并缓存，故 QApplication 前后各设一次；目录不存在就不设，让 Qt 回落默认。
	if (argc > 0)
		applyBundledWebEngineEnv(QFileInfo(QString::fromLocal8Bit(argv[0])).absolutePath());

	// ⚠️ 必须在 QApplication **之前**：WebEngine 要在这里设置与主上下文共享的 OpenGL 上下文，
	// 放到后面（或漏掉）会在运行期报 "WebEngineContext used before initialize" 并直接崩。
	QtWebEngineQuick::initialize();

	QApplication app(argc, argv);

	// 版本号收口到 common/ClientInfo.h，日志、崩溃报告与 helpdesk 都从 applicationVersion 取。
	// 运行时目录契约（<exe>/data 持久化、<exe>/temp/run-<uuid> 临时化）由 util/RuntimeDirs.h 负责。
	(void)prepareRuntimeDirs();

	QCoreApplication::setApplicationVersion(ClientInfo::kVersion);

	// QtWebEngineProcess.exe 只搜 <exe>/bin 与 <exe>/，本工程却把它收在 <exe>/dependence/：
	// 第一次建 WebEngineView 会因找不到而 qFatal（fatal.log 里会列出搜索过的路径）。
	// 显式指到 dependence/ 里的那份，子进程自己的目录就是全部 Qt DLL 所在。
	{
		const QString bundledProcess = QCoreApplication::applicationDirPath()
			+ QStringLiteral("/dependence/QtWebEngineProcess.exe");
		if (QFileInfo::exists(bundledProcess))
			setEnvPath("QTWEBENGINEPROCESS_PATH", bundledProcess);

		// 资源目录兜底：上面按 argv[0] 设过一次，这里按真实 exe 目录再设一次（同一个值就无害），
		// 覆盖"argv[0] 是相对路径 / 从别的目录启动"这些推不准的情况。
		applyBundledWebEngineEnv(QCoreApplication::applicationDirPath());
	}

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

	// 界面副本目录：<exe>/resources/ui（与 translations 一起收进 resources；旧的 <exe>/ui 已迁移）
	const QString stylesDir = QCoreApplication::applicationDirPath() + QStringLiteral("/resources/ui");
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
	qmlRegisterSingletonInstance("ULinHub", 1, 0, "Style", &StyleBridge::instance());
	qmlRegisterSingletonInstance("ULinHub", 1, 0, "Hub", &QmlBridge::instance());   // QML 单例名仍是 Hub（契约）

	// 统一连接登记表：QML 侧用 HostConnections.RegisterConnection 由宿主建连（不用自己写 Connections）
	// 名字不叫 Connections —— 那是 QtQuick 里的类型名，会撞。
	qmlRegisterSingletonInstance("ULinHub", 1, 0, "HostConnections", &HostConnectionsBridge::instance());

	// 扩展 UI 宿主（QML 面）：HostUi.registerItem / item / mountInto / unmountOwner
	qmlRegisterSingletonInstance("ULinHub", 1, 0, "HostUi", &HostUiBridge::instance());

	// 地址栏输入规范化（纯字符串规则在 common/util/UrlInput）：QML 侧 UrlInput.normalize(text)
	qmlRegisterSingletonInstance("ULinHub", 1, 0, "UrlInput", &UrlInputBridge::instance());

	// 右栏面板坞的块（页签）列表：QML 侧 PanelBarDock.open/close/activate/showHome + Repeater { model: PanelBarDock }
	qmlRegisterSingletonInstance("ULinHub", 1, 0, "PanelBarDock", &PanelBarDockBridge::instance());

	// 同一个实例也登记给插件（kPanelBarDock，"对象直给"那条）：插件可以直接用模型的方法与信号，
	// 与 VirtualBlockBackendHost 那两条虚接口并存（见 HostIndex.h 的说明）。
	CommonRegistry::instance().AddToRegistry(DshHostIndex::kPanelBarDock, &PanelBarDockModel::instance());

	// 面板坞的运行时能力：本构建带 WebEngine（上面已 QtWebEngineQuick::initialize()，vcxproj 也列了
	// webenginequick）⇒ 网页/终端块给真视图。缺了它这两个块会降级成说明页，而不是去建 WebEngineView 崩掉。
	PanelBarDockModel::instance().setWebEngineAvailable(true);

	// 右栏「终端」块的接线层：QML 侧 Terminal.bindView(blockId)/closeBlock(...) 拿会话与网格模型。
	// init(api) 已在 QmlBridge::init() 里调过（出站 RPC 只走那一个 DshApiClient）
	qmlRegisterSingletonInstance("ULinHub", 1, 0, "Terminal", &TerminalBridge::instance());
	qmlRegisterSingletonInstance("ULinHub", 1, 0, "FileBrowser", &FileBrowserBridge::instance());

	// 终端网格模型（TerminalVtModel）：QML 侧由 Terminal.bindView(blockId) 提供，
	// 视图（TerminalView.qml）只读它的属性/调它的 INvokable —— 登记类型是为了属性有静态类型可查。
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

	// 扩展自带的 QML：把每个已安装扩展的目录加进 import path，ui/ 因而能 import 兄弟组件
	// 与带 qmldir 的模块式目录。注意这是引擎级全局路径，扩展目录名会进入所有 QML 的解析范围。
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

	// 启动装载客户端扩展（DLL 扩展的运行入口）。位置刻意在 engine.load() 之后：
	// DLL 的 attachHost() 要用的宿主对象（主题 / 多语言 / 宿主 index）此时已就绪。
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