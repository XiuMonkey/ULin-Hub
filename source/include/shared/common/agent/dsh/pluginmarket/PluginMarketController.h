#pragma once

// 分层：本文件在 common/agent/dsh/pluginmarket/ —— **领域层**，与它驱动的
// PluginMarketModel / PluginMarketClient / PluginMarketInstaller 同层（过滤与分页等算法在
// PluginMarketModel，HTTP 与解析在 PluginMarketClient，这里只是本域的数据面与视图状态机）。
// 它不 import 任何 QML 概念（Q_PROPERTY / 只是元对象暴露方式，不依赖 QQml*），可单测。
// QML 面（`Hub.plugins` 属性、类型注册）在 qmlbridge/QmlBridge.* 与 src/core/main.cpp，那边只接线。
// 分层：本类**不直接依赖 core**。此前它的构造要 ServerManager*、.cpp 里
//  include 了 core/ServerManager.h（只为"装完市场包重启内置服务端"），那是 common → core 唯一的一处
//  反向依赖；现在改成**发信号** serverRestartRequested，由接线层 qmlbridge/QmlBridge 接住并调
//  ClientRuntime::restartServer，与 SettingsController 的 serverRestartRequested / stylesReset 同一范式。
//  原因：common/** 只放领域与纯逻辑，重启一个进程是宿主行为；领域层不认识 ServerManager，才谈得上
//  脱离 QML 引擎与服务端单测。发出时机不变：仍在 installFinished(ok) 的同一位置（直接连接 ⇒ 同步落地）。
// 插件市场数据面（M6）：把既有的 PluginMarketClient / PluginMarketModel 接到 QML。
// 逻辑一行不重写：HTTP 与解析在 PluginMarketClient（/dsh-market/*），
// 过滤与分页在 PluginMarketModel（filter / paginate，每页 20 条），这里只做：
//  * 视图状态（当前页、搜索词、当前 Tab、状态文案）
//  * 把 registry / installed 的回包翻成 QML 好用的 QVariantList
//  * 操作（安装/更新/卸载/重启服务）与它们的反馈文案
// 文案全部用既有 qtTrId id（plugin_loading / plugin_loaded_fmt / plugin_action_failed_fmt …）。

#include "common/agent/dsh/pluginmarket/PluginMarketModel.h"
#include "VirtualClass/VirtualHostServices.h"

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVector>

class DshApiClient;
class PluginMarketClient;
class PluginMarketInstaller;
class QTimer;

class PluginMarketController : public QObject, public VirtualPluginMarketHost
{
	Q_OBJECT
		Q_INTERFACES(VirtualPluginMarketHost)
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
	Q_PROPERTY(QStringList blockedBuilds READ blockedBuilds NOTIFY blockedBuildsChanged)
	// 安装/更新/卸载任务：每条都有动作、目标、状态、消息、开始时间和日志。
	// 服务端没有百分比进度，界面用 startedAt 显示已耗时，避免只剩一句“已提交”。
	Q_PROPERTY(QVariantList installTasks READ installTasks NOTIFY installChanged)
	Q_PROPERTY(int activeTaskCount READ activeTaskCount NOTIFY installChanged)
	// 当前操作的结构化进度（轮询 /dsh-market/status；市场包自动安装时来自 pnpm 输出行）。
	// progressPercent < 0 = 服务端没给百分比 ⇒ 界面走不定态进度条。
	Q_PROPERTY(bool progressActive READ progressActive NOTIFY progressChanged)
	Q_PROPERTY(double progressPercent READ progressPercent NOTIFY progressChanged)
	Q_PROPERTY(QString progressText READ progressText NOTIFY progressChanged)
	Q_PROPERTY(int progressSeconds READ progressSeconds NOTIFY progressChanged)
	Q_PROPERTY(bool progressCancelling READ progressCancelling NOTIFY progressChanged)
	// 真实阶段：0=未知 1=解析依赖 2=下载 3=链接 4=构建（界面据此画 4 段阶段条，不用假百分比）
	Q_PROPERTY(int progressStage READ progressStage NOTIFY progressChanged)

public:
	// api：出站客户端（市场数据走 /dsh-market/*）；服务端重启不在这里，见 serverRestartRequested
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
	QStringList blockedBuilds() const { return m_blockedBuilds; }
	QVariantList installTasks() const { return m_installTasks; }
	int activeTaskCount() const { return m_activeTaskCount; }
	bool progressActive() const { return m_progressActive; }
	double progressPercent() const { return m_progressPercent; }
	QString progressText() const { return m_progressText; }
	int progressSeconds() const { return m_progressSeconds; }
	bool progressCancelling() const { return m_progressCancelling; }
	int progressStage() const { return m_progressStage; }

