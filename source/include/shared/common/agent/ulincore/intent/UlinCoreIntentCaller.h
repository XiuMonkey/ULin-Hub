#pragma once

#include "common/agent/ulincore/executor/UlinCoreNativeExecutor.h"
#include "common/agent/ulincore/intent/UlinCoreIntent.h"

#include <functional>

namespace UlinCore
{
// 调用一棵 Intent（Native 或 Chain）。call 是 Intent 语义，不是原生 execute。
class UlinCoreIntentCaller
{
public:
	using IntentResolver = std::function<Intent(const QString&)>;

	UlinCoreIntentCaller(Intent intent, UlinCoreNativeRegistry* executors,
		IntentResolver resolver);

	IntentResult call(const QJsonObject& arguments, const ExecutorContext& context);

private:
	Intent m_intent;
	UlinCoreNativeRegistry* m_executors = nullptr;
	IntentResolver m_resolve;
};
} // namespace UlinCore
