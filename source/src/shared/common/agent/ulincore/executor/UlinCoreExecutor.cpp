#include "common/agent/ulincore/executor/UlinCoreExecutor.h"

#include "common/agent/ulincore/executor/UlinCoreBuiltinExecutors.h"

namespace UlinCore
{
void UlinCoreExecutorRegistry::add(const QString& id, std::shared_ptr<UlinCoreExecutor> executor)
{
	if (!id.isEmpty() && executor)
		m_executors.insert(id, std::move(executor));
}

void UlinCoreExecutorRegistry::addBuiltins()
{
	registerBuiltinExecutors(*this);
}

std::shared_ptr<UlinCoreExecutor> UlinCoreExecutorRegistry::findShared(const QString& id) const
{
	return m_executors.value(id);
}
} // namespace UlinCore
