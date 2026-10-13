#include "qmlbridge/ExtensionManagerController.h"

#include "common/util/ConnectionManager.h"
#include "core/ServerManager.h"
#include "ExtensionSystem/ClientExtension.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFileDialog>
#include <QTimer>

// 与旧壳 ExtensionManagerPopup 一一对应：
//   populate() <-> populateList()、installFromFile() <-> installExtension()、pollInstall() <-> pollInstall()、
//   removeSelected() <-> removeSelected()、cleanupResiduals() <-> cleanupResiduals()、refresh() <-> refresh()。
ExtensionManagerController::ExtensionManagerController(ServerManager* server, QObject* parent)
	: QObject(parent)
	, m_server(server)
{
	m_installTimer = new QTimer(this);  // 与原版一致：100ms 轮询后台安装任务（非阻塞，主线程不卡）
	m_installTimer->setInterval(100);
	dshRegister("ExtensionManager.001", m_installTimer, &QTimer::timeout, this,
		&ExtensionManagerController::pollInstall);

	populate();   // 先备好列表，页面一开就有内容
}

ExtensionManagerController::~ExtensionManagerController()
{
	// 后台任务可能还在跑：等它结束再析构，否则它会往已销毁的对象里写结果
	m_installTask.waitForFinished();
}

ExtensionRegistry ExtensionManagerController::registry() const
{
	// 工具扩展的 profile：<DSH_HOME>/profiles/web（与 PluginMarketInstaller / ServerManager 同一处）
	const QString home = m_server ? m_server->dshHome() : QString();
	return ExtensionRegistry(home + QStringLiteral("/profiles/web"));
}

QString ExtensionManagerController::kindLabel(const QString& kind) const
{
	// 工具扩展在原版里没有种类后缀（裸名字），所以这里返回空串，由 QML 决定不画徽章
	return kind == QLatin1String("client") ? qtTrId("ext_kind_client") : QString();
}

QVariantMap ExtensionManagerController::selectedItem() const
{
	if (m_selected < 0 || m_selected >= m_items.size())
		return QVariantMap();
	return m_items.at(m_selected).toMap();
}

bool ExtensionManagerController::canRemove() const
{
	return !selectedItem().isEmpty();
}

void ExtensionManagerController::setSelectedIndex(int index)
{
	if (index < -1 || index >= m_items.size())
		index = -1;
	if (m_selected == index)
		return;
	m_selected = index;
	emit selectedIndexChanged();
}

void ExtensionManagerController::setStatus(const QString& text)
{
	if (m_status == text)
		return;
	m_status = text;
	emit statusChanged();
}

void ExtensionManagerController::setBusy(bool busy)
{
	if (m_busy == busy)
		return;
	m_busy = busy;
	emit busyChanged();
}

void ExtensionManagerController::setRestartHint(const QString& hint)
{
	if (m_restartHint == hint)
		return;
	m_restartHint = hint;
	emit restartHintChanged();
}

void ExtensionManagerController::openPage()
{
	if (m_open)
		return;
	m_open = true;
	refresh();          // 每次打开都重扫：磁盘状态可能被别的路径改过
	emit openChanged();
}

void ExtensionManagerController::closePage()
{
	if (!m_open)
		return;
	m_open = false;
	setSelectedIndex(-1);
	emit openChanged();
}

void ExtensionManagerController::refresh()
{
	populate();
}

void ExtensionManagerController::populate()
{
	QVariantList rows;

	// ① 工具扩展：来自 extensions.json（和客户端扩展不是一套数据源）
	const QStringList toolNames = registry().installedExtensions();
	for (const QString& name : toolNames) {
		QVariantMap row;
		row.insert(QStringLiteral("kind"), QStringLiteral("tool"));
		row.insert(QStringLiteral("name"), name);
		row.insert(QStringLiteral("kindLabel"), kindLabel(QStringLiteral("tool")));
		row.insert(QStringLiteral("label"), name);
		row.insert(QStringLiteral("loaded"), true);
		rows.append(row);
	}

	// ② 客户端扩展：来自 <exe>/clientExtensions/；未装载的补一段说明
	const QStringList clientNames = ClientExtension::installedNames();
	const QStringList clientLoaded = ClientExtension::loadedNames();
	for (const QString& name : clientNames) {
		const bool loaded = clientLoaded.contains(name);
		QString label = name + QStringLiteral("  [") + qtTrId("ext_kind_client");
		if (!loaded)
			label += QStringLiteral("·") + qtTrId("ext_client_not_loaded");
		label += QLatin1Char(']');

		QVariantMap row;
		row.insert(QStringLiteral("kind"), QStringLiteral("client"));
		row.insert(QStringLiteral("name"), name);
		row.insert(QStringLiteral("kindLabel"), kindLabel(QStringLiteral("client")));
		row.insert(QStringLiteral("label"), label);
		row.insert(QStringLiteral("loaded"), loaded);
		rows.append(row);
	}

	// （2026-10-01 删）这里原有第③来源「Qt Quick 运行时（QQuickSupport）」：宿主自己就是 QML 壳、Qt Quick 随宿主部署，该 .ext 包与其 Subtype 预检一并退役（见工单 A）。

	m_items = rows;
	m_selected = -1;
	emit itemsChanged();
	emit selectedIndexChanged();

	setStatus(rows.isEmpty() ? qtTrId("ext_empty_hint") : qtTrId("ext_installed_count_fmt").arg(rows.size()));
}

