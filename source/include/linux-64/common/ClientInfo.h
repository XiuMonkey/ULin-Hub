#pragma once

// Linux 平台层：客户端身份常量，只依赖 <QtGlobal>。放在 common 根层是因为使用者跨层
// （appearance 风格包、ExtensionSystem 扩展 ui/、core 版本号），放进 core 就成了下层 include 上层。

#include <QtGlobal>

namespace ClientInfo
{
	// 客户端版本：与发布包名（"ULin-Hub-Alpha-<kVersion>"）对得上。main.cpp 用它
	// setApplicationVersion，之后日志 / 关于页 / helpdesk 都从 applicationVersion 统一取。
	inline constexpr auto kVersion = "alpha-0.1.3a";
	inline constexpr auto kPlatform = "Linux-64";
	// 界面契约版本：风格包 / 扩展 ui/ 与本值不符时整包拒载（见 StyleResolver、ExtensionLoader）
	inline constexpr int kUiAbi  = 3;
}
