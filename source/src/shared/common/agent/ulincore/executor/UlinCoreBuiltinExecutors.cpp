#include "common/agent/ulincore/executor/UlinCoreExecutor.h"

#include "common/agent/ulincore/executor/UlinCoreBuiltinExecutors.h"
#include "common/agent/ulincore/executor/UlinCoreBuiltinExecutorBackends.h"
#include "common/agent/ulincore/executor/UlinCoreExecutorBackend.h"

#include <memory>

namespace UlinCore
{
namespace
{
class ToolExecutorAdapter : public UlinCoreExecutor
{
public:
	explicit ToolExecutorAdapter(std::shared_ptr<UlinCoreExecutorBackend> tool)
		: m_tool(std::move(tool))
	{
	}

	ToolResult execute(const QJsonObject& arguments, const ExecutorContext& context) override
	{
		ExecutorBackendContext legacy;
		legacy.workspaceRoot = context.workspaceRoot;
		legacy.sessionId = context.sessionId;
		legacy.turnId = context.turnId;
		legacy.allowWrites = context.allowWrites;
		legacy.allowShell = context.allowShell;
		return m_tool->execute(arguments, legacy);
	}

private:
	std::shared_ptr<UlinCoreExecutorBackend> m_tool;
};
}

void registerBuiltinExecutors(UlinCoreExecutorRegistry& registry)
{
	// 旧 tool 实现暂作为执行器后端；schema/name 已由 Intent 接管。
	UlinCoreExecutorBackendRegistry legacy;
	legacy.addBuiltins();
	for (const QString& name : legacy.names()) {
		const auto tool = legacy.findShared(name);
		if (tool)
			registry.add(QStringLiteral("builtin.") + name,
				std::make_shared<ToolExecutorAdapter>(tool));
	}
}
} // namespace UlinCore
