#include "qmlbridge/BridgeToolsFilter.h"

ToolsFilterBridge::ToolsFilterBridge(ToolsFilter* model, QObject* parent)
	: QAbstractListModel(parent)
	, m_model(model)
{
	// 源模型整体重建时桥也要走 begin/endResetModel，否则 QML 侧视图拿不到重置通知。
	connect(m_model, &QAbstractItemModel::modelAboutToBeReset, this, [this]() {
		beginResetModel();
	});
	connect(m_model, &QAbstractItemModel::modelReset, this, [this]() {
		endResetModel();
	});
	connect(m_model, &QAbstractItemModel::dataChanged, this, &ToolsFilterBridge::dataChanged);
	connect(m_model, &ToolsFilter::busyChanged, this, &ToolsFilterBridge::busyChanged);
	connect(m_model, &ToolsFilter::statusChanged, this, &ToolsFilterBridge::statusChanged);
	connect(m_model, &ToolsFilter::loadedChanged, this, &ToolsFilterBridge::loadedChanged);
	connect(m_model, &ToolsFilter::summaryChanged, this, &ToolsFilterBridge::summaryChanged);
}