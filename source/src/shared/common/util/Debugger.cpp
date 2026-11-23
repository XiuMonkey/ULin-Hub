#include "common/util/Debugger.h"

#include <QDebug>
#include <QtGlobal>

// 环境变量未设、为空、为 "0" 或 "false" 时都算关闭。
bool Debugger::enabled(const char* envName)
{
	if (envName == nullptr || *envName == '\0')
		return false;
	const QString value = qEnvironmentVariable(envName).trimmed();
	if (value.isEmpty())
		return false;
	if (value == QLatin1String("0") || value.compare(QLatin1String("false"), Qt::CaseInsensitive) == 0)
		return false;
	return true;
}

QString Debugger::envValue(const char* envName, const QString& fallback)
{
	if (envName == nullptr || *envName == '\0')
		return fallback;
	const QString value = qEnvironmentVariable(envName);
	return value.isEmpty() ? fallback : value;
}

// UI 布局/渲染探针与宿主附加探针：各自一个环境变量，默认关闭。
bool Debugger::uiProbeEnabled()
{
	return enabled("ULINHUB_UI_PROBE");
}

bool Debugger::attachProbeEnabled()
{
	return enabled("DSH_ULIN_ATTACH_PROBE");
}

// 统一输出 "[tag] detail"，Logger 按 tag 前缀把日志归入模块文件。
void Debugger::log(const QString& tag, const QString& detail)
{
	qInfo().noquote() << QStringLiteral("[") + tag + QStringLiteral("]") << detail;
}
