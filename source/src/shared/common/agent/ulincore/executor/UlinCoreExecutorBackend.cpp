#include "common/agent/ulincore/executor/UlinCoreExecutorBackend.h"

#include "common/agent/ulincore/executor/UlinCoreBuiltinExecutorBackends.h"

#include <utility>

namespace UlinCore
{
QJsonObject UlinCoreExecutorBackend::schema() const
{
	return QJsonObject{
		{ QStringLiteral("type"), QStringLiteral("function") },
		{ QStringLiteral("function"), QJsonObject{
			{ QStringLiteral("name"), name() },
			{ QStringLiteral("description"), description() },
			{ QStringLiteral("parameters"), parametersSchema() }
		} }
	};
}

void UlinCoreExecutorBackendRegistry::add(std::shared_ptr<UlinCoreExecutorBackend> tool)
{
	if (!tool)
		return;
	const QString key = tool->name();
	m_tools.insert(key, std::move(tool));
}

void UlinCoreExecutorBackendRegistry::addBuiltins()
{
	registerBuiltinExecutorBackends(*this);
}

UlinCoreExecutorBackend* UlinCoreExecutorBackendRegistry::find(const QString& name) const
{
	const auto it = m_tools.constFind(name);
	return it == m_tools.constEnd() ? nullptr : it.value().get();
}

std::shared_ptr<UlinCoreExecutorBackend> UlinCoreExecutorBackendRegistry::findShared(const QString& name) const
{
	return m_tools.value(name);
}

QList<QString> UlinCoreExecutorBackendRegistry::names() const
{
	QList<QString> result;
	result.reserve(m_tools.size());
	for (auto it = m_tools.constBegin(); it != m_tools.constEnd(); ++it)
		result.append(it.key());
	result.sort();
	return result;
}

QJsonArray UlinCoreExecutorBackendRegistry::schemas() const
{
	QJsonArray result;
	QList<QString> keys = m_tools.keys();
	keys.sort();
	for (const QString& key : keys)
		result.append(m_tools.value(key)->schema());
	return result;
}
} // namespace UlinCore
