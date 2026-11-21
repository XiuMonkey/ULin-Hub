#include "qmlbridge/BridgeToolsFilter.h"

ToolsFilterBridge::ToolsFilterBridge(ToolsFilter* model, QObject* parent)
	: QAbstractListModel(parent)
	, m_model(model)
{
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