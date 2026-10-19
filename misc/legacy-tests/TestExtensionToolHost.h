#pragma once

// 扩展工具管道链路的集成测试：不依赖真实 DLL，只验证
//   bridge 监听 → 收请求 → 线程池 → 回投 GUI → 回包
// 这条线的完整性（工具查不到时的错误也要按协议回）。

#include <QObject>

class TestExtensionToolHost : public QObject
{
	Q_OBJECT

private slots:
	// 空 profile：没有工具可装载；未知工具必须收到 ok=false + "unknown tool" 回包
	void unknownToolIsRejected();
	// 并发多请求：每个 id 都要有回包（验证池 + 回投 + id 配对），且最终在途为 0
	void concurrentRequestsAreAllAnswered();
	// stop() 之后管道必须关闭（新连接连不上），否则服务端重启会连到"假活"的旧实例
	void connectFailsAfterStop();
};