	void openPage();
	void closePage();
	void setTab(int tab);
	void setSearch(const QString& keyword);
	void refresh();
	void prevPage();
	void nextPage();
	void install(const QString& url);
	void update(const QString& name);
	void uninstall(const QString& name);
	void restartServer();
	void approveBuilds();

	// VirtualPluginMarketHost：只转调真实函数。
	bool PluginMarketInstalling() const override { return installing(); }
	int PluginMarketActiveTaskCount() const override { return activeTaskCount(); }
	void PluginMarketOpenPage() override { openPage(); }
	void PluginMarketClosePage() override { closePage(); }
	void PluginMarketRefresh() override { refresh(); }
	void PluginMarketInstall(const QString& url) override { install(url); }
	void PluginMarketUpdate(const QString& name) override { update(name); }
	void PluginMarketUninstall(const QString& name) override { uninstall(name); }

signals:
	void openChanged();
	void busyChanged();
	void statusChanged();
	void viewChanged();
	void installChanged();
	void progressChanged();
	void blockedBuildsChanged();
	// 市场包装好了，请宿主重启内置服务端（新插件才会被加载）。
	// ⚠️ 本类在 common（领域层），**不许**直接 include/调用 core/ServerManager；这条信号由
	// qmlbridge/QmlBridge 接住落地。
	void serverRestartRequested();

private:
	static constexpr int kPageSize = 20;   // 与现有实现 PluginsManager 同值

	void setBusy(bool busy);
	void setStatus(const QString& text);
	void rebuild();
	// 进度面：/dsh-market/status 轮询 + pnpm 输出行解析，统一收口到 setProgress。
	void applyMarketStatus(const QJsonObject& status);
	void setProgress(bool active, double percent, const QString& text, int seconds, bool cancelling, int stage = 0);
	void updateProgressPolling();
	// 市场包自己的操作日志（<profile>/.dsh-market/log.ndjson）最后一条：pnpm 启动前的阶段
	// （例如先从 GitHub 直接拉 commit 包）它不写 /status 的 progress，只能从这里拿真实阶段。
	QString marketLogStageText() const;
	int activeTaskElapsedSeconds() const;

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
	QVariantList m_installTasks;
	int m_activeTaskCount = 0;
	quint64 m_taskSeq = 0;
	qint64 m_installStartedAtMs = 0;   // 市场包自动安装（pnpm add dshmarket）的耗时基准

	QTimer* m_progressTimer = nullptr;   // 只在进行中任务存在时跑：700ms 轮询 /dsh-market/status
	bool m_progressActive = false;
	double m_progressPercent = -1.0;     // < 0 = 不定态
	QString m_progressText;
	int m_progressSeconds = 0;
	bool m_progressCancelling = false;
	int m_progressStage = 0;
	QString m_lastLoggedPhase;           // 阶段/百分比档变化才打一行日志（排障用，不刷屏）
	int m_lastLoggedPercent10 = -1;
	QString m_lastLoggedStageText;       // 市场日志兜底阶段的去重（日志/状态文案变化才打）

	void beginTask(const QString& action, const QString& name, const QString& url);
	void updateTask(const QString& id, const QString& state, const QString& message,
		bool appendLog = false, const QString& logLine = QString());
	QString activeTaskIdForPath(const QString& path) const;
	void finishTaskForPath(const QString& path, bool ok, const QString& message = QString());
	void recountActiveTasks();

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
	// ⇒ 又触发安装 ⇒ 无限重试；而每轮 refresh 都把状态打回 plugin_loading（"正在等待最新数据"）
	// ⇒ 界面看起来永远在等数据。加这个标记后只试一次，之后停在离线快照上。
	bool m_marketInstallTried = false;

	// pnpm 拦下的构建脚本 + 触发它的操作：批准后原样重试。
	QStringList m_blockedBuilds;
	QString m_pendingInstallUrl;
	QString m_pendingUpdateName;
};
