#pragma once

// 路径型环境变量的写入入口（qputenv）—— 一个坑的收口处，不是通用工具。
//
// 为什么必须集中：Windows 上 qputenv / qgetenv 走**本地 8 位编码**（中文系统 = GBK，CP936），
// 不是 UTF-8。用 toUtf8() 写入、Qt 再按本地编码读回，路径就成乱码：
//   D:\新建文件夹\bin\dependence\QtWebEngineProcess.exe
//   -> D:\鏂板缓鏂囦欢澶筡bin\dependence\QtWebEngineProcess.exe
// WebEngine 在首次创建视图时找不到子进程，直接 qFatal 整个进程退出
// （fatal.log："The following paths were searched for Qt WebEngine Process ... could not find it"；
//  用户侧表现："一打开浏览器就闪退"）。
// 所以统一写 toLocal8Bit()，qgetenv / qEnvironmentVariable 读回来才是原路径。
// Linux 上 toLocal8Bit() 与 toUtf8() 在 UTF-8 locale 下等价，跨平台保持同一入口。
//
// 约定：只给**路径**用。开关 / 标志串（QTWEBENGINE_CHROMIUM_FLAGS、ULINHUB_UI_PROBE 之类）
// 不要走这里，它们不经过文件名解码。

#include <QDir>
#include <QString>
#include <QtGlobal>

// 设置一个路径型环境变量。内部做两件事：统一 Windows 风格分隔符；按本地 8 位编码写。
inline void setEnvPath(const char* name, const QString& path)
{
	qputenv(name, QDir::toNativeSeparators(path).toLocal8Bit());
}
