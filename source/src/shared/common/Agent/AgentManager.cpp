#include "common/Agent/AgentManager.h"

#include "common/settings/ClientSettings.h"

#include <QDebug>
#include <QJsonObject>
#include <QVariantMap>

AgentManager::AgentManager(QObject* parent)
	: QObject(parent)
{
}

const AgentManager::Entry* AgentManager::find(const QString& id) const
{
	const auto it = m_entries.constFind(id);
	return it == m_entries.constEnd() ? nullptr : &it.value();
}

bool AgentManager::registerAgent(const QString& owner, QObject* agent)
{
	if (!agent)
		return false;
	auto* master = dynamic_cast<VirtualMasterAgent*>(agent);
	if (!master)
		return false;
	const QString id = QString::fromUtf8(master->MasterAgentId()).trimmed();
	if (id.isEmpty() || id == QLatin1String("dsh"))
		return false;

	Entry entry;
	entry.owner = owner;
	entry.id = id;
	entry.name = QString::fromUtf8(master->MasterAgentName()).trimmed();
	entry.description = QString::fromUtf8(master->MasterAgentDescription()).trimmed();
	entry.object = agent;
	m_entries.insert(id, entry);
	qInfo().noquote() << "[AgentManager] registered:" << id << entry.name
		<< "owner=" << owner;
	emitRegistered();
	return true;
}

void AgentManager::unregisterOwner(const QString& owner)
{
	QStringList removed;
	for (auto it = m_entries.constBegin(); it != m_entries.constEnd(); ++it) {
		if (it->owner == owner)
			removed << it.key();
	}
	if (removed.isEmpty())
		return;

	// 卸载扩展：只清 active 账，不回调 provider Deactivate。
	// 扩展自己的 detachHost 负责停后端；这里回调会在 WorkBuddy 的 Deactivate 里
	// 再次 Unregister，形成重入。
	if (removed.contains(m_activeId)) {
		m_activeId.clear();
		emit activeChanged(QString());
	}
	for (const QString& id : removed)
		m_entries.remove(id);
	emitRegistered();
}

QVariantList AgentManager::registeredAgents() const
{
	QVariantList rows;
	for (const Entry& entry : m_entries) {
		QVariantMap row;
		row.insert(QStringLiteral("id"), entry.id);
		row.insert(QStringLiteral("name"), entry.name.isEmpty() ? entry.id : entry.name);
		row.insert(QStringLiteral("description"), entry.description);
		rows.append(row);
	}
	return rows;
}

VirtualApiSink* AgentManager::activeSink() const
{
	if (m_activeId.isEmpty())
		return nullptr;
	const Entry* entry = find(m_activeId);
	if (!entry || entry->object.isNull())
		return nullptr;
	return qobject_cast<VirtualApiSink*>(entry->object.data());
}

bool AgentManager::activate(const QString& backendId)
{
	const QString id = backendId.trimmed();
	if (id.isEmpty() || id == QLatin1String("dsh")) {
		deactivate();
		return true;
	}
	const Entry* entry = find(id);
	if (!entry || entry->object.isNull())
		return false;
	auto* master = dynamic_cast<VirtualMasterAgent*>(entry->object.data());
	if (!master)
		return false;
	if (m_activeId == id)
		return true;

	// 切换事务：先收旧后端，再起新后端。扩展对象本身一直在，只换后台会话。
	deactivate();
	m_activeId = id;
	master->ActivateMasterAgent();
	emit activeChanged(id);
	qInfo().noquote() << "[AgentManager] activated:" << id;
	return true;
}

void AgentManager::deactivate()
{
	const QString old = m_activeId;
	if (old.isEmpty())
		return;
	m_activeId.clear();
	if (const Entry* entry = find(old)) {
		if (auto* master = dynamic_cast<VirtualMasterAgent*>(entry->object.data()))
			master->DeactivateMasterAgent();
	}
	emit activeChanged(QString());
	qInfo().noquote() << "[AgentManager] deactivated:" << old;
}

void AgentManager::clear()
{
	deactivate();
	m_entries.clear();
	emitRegistered();
}

void AgentManager::emitRegistered()
{
	emit registeredChanged();
}

QString AgentManager::selectedId() const
{
	const QString saved = ClientSettings::read(QStringLiteral("AgentSetting.json"))
		.value(QStringLiteral("masterBackend")).toString().trimmed();
	return saved.isEmpty() ? QStringLiteral("dsh") : saved;
}

void AgentManager::setSelectedId(const QString& backendId)
{
	const QString id = backendId.trimmed();
	if (id.isEmpty())
		return;
	QJsonObject object = ClientSettings::read(QStringLiteral("AgentSetting.json"));
	object.insert(QStringLiteral("masterBackend"), id);
	ClientSettings::write(QStringLiteral("AgentSetting.json"), object);
}
