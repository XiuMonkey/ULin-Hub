#pragma once

#include "common/agent/ulincore/executor/UlinCoreNativeExecutor.h"

namespace UlinCore
{
// 注册内置执行器：builtin.read_file / builtin.list_dir /
// builtin.write_file / builtin.search_text。
void registerBuiltinExecutors(UlinCoreNativeRegistry& registry);
} // namespace UlinCore
