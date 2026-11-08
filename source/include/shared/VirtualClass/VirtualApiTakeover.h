#pragma once

// 后端接管接口（API takeover）——宿主实现宿主侧，扩展实现接收侧。
//
// 设计约束：
//  - 接口方法顺序 = vtable 槽位顺序；本文件是 ABI 的唯一来源。
//  - 宿主和扩展必须使用同一份头文件；扩展侧副本由 extensions/_shared/include 同步。
//  - 修改本文件必须同时提升 ULINHUB_HOST_ABI_VERSION，旧扩展会被宿主拒绝。
//
// 方向：
//  插件 → 宿主：VirtualApiHost
//  宿主 → 插件：VirtualApiSink
//  入站数据注入：VirtualMain（VirtualCommon.h）

#include <qplugin.h>

class QObject;

// ---------------------------------------------------------------------------
// 插件 → 宿主
// ---------------------------------------------------------------------------
class VirtualApiHost
{
public:
	virtual ~VirtualApiHost() = default;

	// 接管开关：true = 停止内置后端并把出站交给扩展；false = 交还。
	// 宿主侧幂等；重复附着时可重复调用。
	virtual void Takenover(bool on) = 0;

	// 查询当前是否处于接管态。Takenover 无返回值，调用后用这个确认。
	virtual bool IsTakenover() const = 0;

	// 回填一次成功的出站调用：resultJson 是成功回调应拿到的 value（不是 server-response 信封）。
	// 不认识的 rpcId 只记日志（重复回填 / fire-and-forget 常见）。
	virtual void CompleteCall(const char* rpcId, const char* resultJson) = 0;

	// 回填一次失败的出站调用；code 为空时宿主使用 takenover-error。
	virtual void FailCall(const char* rpcId, const char* code, const char* message) = 0;

	// Master Agent 注册表：扩展在 attachHost 时注册，之后由设置页切换。
	// 不注册的扩展仍可直接走 Takenover(true) 的旧路径。
	virtual bool RegisterMasterAgent(const char* owner, QObject* agent) { Q_UNUSED(agent); return false; }
	virtual void UnregisterMasterAgent(const char* owner) { Q_UNUSED(owner); }
	virtual bool ActivateMasterAgent(const char* backendId) { Q_UNUSED(backendId); return false; }
	virtual void DeactivateMasterAgent() {}

	// 当前激活的 Master id；空串 = 旧的直接接管 / 未注册。
	// 扩展收到 Takenover(true) 时用它判断是不是自己被选中。
	virtual const char* ActiveMasterAgentId() const { return ""; }
};
Q_DECLARE_INTERFACE(VirtualApiHost, "com.ulinhub.VirtualApiHost/1.0")

// ---------------------------------------------------------------------------
// 宿主 → 插件：接管期间的出站请求接收端
// ---------------------------------------------------------------------------
class VirtualApiSink
{
public:
	virtual ~VirtualApiSink() = default;

	// 一次出站调用；argsJson 是一次性的，实现方需要自己拷贝。
	// 在 GUI 线程同步调用，不要做阻塞操作。
	virtual void OnOutboundRequest(const char* rpcId, const char* method, const char* argsJson) = 0;
};
Q_DECLARE_INTERFACE(VirtualApiSink, "com.ulinhub.VirtualApiSink/1.0")
