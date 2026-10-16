#include "ExtensionSystem/ClientExtension.h"
#include "ExtensionSystem/DshHostPlugin.h"
#include "common/util/HostIndex.h"
#include "common/util/ConnectionManager.h"
#include "common/util/CommonRegistry.h"
#include "ExtensionSystem/HostUi.h"
#include "VirtualClass/VirtualApiTakeover.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QLibrary>
#include <QPluginLoader>
#include <QThread>
#include <QtGlobal>

namespace ClientExtension
{
	namespace
	{
		QStringList g_loadedNames;  // 只在 GUI 线程读写，不加锁
		QHash<QString, QPluginLoader*> g_loaders;  // 已装载扩展的 loader（键 = 扩展名，所有权在 qApp）

		const char* const kPendingRemovalMarker = ".pending-removal";  // 卸载时删不掉的目录留此标记，下次启动由 sweepPendingRemovals() 清掉
		const char* const kDetachSlot = "detachHost()";  // 插件可选槽"我要退场了"：元对象层字符串契约（非接口方法），invoke 后宿主不再用插件的任何对象；老插件没有它也不踩空 vtable 槽位
		const char* const kIdentitySlot = "setHostIdentity(QString)";  // 可选槽：attachHost() 之前推一次本扩展的安装目录名；架空舞台要求 owner 等于安装名才能校验所有权、卸载时收回它占的台，插件不必硬编码名字

		bool onGuiThread()
		{
			const QCoreApplication* app = QCoreApplication::instance();
			return app && QThread::currentThread() == app->thread();
		}

		// regulation.json5 是"已安装"的判据
		QString regulationPath(const QString& dir)
		{
			return dir + QStringLiteral("/regulation.json5");
		}

		// 只认两种形状：带我们的标记文件，或没有 regulation.json5 但里面还有 dll
		bool looksLikeRemovalLeftover(const QDir& dir)
		{
			if (QFile::exists(dir.filePath(QLatin1String(kPendingRemovalMarker))))
				return true;
			if (QFile::exists(dir.filePath(QStringLiteral("regulation.json5"))))
				return false;
			return !dir.entryList({ QStringLiteral("*.dll") }, QDir::Files).isEmpty();
		}

		// 只在启动期（loadAll 之前）调用：那时 dll 不再被映射，删除必然成功
		void sweepPendingRemovals()
		{
			QDir root(extensionDirectory());
			if (!root.exists())
				return;

			const QFileInfoList subDirs = root.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
			for (const QFileInfo& sub : subDirs) {
				const QString path = sub.absoluteFilePath();
				if (!looksLikeRemovalLeftover(QDir(path)))
					continue;
				if (QDir(path).removeRecursively())
					qInfo("[ClientExtension] swept removal leftover: %s", qPrintable(sub.fileName()));
				else
					qWarning("[ClientExtension] cannot sweep leftover (still locked?): %s", qPrintable(path));
			}
		}

