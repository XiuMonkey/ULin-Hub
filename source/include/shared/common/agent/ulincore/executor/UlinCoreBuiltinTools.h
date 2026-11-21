#pragma once

namespace UlinCore
{
class UlinCoreToolRegistry;

// 注册只读/受限写工具：read_file / list_dir / write_file / search_text。
void registerBuiltinTools(UlinCoreToolRegistry& registry);
} // namespace UlinCore
