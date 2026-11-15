#pragma once

// Debugger：集中管理调试/探针环境变量。
//
// 约定：
//  - 环境变量为空、0、false 视为关闭；其他非空值视为开启；
//  - 探针只写日志，不参与业务判断；
//  - 其他模块不要再直接调用 qEnvironmentVariable / qgetenv 判断探针，
//    统一走这里，方便查看、清理和统一命名。

#include <QString>

class Debugger
{
public:
	static bool enabled(const char* envName);
	static QString envValue(const char* envName, const QString& fallback = QString());

	// UI：帧级布局/滚动探针（ULINHUB_UI_PROBE）
	static bool uiProbeEnabled();

	// 附件/prompt 调试（DSH_ULIN_ATTACH_PROBE）
	static bool attachProbeEnabled();

	// 统一的探针日志格式：[tag] detail
	static void log(const QString& tag, const QString& detail);
};
