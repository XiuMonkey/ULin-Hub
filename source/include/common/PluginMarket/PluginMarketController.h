#pragma once

// 分层（领域归位 2026-10-15）：本文件在 common/PluginMarket/ —— **领域层**，与它驱动的
// PluginMarketModel / PluginMarketClient / PluginMarketInstaller 同层（过滤与分页等算法在
// PluginMarketModel，HTTP 与解析在 PluginMarketClient，这里只是本域的数据面与视图状态机）。
// 它不 import 任何 QML 概念（Q_PROPERTY / Q_INVOKABLE 只是元对象暴露方式，不依赖 QQml*），可单测。
// QML 面（`Hub.plugins` 属性、类型注册）在 qmlbridge/QmlBridge.* 与 src/core/main.cpp，那边只接线。
//
// 分层（2026-10-15 清掉反向依赖）：本类**不直接依赖 core**。此前它的构造要 ServerManager*、.cpp 里
//    include 了 core/ServerManager.h（只为"装完市场包重启内置服务端"），那是 common → core 唯一的一处
//    反向依赖；现在改成**发信号** serverRestartRequested()，由接线层 qmlbridge/QmlBridge 接住并调
//    ClientRuntime::restartServer()，与 SettingsController 的 serverRestartRequested / stylesReset 同一范式。
//    原因：common/** 只放领域与纯逻辑，重启一个进程是宿主行为；领域层不认识 ServerManager，才谈得上
//    脱离 QML 引擎与服务端单测。发出时机不变：仍在 installFinished(ok) 的同一位置（直接连接 ⇒ 同步落地）。
//
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
	// api：出站客户端（市场数据走 /dsh-market/*）；服务端重启不在这里，见 serverRestartRequested()
	explicit PluginMarketController(DshApiClient* api, QObject* parent = nullptr);

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
	// 市场包装好了，请宿主重启内置服务端（新插件才会被加载）。
	// ⚠️ 本类在 common（领域层），**不许**直接 include/调用 core/ServerManager；这条信号由
	//    qmlbridge/QmlBridge 接住落地（搬家前它在 installFinished 里直调 m_server->restart()）。
	void serverRestartRequested();

private:
	static constexpr int kPageSize = 20;   // 与旧壳 PluginsManager 同值

	void setBusy(bool busy);
	void setStatus(const QString& text);
	void rebuild();

	// 读插件包自带的 registry 快照（dshmarket/data/registry-snapshot.json，约 598KB / 839 条）。
	// 打开市场时先用它把列表铺出来（毫秒级），网络那趟只在后台慢慢跑。
	// 找不到文件就返回 false，调用方保持原行为（等引擎）。
	bool loadLocalSnapshot();
	void updateBaseUrl();
	// /dsh-market/* 回 404 = 市场包没装：走 pnpm 自动安装（已在装 / 已存在时什么都不做）
	void startMarketInstall();

	DshApiClient* m_api = nullptr;
	PluginMarketClient* m_client = nullptr;
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
	// 是否已经拿到过一次 registry（打开市场时据此决定要不要重取：启动 1.8s 已预热过，
	// 打开时再取一次只会在数据回来时触发模型 reset + 委托批量重建 ⇒ 打开后 1.5s 卡一下）。
	bool m_haveRegistry = false;
	// 404 自动安装市场包**只试一次**：引擎没有 /dsh-market/* 时，装失败会回到 registryFailed
	// ⇒ 又触发安装 ⇒ 无限重试；而每轮 refresh() 都把状态打回 plugin_loading（"正在等待最新数据"）
	// ⇒ 界面看起来永远在等数据。加这个标记后只试一次，之后停在离线快照上。
	bool m_marketInstallTried = false;
};
