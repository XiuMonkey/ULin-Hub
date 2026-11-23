#pragma once

// 客户端身份常量：版本、平台与界面契约版本（kUiAbi）。放在 common 根层，
// 供 common/appearance、ExtensionSystem 与 core 共用；放进 core 会让下层反向依赖上层。

#include <QtGlobal>

namespace ClientInfo
{
	// 版本号与发布包名（如 "ULin-Hub-Alpha-0.1.3b"）一致；main.cpp 用它设置
	// QCoreApplication::applicationVersion，日志、关于页与 helpdesk 统一从这里取。
	inline constexpr auto kVersion = "alpha-0.1.3b";
	inline constexpr auto kPlatform = "Windows-64";
	// 界面契约版本：风格包与扩展 ui/ 的门控；不一致时风格包整包拒载、扩展只落 DLL。
	inline constexpr int kUiAbi  = 3;
}
