#pragma once
// QML 暴露面：组合 common 服务，不继承、不持有 Hub。
#include "qmlbridge/BridgeCommon.h"

#include <functional>

class SessionsBridge : public QAbstractListModel
{
	Q_OBJECT
	Q_PROPERTY(int count READ count NOTIFY rebuilt)
	Q_PROPERTY(QString filterText READ filterText WRITE setFilterText NOTIFY rebuilt)

public:
	using SelectSession = std::function<void(const QString&)>;
	using CreateSession = std::function<void(const QString&)>;

	explicit SessionsBridge(SessionListModel* model, SelectSession selectSession,
		CreateSession createSession, QObject* parent = nullptr);

	int count() const { return m_model->rowCount(); }
	QString filterText() const { return m_model->filterText(); }
	int rowCount(const QModelIndex& parent = QModelIndex()) const override
	{
		return m_model->rowCount(parent);
	}
	QVariant data(const QModelIndex& index, int role) const override
	{
		return m_model->data(index, role);
	}
	QHash<int, QByteArray> roleNames() const override { return m_model->roleNames(); }

	Q_INVOKABLE void setFilterText(const QString& text) { m_model->setFilterText(text); }
	Q_INVOKABLE void toggleWorkspace(const QString& workspaceId)
	{
		m_model->toggleWorkspace(workspaceId);
	}
	Q_INVOKABLE bool isWorkspaceExpanded(const QString& workspaceId) const
	{
		return m_model->isWorkspaceExpanded(workspaceId);
	}
	Q_INVOKABLE QVariantList workspaceItems() const { return m_model->workspaceItems(); }
	Q_INVOKABLE QString firstSessionForWorkspace(const QString& workspaceId) const
	{
		return m_model->firstSessionForWorkspace(workspaceId);
	}
	// UI 意图：切到工作区最近会话；没有则在该工作区新建。
	Q_INVOKABLE void activateWorkspace(const QString& workspaceId);

signals:
	void rebuilt();

private:
	SessionListModel* m_model = nullptr;
	SelectSession m_selectSession;
	CreateSession m_createSession;
};