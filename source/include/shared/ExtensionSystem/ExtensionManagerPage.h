#pragma once

// 扩展管理页（原版 src/ui/ExtensionManagerPopup.cpp 的 QML 版），只做接线：
//  · 工具扩展 —— <DSH_HOME>/profiles/web（extensions.json + node_modules + cordis.patch.yml）
//  · 客户端扩展 —— <exe>/extensions/<Name>/（main.dll + info.json5 + ui/），进程内 QPluginLoader
// 领域逻辑一行不重写：解压安装是 ExtensionLoader（worker 线程）、进度由 ExtensionInstallTask 轮询、
// 清单读写与目录清理在 ExtensionRegistry / ClientExtension。本类只做三件事：
//  * 列表：两种来源合成 QML 好用的 QVariantList（行里带 kind / name / label / kindLabel / loaded）
//  * 操作：选文件安装、移除选中、刷新、清理残留、重启服务端，以及它们的反馈文案
//  * 视图状态：open / busy / status / selectedIndex
// 分层：成员直接持有 ExtensionSystem 的类型，而 ExtensionSystem 反向 include 了 common/util/*、
// common/ClientInfo.h —— 放进 common 会做成 common ↔ ExtensionSystem 的目录级环。
// 规矩：common/** = 领域与纯逻辑；qmlbridge/** = 只做接线。
// 文案沿用原版的 qtTrId id（ext_* / common_*），既有翻译直接生效。
// 与原版的两处差异（有意）：
//  1) 移除确认弹窗放在 QML（原版是 QMessageBox），本类只暴露 removeSelected；
//  2) 工具扩展增删后**不自动重启服务端**，改成状态行提示 + 页面上的「重启服务端」按钮，
//  免得在用户正聊天时被无声重启（原版是发 serverRestartRequested 直接重启）。

#include "ExtensionSystem/ExtensionInstallTask.h"
#include "ExtensionSystem/ExtensionRegistry.h"
#include "VirtualClass/VirtualHostServices.h"

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

class QTimer;
class ServerManager;
class ExtensionToolHost;

class ExtensionManagerPage : public QObject, public VirtualExtensionHost
{
	Q_OBJECT
		Q_INTERFACES(VirtualExtensionHost)
		Q_PROPERTY(bool open READ isOpen NOTIFY openChanged)
		Q_PROPERTY(QVariantList items READ items NOTIFY itemsChanged)
		Q_PROPERTY(QString status READ status NOTIFY statusChanged)
		Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
		Q_PROPERTY(int selectedIndex READ selectedIndex WRITE setSelectedIndex NOTIFY selectedIndexChanged)
		Q_PROPERTY(bool canRemove READ canRemove NOTIFY selectedIndexChanged)
		// 非空 = 刚才的改动要重启才完全生效（工具扩展重启服务端；客户端扩展重启应用）
		Q_PROPERTY(QString restartHint READ restartHint NOTIFY restartHintChanged)

public:
	// server：用来取 dshHome（工具扩展的 profile 就在它下面）与重启内置服务端；
	// toolHost：工具扩展装/卸后要它 reload/unload（卸 DLL 释放文件占用、重扫描述符）
	explicit ExtensionManagerPage(ServerManager* server, ExtensionToolHost* toolHost, QObject* parent = nullptr);
	~ExtensionManagerPage() override;

	bool isOpen() const { return m_open; }
	QVariantList items() const { return m_items; }
	QString status() const { return m_status; }
	bool busy() const { return m_busy; }
	int selectedIndex() const { return m_selected; }
	bool canRemove() const;
	QString restartHint() const { return m_restartHint; }

	void setSelectedIndex(int index);

	void openPage();
	void closePage();
	// 重新扫两种来源，重建列表与状态行（原版 refresh）
	void refresh();
	// 选一个 .ext 文件并后台安装；正在装时直接忽略（原版 installExtension）
	void installFromFile();
	// 移除当前选中项 —— QML 负责先弹确认；返回是否真的执行了移除
	bool removeSelected();
	// 清理 cordis.patch.yml 里指向不存在包的残留（原版 cleanupResiduals）
	void cleanupResiduals();
	// 工具扩展的增删要重启内置服务端才生效
	void restartServer();

	// VirtualExtensionHost：只转调真实函数。
	bool ExtensionOpen() const override { return isOpen(); }
	QVariantList ExtensionItems() const override { return items(); }
	bool ExtensionCanRemove() const override { return canRemove(); }
	QString ExtensionRestartHint() const override { return restartHint(); }
	void ExtensionOpenPage() override { openPage(); }
	void ExtensionClosePage() override { closePage(); }
	void ExtensionRefresh() override { refresh(); }
	void ExtensionInstallFromFile() override { installFromFile(); }
	bool ExtensionRemoveSelected() override { return removeSelected(); }
	void ExtensionCleanupResiduals() override { cleanupResiduals(); }
	void ExtensionRestartServer() override { restartServer(); }

signals:
	void openChanged();
	void itemsChanged();
	void statusChanged();
	void busyChanged();
	void selectedIndexChanged();
	void restartHintChanged();

private:
	void populate();
	void setStatus(const QString& text);
	void setBusy(bool busy);
	void setRestartHint(const QString& hint);
	// profile 路径要等 ServerManager 启动过才有值（m_dshHome 在 startBundledServer 里赋值），故每次现算、不缓存成成员
	ExtensionRegistry registry() const;
	QString kindLabel(const QString& kind) const;
	QVariantMap selectedItem() const;
	void pollInstall();       // 100ms 定时器：非阻塞取后台安装结果（原版 pollInstall）

	ServerManager* m_server = nullptr;
	ExtensionToolHost* m_toolHost = nullptr;
	ExtensionInstallTask m_installTask;
	QTimer* m_installTimer = nullptr;

	QVariantList m_items;
	QString m_status;
	bool m_busy = false;
	bool m_open = false;
	QString m_restartHint;
	int m_selected = -1;
};
