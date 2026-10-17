#pragma once

// QML 风格包（= 界面树）与宿主之间的**契约版本**。
//
// 为什么单独放一个头：这个值有三个使用者，而它们分处不同层 ——
//   · ThemeManager（common/appearance）：释放内置包时，磁盘上旧 ABI 的副本按"陈旧"处理；
//   · ExtensionLoader（ExtensionSystem）：扩展 ui/ 的界面契约门控（扩展 regulation.json5 的 ABIVersion）；
//   · StyleResolver（common/appearance）：读 pack.json 的 uiAbi，不一致就整包拒载、全部回退内置。
// 原先它定义在 StyleResolver.h 里，于是**下层**（appearance / ExtensionSystem）要 include 上层的
// QML 桥头文件 —— 方向是反的。抽到本文件后谁都能拿，且本文件只依赖 <QtGlobal>。
//
// ⚠️ 改这个值 = 宣布旧风格包 / 旧扩展的 ui 部分过期：皮肤包会整包拒载回退内置，
//    客户端扩展则**只装 DLL、不落 ui/**（见 docs/codex-port-handover.md）。

#include <QtGlobal>

namespace StylePack
{
	// 当前宿主实现的界面契约版本；pack.json 的 uiAbi / 扩展 regulation.json5 的 ABIVersion 都对着它比
	inline constexpr int kUiAbi = 3;
}

// 兼容别名：旧代码按 StyleResolver.h 里的名字引用它（逐处替换后再删）
inline constexpr int kStylePackUiAbi = StylePack::kUiAbi;
