#include "common/util/Debugger.h"

#include <QDebug>
#include <QtGlobal>

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

bool Debugger::uiProbeEnabled()
{
	return enabled("ULINHUB_UI_PROBE");
}

bool Debugger::attachProbeEnabled()
{
	return enabled("DSH_ULIN_ATTACH_PROBE");
}

void Debugger::log(const QString& tag, const QString& detail)
{
	qInfo().noquote() << QStringLiteral("[") + tag + QStringLiteral("]") << detail;
}
