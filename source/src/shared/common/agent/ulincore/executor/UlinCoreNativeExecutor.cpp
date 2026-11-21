#include "common/agent/ulincore/executor/UlinCoreNativeExecutor.h"

#include "common/agent/ulincore/executor/UlinCoreBuiltinNativeExecutors.h"

namespace UlinCore
{
void UlinCoreNativeRegistry::add(const QString& id, std::shared_ptr<UlinCoreNativeExecutor> executor)
{
	if (!id.isEmpty() && executor)
		m_executors.insert(id, std::move(executor));
}

void UlinCoreNativeRegistry::addBuiltins()
{
	registerBuiltinExecutors(*this);
}

std::shared_ptr<UlinCoreNativeExecutor> UlinCoreNativeRegistry::findShared(const QString& id) const
{
	return m_executors.value(id);
}
} // namespace UlinCore
