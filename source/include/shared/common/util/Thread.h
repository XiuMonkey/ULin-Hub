#pragma once

// 线程判定小工具：GUI 线程（QCoreApplication 所属线程）判断。
// 原先 windows-64 / linux-64 两份 ClientExtension.cpp 各有一份逐字相同的实现。

#include <QCoreApplication>
#include <QThread>

inline bool onGuiThread()
{
	const QCoreApplication* app = QCoreApplication::instance();
	return app && QThread::currentThread() == app->thread();
}
