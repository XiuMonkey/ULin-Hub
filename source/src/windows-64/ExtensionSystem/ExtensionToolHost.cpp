// 见 ExtensionSystem/ExtensionToolHost.h：扩展工具链路的调度层（管道 → 线程池 → DllCaller → 回包）。

#include "ExtensionSystem/ExtensionToolHost.h"

#include "ExtensionSystem/DshNamedPipeBridge.h"
#include "ExtensionSystem/ExtensionDllLoader.h"

#include <QDebug>
#include <QLocalSocket>
#include <QThread>
#include <QThreadPool>

#include <functional>
#include <utility>

namespace
{
	// 线程池里的最小包装：QThreadPool 默认 autoDelete，run() 结束就 delete 它。
	// 用 std::function 是为了让任务体留在本文件里，不把 DllCaller 的调用细节暴露出去。
	class ToolCallRunnable : public QRunnable
	{
	public:
		explicit ToolCallRunnable(std::function<void()> work)
			: m_work(std::move(work))
		{
		}

		void run() override
		{
			if (m_work)
				m_work();
		}

	private:
		std::function<void()> m_work;
	};
}

ExtensionToolHost::ExtensionToolHost(QObject* parent)
	: QObject(parent)
	, m_bridge(new DshNamedPipeBridge(this))
	, m_pool(new QThreadPool(this))
{
	// 专用池：切不要用 QThreadPool::globalInstance() —— 那条池还要解析 HTTP 回包，
	// 让可能阻塞的 DLL 调用挤进去会把大回包解析（以及后续所有解析）饿死。
	m_pool->setMaxThreadCount(kMaxThreads);
	connect(m_bridge, &DshNamedPipeBridge::requestReceived,
		this, &ExtensionToolHost::onRequestReceived);
}

ExtensionToolHost::~ExtensionToolHost()
{
	stop();
	m_pool->waitForDone();   // 无超时：任务体捕获了 this，必须等它们彻底结束；DllCaller 析构本来也要等在途归零
}

bool ExtensionToolHost::start(const QString& profileDir, const QString& pipeName)
{
	if (m_running)
		return true;

	m_error.clear();
	m_profileDir = profileDir;

	if (m_profileDir.isEmpty()) {
		m_error = QStringLiteral("extension tool host: empty profile dir");
		qWarning().noquote() << "[ToolHost]" << m_error;
		return false;
	}

	// 先装载、后监听：服务端插件一连上就查到工具，避免"连接成功但工具不存在"的窗口。
	// 装载是 GUI 线程上的 QLibrary 操作（磁盘 IO + LoadLibrary），量小、可接受。
	ExtensionDllLoader::loadAll(&m_caller, m_profileDir);

	if (!m_bridge->start(pipeName)) {
		m_error = m_bridge->errorString();
		qWarning().noquote() << "[ToolHost] pipe listen failed:" << m_error;
		return false;
	}

	m_running = true;
	m_draining = false;

	const QStringList tools = m_caller.tools();
	qInfo().noquote() << QStringLiteral("[ToolHost] started pipe=%1 tools=%2 profile=%3")
		.arg(pipeName)
		.arg(tools.size())
		.arg(m_profileDir);
	return true;
}

void ExtensionToolHost::stop()
{
	if (!m_running)
		return;

	m_running = false;
	m_draining = true;

	m_bridge->stop();   // 先关管道：停收新请求。在途的仍会跑完，但 socket 已被 bridge 释放，回包会按"丢弃"处理
	drainPending();

	m_draining = false;
	qInfo().noquote() << "[ToolHost] stopped";
}

void ExtensionToolHost::reload()
{
	if (!m_running)
		return;

	// 暂停收新请求（onRequestReceived 会当场回错），等在途跑完再卸库 —— 卸载会 wait inFlight，
	// 但这里先把队列清空，避免"卸完库里又冒出排队中的调用"。
	m_draining = true;
	drainPending();

	m_caller.unloadLibrary();
	ExtensionDllLoader::loadAll(&m_caller, m_profileDir);

	m_draining = false;

	const QStringList tools = m_caller.tools();
	qInfo().noquote() << QStringLiteral("[ToolHost] reloaded tools=%1").arg(tools.size());
}

bool ExtensionToolHost::unloadExtension(const QString& name)
{
	if (name.isEmpty())
		return false;

	// removeExtension 内部：先摘描述符、再等在途归零、最后卸载只属于它的 DLL。
	// 在 GUI 线程上等（可能被一次长调用拖住）是可接受的：移除扩展本来就是用户主动动作。
	const bool removed = m_caller.removeExtension(name);
	qInfo().noquote() << QStringLiteral("[ToolHost] unload extension %1 -> %2")
		.arg(name, removed ? QStringLiteral("ok") : QStringLiteral("not loaded"));
	return removed;
}

bool ExtensionToolHost::drainPending()
{
	const bool drained = m_pool->waitForDone(kDrainTimeoutMs);
	if (!drained) {
		qWarning().noquote() << QStringLiteral(
			"[ToolHost] drain timeout (%1ms): still-running tool calls are not killed")
			.arg(kDrainTimeoutMs);
	}
	return drained;
}

QStringList ExtensionToolHost::loadedTools() const
{
	return m_caller.tools();
}

int ExtensionToolHost::inFlight() const
{
	return m_inFlight.loadRelaxed();
}

void ExtensionToolHost::onRequestReceived(int id, const QString& tool, const QJsonObject& args,
	QLocalSocket* socket)
{
	const QPointer<QLocalSocket> guard(socket);

	if (!m_running || m_draining || !m_pool) {
		respond(id, false, QJsonObject(),
			QStringLiteral("extension tool host is not accepting requests"), guard);
		return;
	}

	++m_inFlight;

	auto* task = new ToolCallRunnable([this, id, tool, args, guard]() {   // 任务体只碰 DllCaller 与值类型；回投 GUI 线程后才写 socket（QLocalSocket 属于主线程）
		QJsonObject result;
		QString error;
		const bool ok = m_caller.callTool(tool, args, result, &error);

		QMetaObject::invokeMethod(this, [this, id, ok, result, error, guard]() {
			--m_inFlight;
			respond(id, ok, result, error, guard);
		}, Qt::QueuedConnection);
	});

	m_pool->start(task);
}

void ExtensionToolHost::respond(int id, bool ok, const QJsonObject& result, const QString& error,
	const QPointer<QLocalSocket>& socket)
{
	// 服务端断开（重启/崩溃）时在途请求会走到这里：没有对端可写，记一行就够。
	// state 检查是必要的：stop() 之后 socket 只是 deleteLater，指针还活着但已不可写。
	if (socket.isNull() || socket->state() != QLocalSocket::ConnectedState) {
		qWarning().noquote() << QStringLiteral("[ToolHost] response %1 dropped (socket gone): %2")
			.arg(id)
			.arg(error);
		return;
	}

	m_bridge->sendResponse(socket, id, ok, result, error);
}
