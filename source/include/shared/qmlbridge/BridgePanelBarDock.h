#pragma once
// QML 暴露面：组合 common 服务，不继承、不持有 Hub。
#include "qmlbridge/BridgeCommon.h"

class PanelBarDockBridge : public QAbstractListModel
{
	Q_OBJECT
	Q_PROPERTY(int count READ count NOTIFY countChanged)
	Q_PROPERTY(int activeIndex READ activeIndex NOTIFY activeIndexChanged)
	Q_PROPERTY(bool homeVisible READ homeVisible NOTIFY homeVisibleChanged)
	Q_PROPERTY(QString activeKind READ activeKind NOTIFY activeChanged)
	Q_PROPERTY(QString activeTitle READ activeTitle NOTIFY activeChanged)
	Q_PROPERTY(QString activeBlockId READ activeBlockId NOTIFY activeChanged)
	Q_PROPERTY(QString activeViewPath READ activeViewPath NOTIFY activeViewChanged)
	Q_PROPERTY(bool activeViewBlockScoped READ activeViewBlockScoped NOTIFY activeViewChanged)
	Q_PROPERTY(QString activeViewDetail READ activeViewDetail NOTIFY activeViewChanged)
	Q_PROPERTY(QString activeViewUrl READ activeViewUrl NOTIFY activeViewChanged)
	Q_PROPERTY(QVariantList launcherItems READ launcherItems NOTIFY launcherItemsChanged)
	Q_PROPERTY(QStringList detachedBlocks READ detachedBlocks NOTIFY detachedBlocksChanged)

public:
	static PanelBarDockBridge& instance();

	int count() const { return rowCount(); }
	int activeIndex() const { return PanelBarDockModel::instance().activeIndex(); }
	bool homeVisible() const { return PanelBarDockModel::instance().homeVisible(); }
	QString activeKind() const { return PanelBarDockModel::instance().activeKind(); }
	QString activeTitle() const { return PanelBarDockModel::instance().activeTitle(); }
	QString activeBlockId() const { return PanelBarDockModel::instance().activeBlockId(); }
	QString activeViewPath() const { return PanelBarDockModel::instance().activeViewPath(); }
	bool activeViewBlockScoped() const { return PanelBarDockModel::instance().activeViewBlockScoped(); }
	QString activeViewDetail() const { return PanelBarDockModel::instance().activeViewDetail(); }
	QString activeViewUrl() const { return PanelBarDockModel::instance().activeViewUrl(); }
	QVariantList launcherItems() const { return PanelBarDockModel::instance().launcherItems(); }
	QStringList detachedBlocks() const { return PanelBarDockModel::instance().detachedBlocks(); }

	int rowCount(const QModelIndex& parent = QModelIndex()) const override
	{
		return PanelBarDockModel::instance().rowCount(parent);
	}
	QVariant data(const QModelIndex& index, int role) const override
	{
		return PanelBarDockModel::instance().data(index, role);
	}
	QHash<int, QByteArray> roleNames() const override
	{
		return PanelBarDockModel::instance().roleNames();
	}

	Q_INVOKABLE void open(const QString& kind) { PanelBarDockModel::instance().open(kind); }
	Q_INVOKABLE QString openUrl(const QString& url, const QString& title)
	{
		return PanelBarDockModel::instance().openUrl(url, title);
	}
	Q_INVOKABLE void setBlockUrl(const QString& blockId, const QString& url)
	{
		PanelBarDockModel::instance().setBlockUrl(blockId, url);
	}
	Q_INVOKABLE bool closeBlock(const QString& blockId)
	{
		return PanelBarDockModel::instance().closeBlock(blockId);
	}
	Q_INVOKABLE QStringList blockIds() const
	{
		return PanelBarDockModel::instance().blockIds();
	}
	Q_INVOKABLE bool detachBlock(const QString& blockId)
	{
		return PanelBarDockModel::instance().detachBlock(blockId);
	}
	Q_INVOKABLE bool attachBlock(const QString& blockId)
	{
		return PanelBarDockModel::instance().attachBlock(blockId);
	}
	Q_INVOKABLE bool discardDetachedBlock(const QString& blockId)
	{
		return PanelBarDockModel::instance().discardDetachedBlock(blockId);
	}
	Q_INVOKABLE QVariantMap blockInfo(const QString& blockId) const
	{
		return PanelBarDockModel::instance().blockInfo(blockId);
	}
	Q_INVOKABLE void close(int index) { PanelBarDockModel::instance().close(index); }
	Q_INVOKABLE void activate(int index) { PanelBarDockModel::instance().activate(index); }
	Q_INVOKABLE void moveBlock(int from, int to)
	{
		PanelBarDockModel::instance().moveBlock(from, to);
	}
	Q_INVOKABLE void showHome() { PanelBarDockModel::instance().showHome(); }

signals:
	void countChanged();
	void activeIndexChanged();
	void homeVisibleChanged();
	void activeChanged();
	void activeViewChanged();
	void launcherItemsChanged();
	void detachedBlocksChanged();

private:
	explicit PanelBarDockBridge(QObject* parent = nullptr);
};