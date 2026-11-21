#pragma once

// 进程级操作。目前只有"按 PID 连坐杀进程树"一个：内置引擎会起终端 shell、MCP 服务等子进程，
// 只杀 node 本身会留孙子。
//
// 只在 Windows 上实现：Linux 走 QProcess 的进程组（见 ServerManager 里 `::kill(-pgid, SIGKILL)`），
// 调用点本身也在 `#ifdef Q_OS_WIN` 里，所以这里同样用条件编译，Linux 上不定义这个函数。
//
// 用 taskkill /T /F 必须按 **PID** 点名，绝不按镜像名（用户可能自己跑着 dsh，按名字会误伤）。

#include <QString>
#include <QtGlobal>

#ifdef Q_OS_WIN

#include <QDebug>
#include <QFile>
#include <QProcess>

inline bool killProcessTree(qint64 pid, int waitMs)
{
	if (pid <= 0)
		return false;

	const QString systemRoot = qEnvironmentVariable("SystemRoot");
	const QString taskkillPath = systemRoot + QStringLiteral("/System32/taskkill.exe");
	QProcess killer;
	killer.setProgram(QFile::exists(taskkillPath) ? taskkillPath : QStringLiteral("taskkill"));
	killer.setArguments(QStringList{ QStringLiteral("/PID"), QString::number(pid),
		QStringLiteral("/T"), QStringLiteral("/F") });
	killer.setProcessChannelMode(QProcess::MergedChannels);
	killer.start();
	if (!killer.waitForStarted(waitMs))
		return false;

	const bool finished = killer.waitForFinished(waitMs);
	const int exitCode = finished ? killer.exitCode() : -1;
	qInfo().noquote() << QStringLiteral("[Process] taskkill /PID %1 /T /F -> exit=%2").arg(pid).arg(exitCode);
	return finished && exitCode == 0;
}

#endif   // Q_OS_WIN
