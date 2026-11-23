#pragma once
// QML 暴露面：组合 common 服务，不继承、不持有 Hub。
#include "qmlbridge/BridgeCommon.h"

// 工具开关列表的 QML 面：按目录分组的行数据 + 单项/整组可见性。
class ToolsFilterBridge : public QAbstractListModel
{
	Q_OBJECT
	Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
	Q_PROPERTY(QString status READ status NOTIFY statusChanged)
	Q_PROPERTY(bool loaded READ loaded NOTIFY loadedChanged)
	Q_PROPERTY(int visibleCount READ visibleCount NOTIFY summaryChanged)
	Q_PROPERTY(int totalCount READ totalCount NOTIFY summaryChanged)
	Q_PROPERTY(bool degraded READ degraded NOTIFY loadedChanged)

public:
	explicit ToolsFilterBridge(ToolsFilter* model, QObject* parent = nullptr);

	bool busy() const { return m_model->busy(); }
	QString status() const { return m_model->status(); }
	bool loaded() const { return m_model->loaded(); }
	int visibleCount() const { return m_model->visibleCount(); }
	int totalCount() const { return m_model->totalCount(); }
	bool degraded() const { return m_model->degraded(); }

	int rowCount(const QModelIndex& parent = QModelIndex()) const override
	{
		return m_model->rowCount(parent);
	}
	QVariant data(const QModelIndex& index, int role) const override
	{
		return m_model->data(index, role);
	}
	QHash<int, QByteArray> roleNames() const override { return m_model->roleNames(); }

	Q_INVOKABLE void refresh(const QString& sessionId) { m_model->refresh(sessionId); }
	Q_INVOKABLE void save() { m_model->save(); }
	Q_INVOKABLE void toggleDirectory(const QString& directoryName)
	{
		m_model->toggleDirectory(directoryName);
	}
	Q_INVOKABLE void setToolVisible(const QString& toolName, bool visible)
	{
		m_model->setToolVisible(toolName, visible);
	}
	Q_INVOKABLE void setDirectoryHidden(const QString& directoryName, bool hidden)
	{
		m_model->setDirectoryHidden(directoryName, hidden);
	}
	Q_INVOKABLE void setAllVisible(bool visible) { m_model->setAllVisible(visible); }

signals:
	void busyChanged();
	void statusChanged();
	void loadedChanged();
	void summaryChanged();

private:
	ToolsFilter* m_model = nullptr;
};