		bool loadOneImpl(const QString& dllPath, const QString& rawName, QString* error)
		{
			const QString name = rawName.isEmpty() ? QFileInfo(dllPath).completeBaseName() : rawName;
			const QString fileName = QFileInfo(dllPath).fileName();

			if (g_loadedNames.contains(name)) {  // 已装载（同名二次装载，如重复安装）：不重复装载 dll，只重新 attach
				QPluginLoader* loader = g_loaders.value(name);
				QObject* root = loader ? loader->instance() : nullptr;
				auto* plugin = root ? qobject_cast<DshHostPlugin*>(root) : nullptr;
				if (plugin) {
					plugin->attachHost();
					qInfo("[ClientExtension] re-attached: %s", qPrintable(name));
					return true;
				}
				qWarning("[ClientExtension] %s: already loaded but cannot re-attach", qPrintable(name));
				return false;
			}

			// 顺序别改回一句 QPluginLoader(dllPath)：构造函数会带上 PreventUnloadHint，而
			// setLoadHints() 只在尚未关联文件时才生效，故必须先构造空的、清 hint、再 setFileName
			auto* loader = new QPluginLoader(QCoreApplication::instance());
			loader->setLoadHints(QLibrary::LoadHints());
			loader->setFileName(dllPath);

			QObject* root = loader->instance();
			if (!root) {  // Qt 会在这里拦掉不兼容的插件（版本不符、debug/release 混用），做法补在原因后面
				QString reason = loader->errorString();
				if (reason.contains(QStringLiteral("debug and release"), Qt::CaseInsensitive))
					reason += qtTrId("ext_build_config_mismatch_hint");
				if (error)
					*error = reason;
				qWarning("[ClientExtension] not a Qt plugin (or load failed): %s -- %s",
					qPrintable(fileName), qPrintable(reason));
				delete loader;
				return false;
			}

			g_loaders.insert(name, loader);  // 两个成功分支都登记：装载失败的那个同样会让目录删不掉

			auto* plugin = qobject_cast<DshHostPlugin*>(root);
			if (!plugin) {
				const QString reason = QStringLiteral("does not implement DshHostPlugin");
				if (error)
					*error = reason;
				qWarning("[ClientExtension] %s: %s", qPrintable(fileName), qPrintable(reason));
				return false;  // loader 留在 qApp 下，随进程一起走
			}

			// 可选能力：只登记、不装载判定，拒绝发生在 Takenover(true) 时；插件侧必须在根对象上写 Q_INTERFACES(DshHostPlugin VirtualApiSink)，否则 cast 永远是 nullptr
			if (qobject_cast<VirtualApiSink*>(root)) {
				CommonRegistry::instance().AddToRegistry(DshHostIndex::kApiSink, root);
				qInfo("[ClientExtension] %s: 后端接管接收端（VirtualApiSink）已登记", qPrintable(name));
			}
			else {
				qInfo("[ClientExtension] %s: 未实现 VirtualApiSink —— 它不能接管后端出站", qPrintable(name));
			}

			// 可选槽：插件要架空就必须知道自己的安装名（老插件没有也不踩空槽位）
			if (root->metaObject()->indexOfMethod(kIdentitySlot) >= 0) {
				QMetaObject::invokeMethod(root, "setHostIdentity", Qt::DirectConnection, Q_ARG(QString, name));
			}

			plugin->attachHost();
			g_loadedNames.append(name);
			qInfo("[ClientExtension] loaded: name=%s dll=%s", qPrintable(name), qPrintable(fileName));
			return true;
		}
	}

	QString extensionDirectory()
	{
		const QString override = qEnvironmentVariable("ULINHUB_CLIENT_EXTENSION_DIR");
		if (!override.isEmpty())
			return override;

		return QCoreApplication::applicationDirPath() + QStringLiteral("/clientExtensions");
	}

	QStringList loadedNames()
	{
		return g_loadedNames;
	}

	QStringList installedNames()
	{
		QStringList names;
		const QDir dir(extensionDirectory());
		if (!dir.exists())
			return names;

		const QFileInfoList subDirs = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
		for (const QFileInfo& sub : subDirs) {
			if (QFile::exists(regulationPath(sub.absoluteFilePath())))
				names.append(sub.fileName());
		}
		return names;
	}

