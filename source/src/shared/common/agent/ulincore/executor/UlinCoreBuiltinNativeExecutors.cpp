#include "common/agent/ulincore/executor/UlinCoreNativeExecutor.h"

#include "common/agent/ulincore/executor/UlinCoreBuiltinNativeExecutors.h"
#include "common/agent/ulincore/executor/UlinCoreBuiltinNativeImplementations.h"
#include "common/agent/ulincore/executor/UlinCoreNativeImplementation.h"

#include <memory>

namespace UlinCore
{
namespace
{
class ToolExecutorAdapter : public UlinCoreNativeExecutor
{
public:
	explicit ToolExecutorAdapter(std::shared_ptr<UlinCoreNativeImplementation> tool)
		: m_tool(std::move(tool))
	{
	}

	ExecuteResult execute(const QJsonObject& arguments, const ExecutorContext& context) override
	{
		NativeContext legacy;
		legacy.workspaceRoot = context.workspaceRoot;
		legacy.sessionId = context.sessionId;
		legacy.turnId = context.turnId;
		legacy.allowWrites = context.allowWrites;
		legacy.allowShell = context.allowShell;
		return m_tool->execute(arguments, legacy);
	}

private:
	std::shared_ptr<UlinCoreNativeImplementation> m_tool;
};
}

void registerBuiltinNativeExecutors(UlinCoreNativeRegistry& registry)
{
	// 旧 tool 实现暂作为执行器后端；schema/name 已由 Intent 接管。
	UlinCoreNativeImplementationRegistry legacy;
	legacy.addBuiltins();
	for (const QString& name : legacy.names()) {
		const auto tool = legacy.findShared(name);
		if (tool)
			registry.add(QStringLiteral("builtin.") + name,
				std::make_shared<ToolExecutorAdapter>(tool));
	}
}
} // namespace UlinCore
