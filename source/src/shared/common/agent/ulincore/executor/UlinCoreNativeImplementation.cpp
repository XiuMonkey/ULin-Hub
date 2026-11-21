#include "common/agent/ulincore/executor/UlinCoreNativeImplementation.h"

#include "common/agent/ulincore/executor/UlinCoreBuiltinNativeImplementations.h"

#include <utility>

namespace UlinCore
{
QJsonObject UlinCoreNativeImplementation::schema() const
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

void UlinCoreNativeImplementationRegistry::add(std::shared_ptr<UlinCoreNativeImplementation> tool)
{
	if (!tool)
		return;
	const QString key = tool->name();
	m_tools.insert(key, std::move(tool));
}

void UlinCoreNativeImplementationRegistry::addBuiltins()
{
	registerBuiltinExecutorBackends(*this);
}

UlinCoreNativeImplementation* UlinCoreNativeImplementationRegistry::find(const QString& name) const
{
	const auto it = m_tools.constFind(name);
	return it == m_tools.constEnd() ? nullptr : it.value().get();
}

std::shared_ptr<UlinCoreNativeImplementation> UlinCoreNativeImplementationRegistry::findShared(const QString& name) const
{
	return m_tools.value(name);
}

QList<QString> UlinCoreNativeImplementationRegistry::names() const
{
	QList<QString> result;
	result.reserve(m_tools.size());
	for (auto it = m_tools.constBegin(); it != m_tools.constEnd(); ++it)
		result.append(it.key());
	result.sort();
	return result;
}

QJsonArray UlinCoreNativeImplementationRegistry::schemas() const
{
	QJsonArray result;
	QList<QString> keys = m_tools.keys();
	keys.sort();
	for (const QString& key : keys)
		result.append(m_tools.value(key)->schema());
	return result;
}
} // namespace UlinCore
