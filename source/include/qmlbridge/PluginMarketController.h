#pragma once

// 插件市场数据面（M6）：把既有的 PluginMarketClient / PluginMarketModel 接到 QML。
//
// 逻辑一行不重写：HTTP 与解析在 PluginMarketClient（/dsh-market/*），
// 过滤与分页在 PluginMarketModel（filter / paginate，每页 20 条），这里只做：
//   * 视图状态（当前页、搜索词、当前 Tab、状态文案）
//   * 把 registry / installed 的回包翻成 QML 好用的 QVariantList
//   * 操作（安装/更新/卸载/重启服务）与它们的反馈文案
// 文案全部用既有 qtTrId id（plugin_loading / plugin_loaded_fmt / plugin_action_failed_fmt …）。

#include "common/PluginMarket/PluginMarketModel.h"

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVector>

class DshApiClient;
class PluginMarketClient;
class PluginMarketInstaller;
class ServerManager;

class PluginMarketController : public QObject
{
	Q_OBJECT
	Q_PROPERTY(bool open READ isOpen NOTIFY openChanged)
	Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
	Q_PROPERTY(QString status READ status NOTIFY statusChanged)
	Q_PROPERTY(int tab READ tab NOTIFY viewChanged)
	Q_PROPERTY(QString search READ search NOTIFY viewChanged)
	Q_PROPERTY(QVariantList marketItems READ marketItems NOTIFY viewChanged)
	Q_PROPERTY(QVariantList installedItems READ installedItems NOTIFY viewChanged)
	Q_PROPERTY(QString pageLabel READ pageLabel NOTIFY viewChanged)
	Q_PROPERTY(bool canPrev READ canPrev NOTIFY viewChanged)
	Q_PROPERTY(bool canNext READ canNext NOTIFY viewChanged)
	// 市场包（dshmarket）缺失时的自动安装：进度与输出
	Q_PROPERTY(bool installing READ installing NOTIFY installChanged)
	Q_PROPERTY(QString installStatus READ installStatus NOTIFY installChanged)
	Q_PROPERTY(QStringList installLog READ installLog NOTIFY installChanged)

public:
	// server：装完市场包后要重启内置服务端，新插件才会被加载
	explicit PluginMarketController(DshApiClient* api, ServerManager* server, QObject* parent = nullptr);

	bool isOpen() const { return m_open; }
	bool busy() const { return m_busy; }
	QString status() const { return m_status; }
	int tab() const { return m_tab; }
	QString search() const { return m_search; }
	QVariantList marketItems() const { return m_marketItems; }
	QVariantList installedItems() const { return m_installedItems; }
	QString pageLabel() const;
	bool canPrev() const { return m_page > 0; }
	bool canNext() const { return m_page + 1 < m_pageCount; }
	bool installing() const { return m_installing; }
	QString installStatus() const { return m_installStatus; }
	QStringList installLog() const { return m_installLog; }

	Q_INVOKABLE void openPage();
	Q_INVOKABLE void closePage();
	Q_INVOKABLE void setTab(int tab);
	Q_INVOKABLE void setSearch(const QString& keyword);
	Q_INVOKABLE void refresh();
	Q_INVOKABLE void prevPage();
	Q_INVOKABLE void nextPage();
	Q_INVOKABLE void install(const QString& url);
	Q_INVOKABLE void update(const QString& name);
	Q_INVOKABLE void uninstall(const QString& name);
	Q_INVOKABLE void restartServer();

signals:
	void openChanged();
	void busyChanged();
	void statusChanged();
	void viewChanged();
	void installChanged();

private:
	static constexpr int kPageSize = 20;   // 与旧壳 PluginsManager 同值

	void setBusy(bool busy);
	void setStatus(const QString& text);
	void rebuild();
	void updateBaseUrl();
	// /dsh-market/* 回 404 = 市场包没装：走 pnpm 自动安装（已在装 / 已存在时什么都不做）
	void startMarketInstall();

	DshApiClient* m_api = nullptr;
	PluginMarketClient* m_client = nullptr;
	ServerManager* m_server = nullptr;
	PluginMarketInstaller* m_installer = nullptr;

	bool m_installing = false;
	QString m_installStatus;
	QStringList m_installLog;

	QVector<MarketPlugin> m_all;
	QVector<MarketPlugin> m_filtered;
	QJsonObject m_installed;
	QVariantList m_marketItems;
	QVariantList m_installedItems;

	QString m_status;
	QString m_search;
	int m_tab = 0;
	int m_page = 0;
	int m_pageCount = 1;
	bool m_busy = false;
	bool m_open = false;
};
