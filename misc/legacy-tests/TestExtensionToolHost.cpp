// 见 TestExtensionToolHost.h：扩展工具管道链路的集成测试（不依赖真实 DLL）。
//
// 覆盖的是链路本身，不是 DLL：工具名查不到时 DllCaller 会立刻失败并给出 "unknown tool"，
// 正好用来验证「管道收到请求 → 线程池 → 回投 GUI 线程 → 回包」这一段是通的。

#include "TestExtensionToolHost.h"

#include "ExtensionSystem/ExtensionToolHost.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QSet>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>

#include <functional>

namespace
{
	// 同一个 agent 可能还开着真实的 ULin Hub（它监听 ulinhub-bridge），测试必须用独立管道名
	QString uniquePipeName(const char* tag)
	{
		return QStringLiteral("ulinhub-bridge-test-%1-%2")
			.arg(QLatin1String(tag))
			.arg(QCoreApplication::applicationPid());
	}

	// 空 profile 目录的模板。
	// 不放在 %TEMP%：受限环境（CI/沙箱）里子进程拿到的临时目录可能是只读的，
	// QTemporaryDir 会直接 isValid()==false；exe 所在目录一定可写。
	QString testProfileTemplate()
	{
		return QCoreApplication::applicationDirPath() + QStringLiteral("/toolhost-test-XXXXXX");
	}

	// 从 socket 已到达的数据里切出第一条完整行（没有完整行返回 false）
	bool readOneLine(QLocalSocket& socket, QByteArray& buffer, QJsonObject* out)
	{
		buffer.append(socket.readAll());
		const int newline = buffer.indexOf('\n');
		if (newline < 0)
			return false;

		const QByteArray line = buffer.left(newline);
		buffer.remove(0, newline + 1);
		*out = QJsonDocument::fromJson(line).object();
		return true;
	}

	// 手动泵事件：管道两端都在本进程里，必须让事件循环跑起来才能推进。
	// 注意别换成 QTRY_VERIFY_*：它会把谓词**重复求值**，而 readOneLine 有副作用（消费一行）——
	// 成功那次把数据读走了，随后再求值就是 false，测试会白等满超时再判失败（实测踩过）。
	bool waitUntil(int timeoutMs, const std::function<bool()>& predicate)
	{
		QElapsedTimer timer;
		timer.start();
		while (timer.elapsed() < timeoutMs) {
			if (predicate())
				return true;
			QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
			QThread::msleep(10);
		}
		return predicate();
	}
}

void TestExtensionToolHost::unknownToolIsRejected()
{
	QTemporaryDir profileDir(testProfileTemplate());   // 空 profile：没有扩展可装载
	QVERIFY2(profileDir.isValid(), qPrintable(profileDir.errorString()));

	ExtensionToolHost host;
	const QString pipe = uniquePipeName("unknown");
	QVERIFY2(host.start(profileDir.path(), pipe), qPrintable(host.errorString()));
	QVERIFY(host.isRunning());
	QVERIFY(host.loadedTools().isEmpty());

	QLocalSocket socket;
	socket.connectToServer(pipe);
	QVERIFY2(socket.waitForConnected(3000), qPrintable(socket.errorString()));

	socket.write("{\"id\":7,\"tool\":\"does_not_exist\",\"args\":{}}\n");
	socket.flush();

	QByteArray buffer;
	QJsonObject response;
	QVERIFY(waitUntil(5000, [&]() { return readOneLine(socket, buffer, &response); }));

	QCOMPARE(response.value(QStringLiteral("id")).toInt(), 7);
	QVERIFY(!response.value(QStringLiteral("ok")).toBool());
	QVERIFY(response.value(QStringLiteral("error")).toString().contains(QStringLiteral("unknown tool")));

	host.stop();
}

void TestExtensionToolHost::concurrentRequestsAreAllAnswered()
{
	QTemporaryDir profileDir(testProfileTemplate());
	QVERIFY2(profileDir.isValid(), qPrintable(profileDir.errorString()));

	ExtensionToolHost host;
	const QString pipe = uniquePipeName("concurrent");
	QVERIFY2(host.start(profileDir.path(), pipe), qPrintable(host.errorString()));

	QLocalSocket socket;
	socket.connectToServer(pipe);
	QVERIFY2(socket.waitForConnected(3000), qPrintable(socket.errorString()));

	// 一次灌 8 条：Node 端本来就是异步多发的，回包允许乱序，但每个 id 都必须有
	constexpr int kRequests = 8;
	for (int i = 0; i < kRequests; ++i) {
		const QByteArray line = QStringLiteral("{\"id\":%1,\"tool\":\"nope_%1\",\"args\":{}}\n")
			.arg(100 + i)
			.toUtf8();
		socket.write(line);
	}
	socket.flush();

	QSet<int> answered;
	QByteArray buffer;
	QElapsedTimer timer;
	timer.start();
	while (answered.size() < kRequests && timer.elapsed() < 5000) {
		QJsonObject response;
		while (readOneLine(socket, buffer, &response)) {
			QVERIFY(!response.value(QStringLiteral("ok")).toBool());
			answered.insert(response.value(QStringLiteral("id")).toInt());
		}
		if (answered.size() < kRequests) {
			QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
			QThread::msleep(10);
		}
	}

	QCOMPARE(answered.size(), kRequests);
	// 回包全部到齐 ⇒ 每条任务的 GUI 线程收尾都跑完了，在途计数必须归零
	QCOMPARE(host.inFlight(), 0);

	host.stop();
}

void TestExtensionToolHost::connectFailsAfterStop()
{
	QTemporaryDir profileDir(testProfileTemplate());
	QVERIFY2(profileDir.isValid(), qPrintable(profileDir.errorString()));

	const QString pipe = uniquePipeName("stopped");
	ExtensionToolHost host;
	QVERIFY2(host.start(profileDir.path(), pipe), qPrintable(host.errorString()));
	QVERIFY(host.isRunning());

	host.stop();
	QVERIFY(!host.isRunning());

	// 管道必须真的关了：服务端重启后如果还连得上旧实例，工具调用会一直超时
	QLocalSocket socket;
	socket.connectToServer(pipe);
	QVERIFY(!socket.waitForConnected(1000));
}