void ExtensionManagerController::installFromFile()
{
	if (m_busy || m_installTask.isRunning())
		return;

	// 与原版同一个过滤器与标题；宿主是 QApplication，原生对话框可用
	const QString extPath = QFileDialog::getOpenFileName(nullptr, qtTrId("ext_choose_file_title"),
		QString(), QStringLiteral("DSH Extension (*.ext)"));
	if (extPath.isEmpty())
		return;

	qInfo().noquote() << QStringLiteral("[ExtensionManager] install: %1").arg(extPath);
	setBusy(true);
	setStatus(qtTrId("ext_installing"));

	if (!m_installTask.start(extPath, registry().serverProfilePath())) {
		setBusy(false);
		return;
	}
	m_installTimer->start();
}

void ExtensionManagerController::pollInstall()
{
	if (!m_installTask.tryFinish())
		return;

	m_installTimer->stop();
	setBusy(false);

	if (!m_installTask.succeeded()) {
		setStatus(qtTrId("ext_install_failed_fmt").arg(m_installTask.errorString()));
		qWarning().noquote() << QStringLiteral("[ExtensionManager] install failed:")
			<< m_installTask.errorString();
		return;
	}

	const ExtensionLoader::LoadedExtension& ext = m_installTask.extension();

	// 客户端扩展：不写 extensions.json、不重启服务端；装载必须留在 GUI 线程
	if (ext.isClientExtension) {
		QString loadError;
		if (ClientExtension::loadOne(ext.dllPath, ext.pluginName, &loadError)) {
			setStatus(qtTrId("ext_install_success_fmt").arg(ext.pluginName));
			qInfo().noquote() << QStringLiteral("[ExtensionManager] client extension loaded:")
				<< ext.pluginName;
		}
		else {
			setStatus(qtTrId("ext_install_failed_fmt").arg(loadError));
			qWarning().noquote() << QStringLiteral("[ExtensionManager] client extension load failed:")
				<< ext.pluginName << loadError;
		}
		// 扩展声明的界面 ABI 与本宿主不符 ⇒ 装载器只落了 DLL，没落 ui/（见 ExtensionLoader）
		if (!ext.uiAbiMatched)
			setRestartHint(qtTrId("ext_abi_mismatch_hint"));
		populate();
		return;
	}

	// 工具扩展：登记进 extensions.json；要重启服务端才生效（原版是无声自动重启，这里改成提示）
	registry().registerInstalled(ext.pluginName);
	populate();
	setStatus(qtTrId("ext_install_success_fmt").arg(ext.pluginName));
	setRestartHint(qtTrId("ext_installed_restart_hint"));
}

bool ExtensionManagerController::removeSelected()
{
	const QVariantMap row = selectedItem();
	if (row.isEmpty())
		return false;

	const QString name = row.value(QStringLiteral("name")).toString();
	const QString kind = row.value(QStringLiteral("kind")).toString();

	if (kind == QLatin1String("client")) {
		QString removeError;  // 已装载的插件在进程内卸不掉，要重启应用才彻底消失
		const bool removed = ClientExtension::remove(name, &removeError);
		populate();
		setStatus(removed ? qtTrId("ext_removed_fmt").arg(name) : qtTrId("ext_partial_delete_failed_fmt").arg(removeError));
		if (removed)
			setRestartHint(qtTrId("ext_removed_client_hint"));
		setSelectedIndex(-1);
		return true;
	}

	QString dirError;  // 工具扩展：目录删没删成功都清配置残留，否则 cordis.patch.yml 引用不存在的包
	const bool dirRemoved = registry().removeExtensionDirectory(name, &dirError);
	const bool patchRemoved = ExtensionRegistry::removePatchEntry(registry().serverProfilePath(), name);
	registry().unregisterInstalled(name);

	populate();
	if (dirRemoved && patchRemoved)
		setStatus(qtTrId("ext_removed_fmt").arg(name));
	else if (!dirRemoved)
		setStatus(qtTrId("ext_partial_delete_failed_fmt").arg(dirError));
	else
		setStatus(qtTrId("ext_patch_update_failed_fmt").arg(name));
	setRestartHint(qtTrId("ext_removed_tool_hint"));
	setSelectedIndex(-1);
	return true;
}

void ExtensionManagerController::cleanupResiduals()
{
	const ExtensionRegistry::CleanupResult result = registry().cleanupResiduals();
	if (!result.patchReadable) {
		setStatus(qtTrId("ext_patch_read_failed"));
		return;
	}

	populate();
	setStatus(result.removed.isEmpty() ? qtTrId("ext_no_residue")
		: qtTrId("ext_residue_cleaned_fmt").arg(result.removed.join(QStringLiteral(", "))));
	if (!result.removed.isEmpty())
		setRestartHint(qtTrId("ext_residue_cleaned_restart_hint"));
}

void ExtensionManagerController::restartServer()
{
	if (m_server)
		m_server->restart();
	setRestartHint(QString());
}
