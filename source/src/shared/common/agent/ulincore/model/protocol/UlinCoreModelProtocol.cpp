#include "common/agent/ulincore/model/protocol/UlinCoreModelProtocol.h"

namespace UlinCore::Protocol
{
QJsonObject metadataSchema()
{
	return QJsonObject{
		{ QStringLiteral("type"), QStringLiteral("object") },
		{ QStringLiteral("description"),
			QStringLiteral("Optional ULin protocol metadata. Use only when the task needs tags or an inline Intent definition.") },
		{ QStringLiteral("properties"), QJsonObject{
			{ QStringLiteral("tags"), QJsonObject{
				{ QStringLiteral("type"), QStringLiteral("array") },
				{ QStringLiteral("items"), QJsonObject{{ QStringLiteral("type"), QStringLiteral("string") }} }
			} },
			{ QStringLiteral("Intent"), QJsonObject{
				{ QStringLiteral("type"), QStringLiteral("object") },
				{ QStringLiteral("description"), QStringLiteral("Complete Intent definition.") }
			} }
		} },
		{ QStringLiteral("additionalProperties"), true }
	};
}

QJsonArray encodeToolSchemas(const QJsonArray& tools)
{
	QJsonArray out;
	for (const QJsonValue& value : tools) {
		QJsonObject tool = value.toObject();
		QJsonObject function = tool.value(QStringLiteral("function")).toObject();
		QJsonObject parameters = function.value(QStringLiteral("parameters")).toObject();
		QJsonObject properties = parameters.value(QStringLiteral("properties")).toObject();
		if (!properties.contains(QString::fromLatin1(kMetadataKey))) {
			properties.insert(QString::fromLatin1(kMetadataKey), metadataSchema());
			parameters.insert(QStringLiteral("properties"), properties);
			function.insert(QStringLiteral("parameters"), parameters);
			tool.insert(QStringLiteral("function"), function);
		}
		out.append(tool);
	}
	return out;
}

QJsonObject encodeToolArguments(const QJsonObject& arguments, const QJsonObject& metadata)
{
	if (metadata.isEmpty())
		return arguments;
	QJsonObject out = arguments;
	out.insert(QString::fromLatin1(kMetadataKey), metadata);
	return out;
}

QJsonObject decodeToolArguments(const QJsonObject& arguments, QJsonObject* metadata)
{
	QJsonObject out = arguments;
	const QString key = QString::fromLatin1(kMetadataKey);
	const QJsonValue value = out.value(key);
	if (metadata)
		*metadata = value.isObject() ? value.toObject() : QJsonObject();
	out.remove(key);
	return out;
}
} // namespace UlinCore::Protocol
