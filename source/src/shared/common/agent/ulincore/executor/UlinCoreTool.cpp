#include "common/agent/ulincore/executor/UlinCoreTool.h"

#include "common/agent/ulincore/executor/UlinCoreBuiltinTools.h"

#include <utility>

namespace UlinCore
{
QJsonObject UlinCoreTool::schema() const
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

void UlinCoreToolRegistry::add(std::shared_ptr<UlinCoreTool> tool)
{
	if (!tool)
		return;
	const QString key = tool->name();
	m_tools.insert(key, std::move(tool));
}

void UlinCoreToolRegistry::addBuiltins()
{
	registerBuiltinTools(*this);
}

UlinCoreTool* UlinCoreToolRegistry::find(const QString& name) const
{
	const auto it = m_tools.constFind(name);
	return it == m_tools.constEnd() ? nullptr : it.value().get();
}

std::shared_ptr<UlinCoreTool> UlinCoreToolRegistry::findShared(const QString& name) const
{
	return m_tools.value(name);
}

QList<QString> UlinCoreToolRegistry::names() const
{
	QList<QString> result;
	result.reserve(m_tools.size());
	for (auto it = m_tools.constBegin(); it != m_tools.constEnd(); ++it)
		result.append(it.key());
	result.sort();
	return result;
}

QJsonArray UlinCoreToolRegistry::schemas() const
{
	QJsonArray result;
	QList<QString> keys = m_tools.keys();
	keys.sort();
	for (const QString& key : keys)
		result.append(m_tools.value(key)->schema());
	return result;
}
} // namespace UlinCore
