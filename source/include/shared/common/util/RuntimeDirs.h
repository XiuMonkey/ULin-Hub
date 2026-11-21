#pragma once

// 运行时目录契约（两个平台入口共用）：
//   <exe>/data          持久化（client / ulincore 子目录）
//   <exe>/temp          临时根
//   <exe>/temp/run-<uuid>  本次进程专属临时目录，通过 ULINHUB_TEMP_RUN 发布给 C++ / 扩展
//
// 不用 %LOCALAPPDATA% / %TEMP%：沙箱下可能不可写，也会让数据分裂到用户目录。
// 启动时顺带清掉上次进程遗留的 run-*（只清 run-*，data/ 永不碰）。

#include "common/util/EnvPath.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QString>
#include <QUuid>

struct RuntimeDirs
{
	QString dataRoot;   // <exe>/data
	QString tempRoot;   // <exe>/temp
	QString runDir;     // <exe>/temp/run-<uuid>
};

// 建目录 + 清理旧 run-* + 发布 ULINHUB_TEMP_RUN。必须在 QApplication 之后调用
// （applicationDirPath() 这时才可靠）。
inline RuntimeDirs prepareRuntimeDirs()
{
	RuntimeDirs dirs;
	const QString appDir = QCoreApplication::applicationDirPath();

	dirs.dataRoot = appDir + QStringLiteral("/data");
	QDir().mkpath(dirs.dataRoot + QStringLiteral("/client"));
	QDir().mkpath(dirs.dataRoot + QStringLiteral("/ulincore"));

	dirs.tempRoot = appDir + QStringLiteral("/temp");
	QDir().mkpath(dirs.tempRoot);

	dirs.runDir = dirs.tempRoot + QStringLiteral("/run-")
		+ QUuid::createUuid().toString(QUuid::WithoutBraces);
	QDir().mkpath(dirs.runDir);
	setEnvPath("ULINHUB_TEMP_RUN", dirs.runDir);

	const QFileInfoList stale = QDir(dirs.tempRoot).entryInfoList(
		{ QStringLiteral("run-*") }, QDir::Dirs | QDir::NoDotAndDotDot);
	const QString currentRun = QDir(dirs.runDir).absolutePath();
	for (const QFileInfo& info : stale) {
		if (info.absoluteFilePath() != currentRun)
			QDir(info.absoluteFilePath()).removeRecursively();
	}

	return dirs;
}
