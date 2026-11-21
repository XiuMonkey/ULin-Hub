#include "qmlbridge/SessionsBridge.h"

SessionsBridge::SessionsBridge(SessionListModel* model, SelectSession selectSession,
	CreateSession createSession, QObject* parent)
	: QAbstractListModel(parent)
	, m_model(model)
	, m_selectSession(std::move(selectSession))
	, m_createSession(std::move(createSession))
{
	connect(m_model, &QAbstractItemModel::modelAboutToBeReset, this, [this]() {
		beginResetModel();
	});
	connect(m_model, &QAbstractItemModel::modelReset, this, [this]() {
		endResetModel();
	});
	connect(m_model, &SessionListModel::rebuilt, this, &SessionsBridge::rebuilt);
}

void SessionsBridge::activateWorkspace(const QString& workspaceId)
{
	const QString first = m_model->firstSessionForWorkspace(workspaceId);
	if (!first.isEmpty()) {
		if (m_selectSession)
			m_selectSession(first);
		return;
	}
	if (!m_createSession)
		return;
	const QString target = workspaceId == SessionListModel::ungroupedWorkspaceKey()
		? QString() : workspaceId;
	m_createSession(target);
}
