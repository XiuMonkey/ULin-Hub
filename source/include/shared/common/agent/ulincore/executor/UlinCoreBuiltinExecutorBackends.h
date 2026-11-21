#pragma once

namespace UlinCore
{
class UlinCoreExecutorBackendRegistry;

// 注册只读/受限写工具：read_file / list_dir / write_file / search_text。
void registerBuiltinExecutorBackends(UlinCoreExecutorBackendRegistry& registry);
} // namespace UlinCore
