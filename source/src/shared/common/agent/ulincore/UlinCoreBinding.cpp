#include "common/agent/ulincore/UlinCoreBinding.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QStringList>

namespace UlinCore
{
QString jsonTypeName(const QJsonValue& value)
{
	if (value.isBool())
		return QStringLiteral("boolean");
	if (value.isDouble()) {
		const double number = value.toDouble();
		return qFuzzyCompare(number, static_cast<double>(static_cast<qint64>(number)))
			? QStringLiteral("integer")
			: QStringLiteral("number");
	}
	if (value.isObject())
		return QStringLiteral("object");
	if (value.isArray())
		return QStringLiteral("array");
	return QStringLiteral("string");
}

QJsonValue resolveBinding(const QJsonValue& value, const QJsonObject& params)
{
	if (value.isObject()) {
		const QJsonObject object = value.toObject();
		if (object.size() == 1 && object.contains(QStringLiteral("$param"))) {
			const QString name = object.value(QStringLiteral("$param")).toString();
			return params.value(name);
		}
		QJsonObject resolved;
		for (auto it = object.constBegin(); it != object.constEnd(); ++it)
			resolved.insert(it.key(), resolveBinding(it.value(), params));
		return resolved;
	}
	if (value.isArray()) {
		QJsonArray array;
		for (const QJsonValue& item : value.toArray())
			array.append(resolveBinding(item, params));
		return array;
	}
	return value;
}

QJsonObject resolveBindings(const QJsonObject& args, const QJsonObject& params)
{
	QJsonObject resolved;
	for (auto it = args.constBegin(); it != args.constEnd(); ++it)
		resolved.insert(it.key(), resolveBinding(it.value(), params));
	return resolved;
}

QString expandPromptTemplate(const QString& text, const QJsonObject& params)
{
	QString result = text;
	for (auto it = params.constBegin(); it != params.constEnd(); ++it) {
		QString value;
		const QJsonValue json = it.value();
		if (json.isString())
			value = json.toString();
		else if (json.isBool())
			value = json.toBool() ? QStringLiteral("true") : QStringLiteral("false");
		else if (json.isDouble())
			value = QString::number(json.toDouble());
		else if (json.isNull())
			value.clear();
		else
			value = QString::fromUtf8(QJsonDocument(json.toObject()).toJson(QJsonDocument::Compact));

		result.replace(QLatin1Char('[') + it.key() + QLatin1Char(']'), value);
		result.replace(QStringLiteral("{{%1}}").arg(it.key()), value);
	}
	return result;
}

QJsonObject parametersSchema(const QVector<IntentParameter>& parameters)
{
	QJsonObject properties;
	QJsonArray required;
	for (const IntentParameter& parameter : parameters) {
		if (parameter.name.trimmed().isEmpty())
			continue;
		QJsonObject property{ { QStringLiteral("type"), parameter.type } };
		if (!parameter.defaultValue.isUndefined())
			property.insert(QStringLiteral("default"), parameter.defaultValue);
		properties.insert(parameter.name, property);
		if (parameter.defaultValue.isUndefined())
			required.append(parameter.name);
	}
	return QJsonObject{
		{ QStringLiteral("type"), QStringLiteral("object") },
		{ QStringLiteral("properties"), properties },
		{ QStringLiteral("required"), required }
	};
}

QVector<IntentParameter> inferParameters(const QJsonObject& args)
{
	QVector<IntentParameter> parameters;
	for (auto it = args.constBegin(); it != args.constEnd(); ++it) {
		IntentParameter parameter;
		parameter.name = it.key();
		parameter.type = jsonTypeName(it.value());
		parameter.defaultValue = it.value();
		parameters.append(parameter);
	}
	return parameters;
}
} // namespace UlinCore
