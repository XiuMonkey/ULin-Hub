#pragma once

#include "common/agent/ulincore/intent/UlinCoreInvocation.h"

#include <QString>

namespace UlinCore
{
class UlinCoreParser
{
public:
	// 解析 /it 入口；普通文本返回 Prompt。
	// 解析本身不查 Intent 表，未知 intent 是否 fallback 由 Kernel 决定。
	static UlinCoreInvocation parse(const QString& rawText);
};
} // namespace UlinCore