	bool remove(const QString& name, QString* error)
	{
		if (name.isEmpty()) {
			if (error)
				*error = QStringLiteral("empty extension name");
			return false;
		}

		const QString dir = extensionDirectory() + QStringLiteral("/") + name;
		if (!QDir(dir).exists()) {
			if (error)
				*error = QStringLiteral("not installed: %1").arg(name);
			return false;
		}

		// 卸载成功 ⇒ 模块解映射 ⇒ 文件锁松开 ⇒ 目录马上能删干净。前提是装载时 PreventUnloadHint
		// 已清掉、且插件声明可安全退场；detachHost() 里必须删掉自己挂在宿主控件树里的东西
		if (QPluginLoader* loader = g_loaders.take(name)) {
			QObject* root = loader->instance();
			const bool canDetach = root && root->metaObject()->indexOfMethod(kDetachSlot) >= 0;
			if (canDetach) {
				QMetaObject::invokeMethod(root, "detachHost", Qt::DirectConnection);

				// 接管交还（2026-10-01）：扩展可能接管了宿主的入口（侧栏设置 / 插件市场按钮）。
				// 不交还的话，扩展卸下之后那些按钮会变成"点了没反应" —— 它接的槽已经不存在了。
				// 放在 unload() 之前：此刻插件还活着，断开的是它那条连接。
				ConnectionManager::instance().ReleaseTakeoversFor(name);
				// 顶替面板坞块后端的登记同理要撤：不撤的话登记表里会留一具尸体 —— HasPanelBackend
				// 一直报真（自省/界面提示据此说"已被扩展接管"），而 panelBackend 因 QPointer 置空
				// 返回空指针，两边说法不一致。撤掉它，语义才回到"该 kind 用内置后端"。
				HostUi::instance().releasePanelBackends(name);

				const bool unloaded = loader->unload();  // unload() 自己会把根组件删掉，这里不该手工 delete 插件对象
				qInfo("[ClientExtension] remove: detach+unload %s -> %s", qPrintable(name),
					unloaded ? "ok" : "failed (loader refused)");

				if (unloaded)  // 实例已销毁、dll 也解映射，名字不再代表活着的东西；不摘掉的话"移除 → 再安装"会走"重新 attach"分支却找不到 loader、扩展按钮不出来
					g_loadedNames.removeAll(name);

				if (unloaded && QDir(dir).removeRecursively()) {
					qInfo("[ClientExtension] removed: %s (unloaded + dir deleted)", qPrintable(name));
					return true;
				}
			}
			else if (root) {
				qInfo("[ClientExtension] %s has no %s slot: it cannot be unloaded safely,"
					" falling back to mark + sweep", qPrintable(name), kDetachSlot);
			}
		}

		// （2026-10-01 删）这里原本还有一次 UiStage::releaseForOwner(name)（"宿主强制收回架空"）：UiStage 整套（抢台 / 还台 / 自绘窗口条）
		// 是旧控件壳的能力，它要 QMainWindow*，而现在的宿主是 QQmlApplicationEngine + QML Window、永远抢不到台，所以整套连同 WindowFrame 一起删了。

		// 删判据文件即"已卸载"：不再出现在列表，下次启动也不会被装载
		const QString regulation = regulationPath(dir);
		if (QFile::exists(regulation) && !QFile::remove(regulation)) {
			if (error)
				*error = QStringLiteral("cannot remove %1").arg(regulation);
			return false;
		}

		if (QDir(dir).removeRecursively()) {
			qInfo("[ClientExtension] removed: %s (dir deleted)", qPrintable(name));
			return true;
		}

		// 还有文件删不掉（被本进程映射着、插件没声明可退场）：留标记，下次启动收尾；注意 Windows 上"改名/移动目录"也躲不开这个锁，所以不做那种花招
		QFile marker(QStringLiteral("%1/%2").arg(dir, QLatin1String(kPendingRemovalMarker)));
		if (!marker.open(QIODevice::WriteOnly | QIODevice::Truncate))
			qWarning("[ClientExtension] cannot write removal marker: %s", qPrintable(marker.fileName()));
		else
			marker.close();

		const QString leftovers = QDir(dir).entryList(QDir::Files, QDir::Name).join(QLatin1String(", "));
		qInfo("[ClientExtension] removed (files still locked): %s leftovers=[%s] dir=%s",
			qPrintable(name), qPrintable(leftovers), qPrintable(dir));
		if (error)
			*error = qtTrId("ext_delete_locked");   // 残留清单与目录已在上一条 qInfo 里
		return false;
	}

	bool isClientExtensionType(const QString& type)
	{
		return type.compare(QStringLiteral("ClientExtension"), Qt::CaseInsensitive) == 0
			|| type.compare(QStringLiteral("ClientExtensionDebug"), Qt::CaseInsensitive) == 0;
	}

