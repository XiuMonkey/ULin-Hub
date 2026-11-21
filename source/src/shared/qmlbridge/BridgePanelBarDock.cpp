#include "qmlbridge/BridgePanelBarDock.h"

PanelBarDockBridge& PanelBarDockBridge::instance()
{
	static PanelBarDockBridge bridge;
	return bridge;
}

PanelBarDockBridge::PanelBarDockBridge(QObject* parent)
	: QAbstractListModel(parent)
{
	const PanelBarDockModel& base = PanelBarDockModel::instance();
	connect(&base, &PanelBarDockModel::countChanged, this, &PanelBarDockBridge::countChanged);
	connect(&base, &PanelBarDockModel::activeIndexChanged, this, &PanelBarDockBridge::activeIndexChanged);
	connect(&base, &PanelBarDockModel::homeVisibleChanged, this, &PanelBarDockBridge::homeVisibleChanged);
	connect(&base, &PanelBarDockModel::activeChanged, this, &PanelBarDockBridge::activeChanged);
	connect(&base, &PanelBarDockModel::activeViewChanged, this, &PanelBarDockBridge::activeViewChanged);
	connect(&base, &PanelBarDockModel::launcherItemsChanged, this, &PanelBarDockBridge::launcherItemsChanged);
	connect(&base, &PanelBarDockModel::detachedBlocksChanged, this, &PanelBarDockBridge::detachedBlocksChanged);
	// QML 的 Repeater/ListView 监听的是桥自己的模型信号；基类插删行不会自动跨对象转发。
	connect(&base, &QAbstractItemModel::rowsInserted, this, &PanelBarDockBridge::rowsInserted);
	connect(&base, &QAbstractItemModel::rowsRemoved, this, &PanelBarDockBridge::rowsRemoved);
	connect(&base, &QAbstractItemModel::rowsMoved, this, &PanelBarDockBridge::rowsMoved);
	connect(&base, &QAbstractItemModel::modelReset, this, &PanelBarDockBridge::modelReset);
	connect(&base, &QAbstractItemModel::dataChanged, this, &PanelBarDockBridge::dataChanged);
}