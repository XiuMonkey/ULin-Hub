#pragma once

#include "common/agent/ulincore/executor/UlinCoreExecutor.h"
#include "common/agent/ulincore/intent/UlinCoreIntent.h"

#include <functional>

namespace UlinCore
{
// 把一棵 Intent（Native 或 Chain）执行成结果。它只做执行，不声明 name/schema。
class UlinCoreIntentExecutor : public UlinCoreExecutor
{
public:
	using IntentResolver = std::function<Intent(const QString&)>;

	UlinCoreIntentExecutor(Intent intent, UlinCoreExecutorRegistry* executors,
		IntentResolver resolver);

	ToolResult execute(const QJsonObject& arguments, const ExecutorContext& context) override;

private:
	Intent m_intent;
	UlinCoreExecutorRegistry* m_executors = nullptr;
	IntentResolver m_resolve;
};
} // namespace UlinCore
