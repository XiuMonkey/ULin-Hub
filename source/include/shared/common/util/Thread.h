#pragma once

// 线程判定小工具：判断当前线程是否为 GUI 线程（QCoreApplication 所属线程）。

#include <QCoreApplication>
#include <QThread>

inline bool onGuiThread()
{
	const QCoreApplication* app = QCoreApplication::instance();
	return app && QThread::currentThread() == app->thread();
}
