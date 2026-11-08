#pragma once

// 客户端扩展（QPlugin DLL）的生命周期接口。
// 命名规则：虚接口统一以 Virtual 开头；实现类保持 *Plugin（如 CodexBackendPlugin）。
//
// 必须全内联、无配套 .cpp，且不能派生 QObject。
// 插件 → 宿主的导出函数见 ExtensionSystem/HostExports.h。

#include <QObject>

class VirtualPlugin
{
public:
	virtual ~VirtualPlugin() = default;

	// 挂载：ABI 握手 → 取宿主对象 → 建控件/注册服务。
	// 会被多次调用（同名扩展二次装载会再 attach 一次），实现必须可重复挂载。
	virtual void attachHost() = 0;

	// 可选退场钩子，实现为槽 `detachHost`（不是虚方法，避免改 vtable）。
	// 实现它 = 允许宿主安全 unload DLL；必须同步摘掉自己挂在宿主里的一切。
};
Q_DECLARE_INTERFACE(VirtualPlugin, "com.ulinhub.VirtualPlugin/1.0")
