// 见 PluginMarketController.h：插件市场数据面。
// 本文件在 common/**（领域层）：**不 include core/**、不碰 qmlbridge/**。"装完市场包重启内置服务端"
// 是宿主行为，只发 serverRestartRequested()，由 qmlbridge/QmlBridge 接住调 ClientRuntime::restartServer()。

#include "common/PluginMarket/PluginMarketController.h"

#include "common/PluginMarket/PluginMarketClient.h"
#include "common/PluginMarket/PluginMarketInstaller.h"
#include "network/DshApiClient.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonValue>
#include <QTimer>
#include <QVariantMap>

PluginMarketController::PluginMarketController(DshApiClient* api, QObject* parent)
	: QObject(parent)
	, m_api(api)
	, m_client(new PluginMarketClient(this))
	, m_installer(new PluginMarketInstaller(this))
{
	connect(m_client, &PluginMarketClient::registryLoaded, this,
		[this](const QJsonArray& plugins, const QString& source) {
			m_all = PluginMarketModel::parsePlugins(plugins);
			m_haveRegistry = true;   // 之后打开市场直接用这份，不再重取（见 openPage）
			setBusy(false);

			// 三种来源的文案与旧壳一致（快照 / 缓存 / 实时）
			if (source == QLatin1String("snapshot"))
				setStatus(qtTrId("plugin_loaded_offline"));
			else if (source == QLatin1String("cache"))
				setStatus(qtTrId("plugin_loaded_cached_fmt").arg(m_all.size()));
			else
				setStatus(qtTrId("plugin_loaded_fmt").arg(m_all.size()));

			rebuild();
		});

	connect(m_client, &PluginMarketClient::registryFailed, this,
		[this](const QString& error, int status) {
			setBusy(false);
			setStatus(qtTrId("plugin_load_failed_fmt").arg(error));
			// 404 = /dsh-market/* 不存在 ⇒ 市场包没装：自动安装（旧壳 PluginsManager 同路）
			if (status == 404)
				startMarketInstall();
		});

	// ---- 市场包自动安装（pnpm add dshmarket）----
	connect(m_installer, &PluginMarketInstaller::installStarted, this, [this] {
		m_installing = true;
		m_installLog.clear();
		m_installStatus = qtTrId("plugin_installing_market");
		qInfo().noquote() << "[Plugins] 市场包缺失，开始自动安装";
		emit installChanged();
	});

	connect(m_installer, &PluginMarketInstaller::installOutput, this, [this](const QString& line) {
		m_installLog.append(line);
		while (m_installLog.size() > 40)   // 只留最近 40 行，够排障
			m_installLog.removeFirst();
		m_installStatus = line;
		emit installChanged();
	});

	connect(m_installer, &PluginMarketInstaller::installFinished, this, [this](bool ok) {
		m_installing = false;
		setBusy(false);
		m_installStatus = ok ? qtTrId("plugin_install_done_restarting") : qtTrId("plugin_install_failed");
		emit installChanged();

		if (!ok)
			return;   // 安装失败：不重启服务端、不重试拉数据

		// 装完必须重启内置服务端，新插件才会被加载；稍后自动重试拉市场数据。
		// 重启本身是宿主行为：这里只发信号（接线层同步接住 ⇒ 与原 m_server->restart() 同时机）。
		emit serverRestartRequested();
		QTimer::singleShot(1800, this, [this] { refresh(); });
	});

	connect(m_client, &PluginMarketClient::installedLoaded, this,
		[this](const QJsonObject& installed) {
			m_installed = installed;
			rebuild();
		});

	connect(m_client, &PluginMarketClient::operationCompleted, this,
		[](const QString& path) {
			qInfo().noquote() << "[Plugins] 操作已提交:" << path;
		});

	connect(m_client, &PluginMarketClient::operationFailed, this,
		[this](const QString& path, const QString& error, int) {
			setBusy(false);
			qWarning().noquote() << "[Plugins] 操作失败:" << path << error;
			setStatus(qtTrId("plugin_action_failed_fmt").arg(error));
		});

	updateBaseUrl();
}

void PluginMarketController::startMarketInstall()
{
	if (!m_installer || m_installing)
		return;

	// 市场包已存在、或安装已在进行中时返回 false（调用方无需做任何事）
	if (!m_installer->ensureInstalled(QCoreApplication::applicationDirPath()))
		return;

	setBusy(true);
}

