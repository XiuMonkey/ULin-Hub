#pragma once

namespace UlinCore
{
class UlinCoreNativeImplementationRegistry;

// 注册只读/受限写工具：read_file / list_dir / write_file / search_text。
void registerBuiltinExecutorBackends(UlinCoreNativeImplementationRegistry& registry);
} // namespace UlinCore
