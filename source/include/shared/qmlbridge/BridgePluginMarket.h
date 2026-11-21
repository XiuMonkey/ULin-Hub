#pragma once
// QML 暴露面：组合 common 服务，不继承、不持有 Hub。
#include "qmlbridge/BridgeCommon.h"

class PluginMarketBridge : public QObject
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
	Q_PROPERTY(bool installing READ installing NOTIFY installChanged)
	Q_PROPERTY(QString installStatus READ installStatus NOTIFY installChanged)
	Q_PROPERTY(QStringList installLog READ installLog NOTIFY installChanged)
	Q_PROPERTY(QStringList blockedBuilds READ blockedBuilds NOTIFY blockedBuildsChanged)
	Q_PROPERTY(QVariantList installTasks READ installTasks NOTIFY installChanged)
	Q_PROPERTY(int activeTaskCount READ activeTaskCount NOTIFY installChanged)
	Q_PROPERTY(bool progressActive READ progressActive NOTIFY progressChanged)
	Q_PROPERTY(double progressPercent READ progressPercent NOTIFY progressChanged)
	Q_PROPERTY(QString progressText READ progressText NOTIFY progressChanged)
	Q_PROPERTY(int progressSeconds READ progressSeconds NOTIFY progressChanged)
	Q_PROPERTY(bool progressCancelling READ progressCancelling NOTIFY progressChanged)
	Q_PROPERTY(int progressStage READ progressStage NOTIFY progressChanged)

public:
	explicit PluginMarketBridge(PluginMarketController* controller, QObject* parent = nullptr);

	bool isOpen() const { return m_controller->isOpen(); }
	bool busy() const { return m_controller->busy(); }
	QString status() const { return m_controller->status(); }
	int tab() const { return m_controller->tab(); }
	QString search() const { return m_controller->search(); }
	QVariantList marketItems() const { return m_controller->marketItems(); }
	QVariantList installedItems() const { return m_controller->installedItems(); }
	QString pageLabel() const { return m_controller->pageLabel(); }
	bool canPrev() const { return m_controller->canPrev(); }
	bool canNext() const { return m_controller->canNext(); }
	bool installing() const { return m_controller->installing(); }
	QString installStatus() const { return m_controller->installStatus(); }
	QStringList installLog() const { return m_controller->installLog(); }
	QStringList blockedBuilds() const { return m_controller->blockedBuilds(); }
	QVariantList installTasks() const { return m_controller->installTasks(); }
	int activeTaskCount() const { return m_controller->activeTaskCount(); }
	bool progressActive() const { return m_controller->progressActive(); }
	double progressPercent() const { return m_controller->progressPercent(); }
	QString progressText() const { return m_controller->progressText(); }
	int progressSeconds() const { return m_controller->progressSeconds(); }
	bool progressCancelling() const { return m_controller->progressCancelling(); }
	int progressStage() const { return m_controller->progressStage(); }

	Q_INVOKABLE void openPage() { m_controller->openPage(); }
	Q_INVOKABLE void closePage() { m_controller->closePage(); }
	Q_INVOKABLE void setTab(int tab) { m_controller->setTab(tab); }
	Q_INVOKABLE void setSearch(const QString& keyword) { m_controller->setSearch(keyword); }
	Q_INVOKABLE void refresh() { m_controller->refresh(); }
	Q_INVOKABLE void prevPage() { m_controller->prevPage(); }
	Q_INVOKABLE void nextPage() { m_controller->nextPage(); }
	Q_INVOKABLE void install(const QString& url) { m_controller->install(url); }
	Q_INVOKABLE void update(const QString& name) { m_controller->update(name); }
	Q_INVOKABLE void uninstall(const QString& name) { m_controller->uninstall(name); }
	Q_INVOKABLE void restartServer() { m_controller->restartServer(); }
	Q_INVOKABLE void approveBuilds() { m_controller->approveBuilds(); }

signals:
	void openChanged();
	void busyChanged();
	void statusChanged();
	void viewChanged();
	void installChanged();
	void progressChanged();
	void blockedBuildsChanged();

private:
	PluginMarketController* m_controller = nullptr;
};