#pragma once

// 路径型环境变量的写入入口（qputenv）—— 一个坑的收口处，不是通用工具。
//
// 为什么必须集中：Windows 上 qputenv / qgetenv 走本地 8 位编码（中文系统 = GBK/CP936），
// 不是 UTF-8。用 toUtf8() 写入、Qt 再按本地编码读回，中文路径就成了乱码，
// WebEngine 首次创建视图时找不到子进程直接 qFatal 整个退出（用户侧表现："一打开浏览器就闪退"）。
// 所以统一写 toLocal8Bit()；Linux 在 UTF-8 locale 下两种写法等价，跨平台保持同一入口。
//
// 约定：只给**路径**用 —— 开关 / 标志串（QTWEBENGINE_CHROMIUM_FLAGS、ULINHUB_UI_PROBE 之类）不走这里。

#include <QDir>
#include <QString>
#include <QtGlobal>

// 设置一个路径型环境变量。内部做两件事：统一 Windows 风格分隔符；按本地 8 位编码写。
inline void setEnvPath(const char* name, const QString& path)
{
	qputenv(name, QDir::toNativeSeparators(path).toLocal8Bit());
}