void PluginMarketController::updateBaseUrl() {
	if (m_api)
		m_client->setBaseUrl(m_api->baseUrl());
}

void PluginMarketController::setBusy(bool busy)
{
	if (m_busy == busy)
		return;

	m_busy = busy;
	emit busyChanged();
}

void PluginMarketController::setStatus(const QString& text)
{
	if (m_status == text)
		return;

	m_status = text;
	emit statusChanged();
}

QString PluginMarketController::pageLabel() const
{
	// 旧壳：plugin_page_fmt = "第 %1 / %2 页"
	return qtTrId("plugin_page_fmt").arg(m_page + 1).arg(m_pageCount);
}

void PluginMarketController::openPage()
{
	if (m_open)
		return;

	m_open = true;
	emit openChanged();

	// ⚠️ 打开时**不要无条件** refresh()：启动后 1.8 秒已经预热过一次（见构造函数里的 singleShot），
	// 再刷一次只是把同一个查询重发一遍 —— 数据 1.5 秒后回来时 models 整体 reset、ListView 一次性
	// 建出十几张卡 ⇒ 正好在"打开后 1.5 秒"卡一下（实测单帧 51ms，用户体感就是打开不流畅，且
	// 5 天前没有这个行为）。有数据就直接用缓存；只有"还没拿到过"时才去取。
	if (!m_haveRegistry)
		refresh();
}

void PluginMarketController::closePage()
{
	if (!m_open)
		return;

	m_open = false;
	emit openChanged();
}

void PluginMarketController::setTab(int tab)
{
	const int bounded = qBound(0, tab, 1);
	if (m_tab == bounded)
		return;

	m_tab = bounded;
	emit viewChanged();
}

void PluginMarketController::setSearch(const QString& keyword)
{
	if (m_search == keyword)
		return;

	m_search = keyword;
	m_page = 0;   // 旧壳：搜索词变化回到第 0 页
	rebuild();
}

void PluginMarketController::refresh()
{
	updateBaseUrl();
	setBusy(true);
	setStatus(qtTrId("plugin_loading"));
	m_client->fetchRegistry();
	m_client->fetchInstalled();
}

void PluginMarketController::prevPage()
{
	if (!canPrev())
		return;

	--m_page;
	rebuild();
}

void PluginMarketController::nextPage()
{
	if (!canNext())
		return;

	++m_page;
	rebuild();
}

void PluginMarketController::install(const QString& url)
{
	if (url.isEmpty())
		return;

	setBusy(true);
	setStatus(qtTrId("plugin_action_submitted"));
	m_client->installPlugin(url);
}

void PluginMarketController::update(const QString& name)
{
	if (name.isEmpty())
		return;

	setBusy(true);
	setStatus(qtTrId("plugin_action_submitted"));
	m_client->updatePlugin(name);
}

void PluginMarketController::uninstall(const QString& name)
{
	if (name.isEmpty())
		return;

	setBusy(true);
	setStatus(qtTrId("plugin_action_submitted"));
	m_client->uninstallPlugin(name);
}

void PluginMarketController::restartServer()
{
	setBusy(true);
	setStatus(qtTrId("plugin_action_submitted"));
	m_client->restartServer();
}

void PluginMarketController::rebuild()
{
	// 市场列表：过滤 + 分页（都在 PluginMarketModel 里）
	m_filtered = PluginMarketModel::filter(m_all, m_search);
	const PluginMarketModel::Page page = PluginMarketModel::paginate(m_filtered.size(), kPageSize, m_page);
	m_page = page.index;
	m_pageCount = page.count;

	QVariantList market;
	for (int i = page.begin; i < page.end; ++i) {
		const MarketPlugin& plugin = m_filtered.at(i);
		QVariantMap row;
		row.insert(QStringLiteral("name"), plugin.name);
		row.insert(QStringLiteral("category"), plugin.category);
		row.insert(QStringLiteral("url"), plugin.url);
		// 中文优先、回落英文（PluginMarketModel::displayDescription）
		row.insert(QStringLiteral("description"), plugin.displayDescription());
		market.append(row);
	}
	m_marketItems = market;

	QVariantList installed;  // 已安装列表：name -> version
	for (auto it = m_installed.constBegin(); it != m_installed.constEnd(); ++it) {
		QVariantMap row;
		row.insert(QStringLiteral("name"), it.key());
		row.insert(QStringLiteral("version"), it.value().toString());
		installed.append(row);
	}
	m_installedItems = installed;

	emit viewChanged();
}
