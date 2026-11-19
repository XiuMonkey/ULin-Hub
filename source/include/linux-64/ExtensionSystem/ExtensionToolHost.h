#pragma once

// 扩展工具宿主：把「DSH 服务端插件 → 命名管道 → 客户端 DllCaller」这条链接起来。
// 背景：Agent 看到的工具由 .ext 里的 AttachedPlugin（跑在 node 服务端）注册，但 DLL 跑在
// Qt 客户端进程里，两者之间是 \\.\pipe\ulinhub-bridge 上的行式 JSON：
//  {"id":1,"tool":"my_tool","args":{...}} → {"id":1,"ok":true,"result":{...}}
// ，这条链只剩下两端（DshNamedPipeBridge、DllCaller），中间没人接：
// 管道没人监听、requestReceived 没人收、callTool 没人调 —— 于是工具能注册、一调就超时。
// 本类就是补上的那一段，并顺手把「调度」从 DllCaller 里分离出来：
//  bridge.requestReceived ──▶ 本类 ──▶ 专用线程池 ──▶ DllCaller::callTool
//  ──▶ 回投 GUI 线程 ──▶ bridge.sendResponse
// 线程契约（改之前先读）：
//  · 本对象、bridge 的监听/停止、DllCaller 的装载/卸载，都只在 GUI 线程；
//  · 工作线程只调 DllCaller::callTool —— 它自带 per-DLL runMutex（同 DLL 串行）与
//  inFlight 计数（卸载时等在途归零），所以工作线程不需要再串行化；
//  · 结果一律回投 GUI 线程再写 socket（QLocalSocket 属于 GUI 线程，且 HostExports 也
//  只允许主线程碰宿主对象）；
//  · 停止/重载：先停收 → 清队列 → 等在执行的任务结束 → 才卸 DLL（顺序反了会卸掉
//  正在执行的库）。

#include "ExtensionSystem/DllCaller.h"

#include <QAtomicInt>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>

class DshNamedPipeBridge;
class QLocalSocket;
class QThreadPool;

class ExtensionToolHost : public QObject
{
	Q_OBJECT

public:
	explicit ExtensionToolHost(QObject* parent = nullptr);
	~ExtensionToolHost() override;

	// profileDir = <DSH_HOME>/profiles/web（extensions/ 与 node_modules/ 都在它下面）。
	// 先装载已安装扩展的描述/DLL，再启动管道监听。失败返回 false，原因见 errorString。
	// 幂等：已运行则直接返回 true。
	bool start(const QString& profileDir, const QString& pipeName = QStringLiteral("ulinhub-bridge"));
	// 停收 + 清队列 + 等在执行的任务（上限 kDrainTimeoutMs）→ 关管道。幂等。
	void stop();

	// 扩展安装/移除后重扫 profile：暂停收新请求 → 等在途 → 卸全部 → 重新装载 → 恢复。
	// 会短暂阻塞 GUI 线程（卸载本身还要等在途调用归零，见 DllCaller::unloadLibrary）。
	void reload();

	// 移除某个扩展前调用：卸载它的 DLL 并释放文件占用（Windows 不释放就删不掉目录）。
	// 返回 false = 该扩展本来就没被装载（不算错误，调用方照常删目录即可）。
	bool unloadExtension(const QString& name);

	bool isRunning() const { return m_running; }
	QString errorString() const { return m_error; }
	// 当前已装载扩展暴露的工具名（诊断/测试用；加锁读）
	QStringList loadedTools() const;
	// 已提交、还没回包的请求数（诊断用）
	int inFlight() const;

private:
	void onRequestReceived(int id, const QString& tool, const QJsonObject& args, QLocalSocket* socket);
	void respond(int id, bool ok, const QJsonObject& result, const QString& error,
		const QPointer<QLocalSocket>& socket);
	// 清掉还没开始的、等在执行的；返回是否在时限内排空
	bool drainPending();

	static constexpr int kDrainTimeoutMs = 3000;
	// 专用池上限：不同 DLL/COM 之间并行；同一个 DLL 仍由 DllCaller 的 runMutex 串行。
	//  从 4 调到 6：多扩展同时被调用时，4 条线程会被长调用占满、后面的请求排队。
	static constexpr int kMaxThreads = 6;

	DshNamedPipeBridge* m_bridge = nullptr;
	DllCaller m_caller;
	QThreadPool* m_pool = nullptr;
	QString m_profileDir;
	QString m_error;
	bool m_running = false;
	bool m_draining = false;
	QAtomicInt m_inFlight;
};
