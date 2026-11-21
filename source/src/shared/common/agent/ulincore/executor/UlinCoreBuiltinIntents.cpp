#include "common/agent/ulincore/executor/UlinCoreBuiltinIntents.h"

namespace UlinCore
{
namespace
{
IntentParameter param(const QString& name, const QString& type, const QJsonValue& defaultValue = QJsonValue(QJsonValue::Undefined))
{
	IntentParameter parameter;
	parameter.name = name;
	parameter.type = type;
	parameter.defaultValue = defaultValue;
	return parameter;
}
}

QList<Intent> builtinIntents()
{
	Intent read;
	read.name = QStringLiteral("read_file");
	read.description = QStringLiteral("Read a UTF-8 text file inside the workspace.");
	read.executor = QStringLiteral("builtin.read_file");
	read.parameters = { param("path", "string"), param("maxBytes", "number", 65536) };

	Intent list;
	list.name = QStringLiteral("list_dir");
	list.description = QStringLiteral("List entries in a workspace directory.");
	list.executor = QStringLiteral("builtin.list_dir");
	list.parameters = { param("path", "string", "."), param("maxEntries", "number", 2000) };

	Intent write;
	write.name = QStringLiteral("write_file");
	write.description = QStringLiteral("Write a UTF-8 text file inside the workspace.");
	write.executor = QStringLiteral("builtin.write_file");
	write.parameters = { param("path", "string"), param("content", "string") };

	Intent search;
	search.name = QStringLiteral("search_text");
	search.description = QStringLiteral("Search text files under a workspace directory.");
	search.executor = QStringLiteral("builtin.search_text");
	search.parameters = { param("path", "string", "."), param("query", "string"),
		param("caseSensitive", "boolean", false), param("maxResults", "number", 50) };

	return { read, list, write, search };
}
} // namespace UlinCore
