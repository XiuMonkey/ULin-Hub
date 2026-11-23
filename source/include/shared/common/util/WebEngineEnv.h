#pragma once

// WebEngine 的**目录**环境变量（resources / locales）—— 两个平台入口共用。
//
// Qt 只认 <exe>/resources/，而本工程把 WebEngine 的运行时数据放在 <exe>/resources/webengine/
// （icudtl.dat、qtwebengine_resources*.pak、v8_context_snapshot.bin、locales/），
// 所以必须用 QTWEBENGINE_RESOURCES_PATH / QTWEBENGINE_LOCALES_PATH 指过去。
//
// 时机有讲究：WebEngine 首次建上下文时把这两个值读走并缓存，之后设无效，因此调用方要在
// QApplication 之前（按 argv[0] 推）和之后（按真实 exe 目录兜底）各调一次。
//
// 路径写入统一走 EnvPath.h —— 那里收口了 Windows 上 qputenv 的本地 8 位编码问题。

#include "common/util/EnvPath.h"

#include <QFileInfo>
#include <QString>

// <appDir>/resources/webengine 里确实有 qtwebengine_resources.pak 才算数；
// 返回空表示"没有内置副本"，调用方不要设变量，让 Qt 回落到默认的 <exe>/resources。
inline QString bundledWebEngineResourcesDir(const QString& appDir)
{
	if (appDir.isEmpty())
		return QString();
	const QString dir = appDir + QStringLiteral("/resources/webengine");
	return QFileInfo::exists(dir + QStringLiteral("/qtwebengine_resources.pak")) ? dir : QString();
}

// 按 appDir 设置 resources / locales 两个环境变量；目录不存在就保持不动。
inline void applyBundledWebEngineEnv(const QString& appDir)
{
	const QString resources = bundledWebEngineResourcesDir(appDir);
	if (resources.isEmpty())
		return;

	setEnvPath("QTWEBENGINE_RESOURCES_PATH", resources);
	const QString locales = resources + QStringLiteral("/locales");
	if (QFileInfo::exists(locales))
		setEnvPath("QTWEBENGINE_LOCALES_PATH", locales);
}
