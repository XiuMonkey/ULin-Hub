#include "common/util/Debugger.h"
#include "common/util/Format.h"
#include "common/util/Json.h"
#include "common/session/EventRecord.h"
#include "common/session/ChatController.h"
#include "common/session/MessageListModel.h"
#include "common/agent/AgentSessionClient.h"
#include "common/session/TraceModel.h"

#include "common/agent/dsh/DshApiClient.h"
#include "common/agent/AgentEventParser.h"
#include "common/util/ConnectionManager.h"
#include "network/SessionCommands.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QTemporaryFile>
#include <QUuid>
#include <QUrl>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <QVariantMap>

#include <algorithm>
#include "common/session/chat/ChatControllerInternal.h"

using namespace chatctl;

void ChatController::refreshModelCatalog()
{
	if (!m_api || m_sessionId.isEmpty())
		return;

	ModelSelectionService::fetch(m_api, m_sessionId,
		[this](const SessionModelDirectory& directory) {
			m_directory = directory;
			// 会话记录过的选择优先（provider/model 为空 = 服务端无记录，保持目录默认）
			if (!m_overrideProvider.isEmpty() && !m_overrideModel.isEmpty()) {
				if (m_directory.findModel(m_overrideProvider, m_overrideModel)) {
					m_directory.current.provider = m_overrideProvider;
					m_directory.current.model = m_overrideModel;
					if (!m_overrideEffort.isEmpty())
						m_directory.current.reasoningEffort = m_overrideEffort;
				}
				else {
					qInfo().noquote() << "[Chat] 会话记录的模型不在目录里:" << m_overrideProvider << m_overrideModel;
				}
			}
			m_catalogLoaded = true;
			rebuildCatalog();
		},
		[this](const DshApiClient::RpcError& error) {
			// 失败不拦发送（未知，不是"没有模型"）；只清空目录让 chip 隐藏
			qWarning().noquote() << "[Chat] session/modelCatalog failed:" << error.code << error.message;
			m_directory = SessionModelDirectory();
			m_catalogLoaded = false;
			rebuildCatalog();
		});
}

// 目录摊成 QML 用的两级结构：provider → models，外加一组推理档位。
void ChatController::rebuildCatalog()
{
	QVariantList groups;
	for (const ModelProviderGroup& group : m_directory.groups) {
		QVariantMap groupRow;
		groupRow.insert(QStringLiteral("id"), group.id);
		groupRow.insert(QStringLiteral("name"), group.name);

		QVariantList models;
		for (const ModelOption& option : group.models) {
			QVariantMap row;
			row.insert(QStringLiteral("id"), option.id);
			row.insert(QStringLiteral("name"), option.name);
			// 副标题：说明为空时退回 id
			row.insert(QStringLiteral("description"), option.description.isEmpty() ? option.id : option.description);
			row.insert(QStringLiteral("selected"), m_directory.current.provider == group.id && m_directory.current.model == option.id);
			models.append(row);
		}
		groupRow.insert(QStringLiteral("models"), models);
		groups.append(groupRow);
	}
	m_modelGroups = groups;

	QVariantList levels;
	for (const ReasoningLevel& level : m_directory.selectableLevels()) {
		QVariantMap row;
		row.insert(QStringLiteral("id"), level.id);
		row.insert(QStringLiteral("name"), level.name);
		row.insert(QStringLiteral("selected"), level.id == currentLevelId());
		levels.append(row);
	}
	m_reasoningLevels = levels;

	emit catalogChanged();
}

QString ChatController::currentLevelId() const
{
	const ReasoningLevel* level = m_directory.currentLevel();
	return level ? level->id : m_directory.current.reasoningEffort;
}

QString ChatController::currentModelName() const
{
	return m_directory.currentModelName();
}

QString ChatController::currentLevelName() const
{
	return m_directory.currentLevelName();
}

bool ChatController::useGenericLevels() const
{
	return m_directory.usesFallbackLevels();
}

bool ChatController::hasModels() const
{
	return m_catalogLoaded && !m_directory.groups.isEmpty();
}

void ChatController::selectModel(const QString& provider, const QString& model)
{
	if (!m_api || m_sessionId.isEmpty() || provider.isEmpty() || model.isEmpty())
		return;

	ModelSelection selection;  // 换模型清空档位（交给新模型的默认档位），成功后重拉目录
	selection.provider = provider;
	selection.model = model;

	ModelSelectionService::select(m_api, m_sessionId, selection,
		[this](const ModelSelection& selected) {
			m_overrideProvider = selected.provider;
			m_overrideModel = selected.model;
			m_overrideEffort = selected.reasoningEffort;
			refreshModelCatalog();
		},
		[this](const DshApiClient::RpcError& error) {
			qWarning().noquote() << "[Chat] session/selectModel failed:" << error.code << error.message;
			refreshModelCatalog();
		});
}

void ChatController::selectLevel(const QString& levelId)
{
	if (!m_api || m_sessionId.isEmpty() || !m_directory.current.isValid())
		return;

	// 换档位保留 provider/model，只改 reasoningEffort，不重拉目录
	ModelSelection selection = m_directory.current;
	selection.reasoningEffort = levelId;

	ModelSelectionService::select(m_api, m_sessionId, selection,
		[this](const ModelSelection& selected) {
			m_overrideEffort = selected.reasoningEffort;
			m_directory.current.reasoningEffort = selected.reasoningEffort;
			rebuildCatalog();
		},
		[this](const DshApiClient::RpcError& error) {
			qWarning().noquote() << "[Chat] session/selectModel (level) failed:" << error.code << error.message;
		});
}

void ChatController::applySessionModelSelection(const QString& provider, const QString& model, const QString& reasoningEffort)
{
	m_overrideProvider = provider;
	m_overrideModel = model;
	m_overrideEffort = reasoningEffort;

	if (!provider.isEmpty() && !model.isEmpty())
		refreshModelCatalog();
}

