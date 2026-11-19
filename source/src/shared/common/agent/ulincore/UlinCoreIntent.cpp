#include "common/agent/ulincore/UlinCoreIntent.h"

#include <QJsonArray>

namespace UlinCore
{
QJsonObject IntentParameter::toJson() const
{
	QJsonObject object{
		{ QStringLiteral("name"), name },
		{ QStringLiteral("type"), type }
	};
	if (!defaultValue.isUndefined())
		object.insert(QStringLiteral("default"), defaultValue);
	return object;
}

IntentParameter IntentParameter::fromJson(const QJsonObject& json)
{
	IntentParameter parameter;
	parameter.name = json.value(QStringLiteral("name")).toString();
	parameter.type = json.value(QStringLiteral("type")).toString(QStringLiteral("string"));
	parameter.defaultValue = json.value(QStringLiteral("default"));
	return parameter;
}

QJsonObject IntentStep::toJson() const
{
	return QJsonObject{
		{ QStringLiteral("tool"), tool },
		{ QStringLiteral("arguments"), arguments }
	};
}

IntentStep IntentStep::fromJson(const QJsonObject& json)
{
	IntentStep step;
	step.tool = json.value(QStringLiteral("tool")).toString();
	step.arguments = json.value(QStringLiteral("arguments")).toObject();
	return step;
}

QJsonObject Intent::toJson() const
{
	QJsonArray parameterArray;
	for (const IntentParameter& parameter : parameters)
		parameterArray.append(parameter.toJson());

	QJsonArray stepArray;
	for (const IntentStep& step : steps)
		stepArray.append(step.toJson());

	return QJsonObject{
		{ QStringLiteral("name"), name },
		{ QStringLiteral("description"), description },
		{ QStringLiteral("prompt"), promptTemplate },
		{ QStringLiteral("parameters"), parameterArray },
		{ QStringLiteral("steps"), stepArray }
	};
}

Intent Intent::fromJson(const QJsonObject& json)
{
	Intent intent;
	intent.name = json.value(QStringLiteral("name")).toString();
	intent.description = json.value(QStringLiteral("description")).toString();
	intent.promptTemplate = json.value(QStringLiteral("prompt")).toString();
	for (const QJsonValue& value : json.value(QStringLiteral("parameters")).toArray())
		intent.parameters.append(IntentParameter::fromJson(value.toObject()));
	for (const QJsonValue& value : json.value(QStringLiteral("steps")).toArray())
		intent.steps.append(IntentStep::fromJson(value.toObject()));
	return intent;
}
} // namespace UlinCore