	QStringList loadAll()
	{
		QStringList loaded;
		if (!onGuiThread()) {
			qWarning("[ClientExtension] loadAll() must be called on the GUI thread, skipped");
			return loaded;
		}

		sweepPendingRemovals();  // 清掉上次没删干净的残留：那时 dll 还锁着，新进程里不再锁

		const QDir dir(extensionDirectory());
		if (!dir.exists())
			return loaded;  // 没装客户端扩展是常态，不报错也不建目录

		const QFileInfoList subDirs = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
		for (const QFileInfo& sub : subDirs) {
			const QDir subDir(sub.absoluteFilePath());

			if (!QFile::exists(regulationPath(sub.absoluteFilePath()))) {  // 没有 regulation.json5 的一律跳过：那是移除时 DLL 被锁留下的残留
				qInfo("[ClientExtension] skip (no regulation.json5, likely a removal leftover): %s",
					qPrintable(sub.fileName()));
				continue;
			}

			QString dll = subDir.filePath(QStringLiteral("main.dll"));
			if (!QFile::exists(dll)) {
				const QStringList any = subDir.entryList({ QStringLiteral("*.dll") }, QDir::Files, QDir::Name);
				if (any.isEmpty())
					continue;
				dll = subDir.filePath(any.first());
			}
			if (loadOneImpl(dll, sub.fileName(), nullptr))
				loaded.append(sub.fileName());
		}

		return loaded;
	}

	bool loadOne(const QString& dllPath, const QString& name, QString* error)
	{
		if (!onGuiThread()) {
			const QString reason = QStringLiteral("loadOne() must be called on the GUI thread");
			if (error)
				*error = reason;
			qWarning("[ClientExtension] %s", qPrintable(reason));
			return false;
		}

		if (dllPath.isEmpty() || !QFile::exists(dllPath)) {
			const QString reason = QStringLiteral("dll not found: %1").arg(dllPath);
			if (error)
				*error = reason;
			qWarning("[ClientExtension] %s", qPrintable(reason));
			return false;
		}

		return loadOneImpl(dllPath, name, error);
	}

	// 退出路径的收尾，三件事的顺序不能换：先让插件自己退场（此刻宿主界面还完整、事件循环还活着），
	// 再收它占的台，最后才把 loader 摘出 qApp —— 一摘就不能再指望插件做任何事了。
	void shutdownForExit()
	{
		if (g_loaders.isEmpty())
			return;

		if (!onGuiThread()) {
			qWarning("[ClientExtension] shutdownForExit() must be called on the GUI thread, skipped");
			return;
		}

		const QStringList names = g_loaders.keys();  // 先取快照再逐个 take：detachHost() 里插件可能回到宿主（重装扩展之类），不能边迭代边改表
		g_loadedNames.clear();

		for (const QString& name : names) {
			QPluginLoader* loader = g_loaders.take(name);
			QObject* root = loader ? loader->instance() : nullptr;

			if (root && root->metaObject()->indexOfMethod(kDetachSlot) >= 0) {
				QMetaObject::invokeMethod(root, "detachHost", Qt::DirectConnection);
				qInfo("[ClientExtension] exit: %s 已退场（detachHost 同步执行）", qPrintable(name));
			}
			else {
				qInfo("[ClientExtension] exit: %s 没有 %s 槽，只摘出（插件未声明可安全退场）", qPrintable(name), kDetachSlot);
			}

			// 摘掉接管登记：插件不再受宿主调度，留着它会让退出路径上还有代码去调插件对象；（2026-10-01 删）原先这里也收一次架空舞台，UiStage 已随旧控件壳一起删除
			if (root)
				CommonRegistry::instance().Destroy(DshHostIndex::kApiSink, root);

			// 关键一步（理由见头文件）：摘出 qApp，~QApplication 就不会删它 ⇒ 不卸 dll、不析构插件根对象。
			// 卸载留给进程终止 —— 那时内核直接回收映射，不跑 C++ 析构，也就不会卡在管道等待上。
			if (loader)
				loader->setParent(nullptr);
		}

		qInfo("[ClientExtension] 退出收尾：%d 个扩展已退场，dll 随进程终止回收", int(names.size()));
	}
}

// （2026-10-01 删）本文件末尾原先还有 namespace QQuickSupport 的整套实现（约 140 行：.ext 形式的 Qt Quick 运行时包安装 / 激活 / 移除）：
// 宿主自己就是 QML 壳、Qt Quick 随宿主部署 ⇒ 运行时包与其 Subtype 预检一并退役，见 docs/codex-port-handover.md 工单 A。
