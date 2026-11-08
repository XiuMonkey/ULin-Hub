#pragma once

// 客户端基本信息：谁都能拿的"客户端身份"常量，只依赖 <QtGlobal>。
// 为什么放在 common 根层（不在 appearance，也不在 core）：它的使用者分处不同层 ——
//  · ThemeManager / StyleResolver（common/appearance）：风格包门控；
//  · ExtensionLoader（ExtensionSystem）：扩展 ui/ 的界面契约门控；
//  · core（main.cpp）：向 QCoreApplication 登记版本号。
// appearance 与 ExtensionSystem 是 common 下的下层模块，core 是宿主层。若把本文件放进
// core，下层就要 include 上层 —— 正是当年把 kUiAbi 从 StyleResolver.h 抽出来要解决的
// 依赖倒置，绕一圈又犯回来。放在 common 根层（与 util 平级），所有模块往下拿方向都是正的。
// 历史：kUiAbi 原先定义在 StyleResolver.h，抽成 appearance/UiAbi.h，
// 现在并入本文件并改名 ClientInfo —— 界面契约版本本来就是"客户端对外宣告的能力"之一。

#include <QtGlobal>

namespace ClientInfo
{
	// 客户端版本：与发布包名（如 "ULin-Hub-Alpha-0.1.0b"）对得上。
	// main.cpp 里会 QCoreApplication::setApplicationVersion(kVersion)，
	// 之后日志、关于页、helpdesk 对版本都从 applicationVersion 统一取，不再各处手抄。
	inline constexpr auto kVersion = "alpha-0.1.2a";

	// QML 风格包（= 界面树）与宿主之间的**契约版本**。
	// pack.json 的 uiAbi / 扩展 info.json5 的 ABIVersion 都对着它比。
	// ⚠️ 改这个值 = 宣布旧风格包 / 旧扩展的 ui 部分过期：皮肤包会整包拒载回退内置，
	//  客户端扩展则**只装 DLL、不落 ui/**（见 docs/codex-port-handover.md）。
	inline constexpr int kUiAbi = 3;
}
