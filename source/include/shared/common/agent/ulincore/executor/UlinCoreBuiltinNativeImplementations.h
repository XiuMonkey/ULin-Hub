#pragma once

namespace UlinCore
{
class UlinCoreNativeImplementationRegistry;

// 注册只读/受限写工具：read_file / list_dir / write_file / search_text。
void registerBuiltinNativeImplementations(UlinCoreNativeImplementationRegistry& registry);
} // namespace UlinCore
