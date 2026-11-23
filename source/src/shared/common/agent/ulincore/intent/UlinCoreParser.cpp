#include "common/agent/ulincore/intent/UlinCoreParser.h"

#include <QChar>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QStringList>

namespace UlinCore
{
namespace
{
bool isSpace(QChar c)
{
	return c.isSpace();
}

int findMatching(const QString& text, int open)
{
	if (open < 0 || open >= text.size() || text.at(open) != QLatin1Char('{'))
		return -1;
	int depth = 0;
	bool inString = false;
	bool escape = false;
	for (int i = open; i < text.size(); ++i) {
		const QChar c = text.at(i);
		if (inString) {
			if (escape)
				escape = false;
			else if (c == QLatin1Char('\\'))
				escape = true;
			else if (c == QLatin1Char('"'))
				inString = false;
			continue;
		}
		if (c == QLatin1Char('"')) {
			inString = true;
			continue;
		}
		if (c == QLatin1Char('{'))
			++depth;
		else if (c == QLatin1Char('}')) {
			--depth;
			if (depth == 0)
				return i;
		}
	}
	return -1;
}

// 元数据值里允许出现 JSON object/array（Intent、tags）；按括号配对切出原文。
int findMatchingValue(const QString& text, int open)
{
	if (open < 0 || open >= text.size())
		return -1;
	const QChar opener = text.at(open);
	if (opener != QLatin1Char('{') && opener != QLatin1Char('['))
		return -1;
	const QChar closer = (opener == QLatin1Char('{')) ? QLatin1Char('}') : QLatin1Char(']');
	int depth = 0;
	bool inString = false;
	bool escape = false;
	for (int i = open; i < text.size(); ++i) {
		const QChar c = text.at(i);
		if (inString) {
			if (escape)
				escape = false;
			else if (c == QLatin1Char('\\'))
				escape = true;
			else if (c == QLatin1Char('"'))
				inString = false;
			continue;
		}
		if (c == QLatin1Char('"')) {
			inString = true;
			continue;
		}
		if (c == opener)
			++depth;
		else if (c == closer) {
			--depth;
			if (depth == 0)
				return i;
		}
	}
	return -1;
}

QString unquote(const QString& token)
{
	const QString trimmed = token.trimmed();
	if (trimmed.size() < 2 || !trimmed.startsWith(QLatin1Char('"')) || !trimmed.endsWith(QLatin1Char('"')))
		return trimmed;
	// 手工去引号并处理常见转义；协议里的字符串不需要完整 JSON 解码器。
	QString result = trimmed.mid(1, trimmed.size() - 2);
	result.replace(QStringLiteral("\\\""), QStringLiteral("\""));
	result.replace(QStringLiteral("\\\\"), QStringLiteral("\\"));
	result.replace(QStringLiteral("\\n"), QStringLiteral("\n"));
	result.replace(QStringLiteral("\\t"), QStringLiteral("\t"));
	return result;
}

QJsonValue parseScalar(const QString& token)
{
	const QString trimmed = token.trimmed();
	if (trimmed.startsWith(QLatin1Char('"')) && trimmed.endsWith(QLatin1Char('"')))
		return QJsonValue(unquote(trimmed));
	if (trimmed.compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0)
		return QJsonValue(true);
	if (trimmed.compare(QStringLiteral("false"), Qt::CaseInsensitive) == 0)
		return QJsonValue(false);
	if (trimmed.compare(QStringLiteral("null"), Qt::CaseInsensitive) == 0)
		return QJsonValue(QJsonValue::Null);

	bool ok = false;
	const double number = trimmed.toDouble(&ok);
	if (ok && !trimmed.isEmpty())
		return QJsonValue(number);
	return QJsonValue(trimmed);
}

bool parseKeyValueBody(const QString& body, bool colon, QJsonObject* out, QString* error)
{
	int i = 0;
	const int n = body.size();
	while (i < n) {
		while (i < n && (isSpace(body.at(i)) || body.at(i) == QLatin1Char(',')))
			++i;
		if (i >= n)
			break;

		QString key;
		if (body.at(i) == QLatin1Char('"')) {
			int end = i + 1;
			bool escape = false;
			for (; end < n; ++end) {
				if (escape)
					escape = false;
				else if (body.at(end) == QLatin1Char('\\'))
					escape = true;
				else if (body.at(end) == QLatin1Char('"'))
					break;
			}
			if (end >= n) {
				if (error)
					*error = QStringLiteral("unterminated key string");
				return false;
			}
			key = unquote(body.mid(i, end - i + 1));
			i = end + 1;
		} else {
			const int start = i;
			const QChar delimiter = colon ? QLatin1Char(':') : QLatin1Char('=');
			while (i < n && body.at(i) != delimiter && !isSpace(body.at(i)))
				++i;
			key = body.mid(start, i - start).trimmed();
		}

		while (i < n && isSpace(body.at(i)))
			++i;
		const QChar delimiter = colon ? QLatin1Char(':') : QLatin1Char('=');
		if (i >= n || body.at(i) != delimiter) {
			if (error)
				*error = QStringLiteral("expected '%1' after key %2")
					.arg(delimiter).arg(key);
			return false;
		}
		++i;
		while (i < n && isSpace(body.at(i)))
			++i;

		// tags:["asPrompt"] / Intent:{...}：值本身是结构化 JSON。
		if (i < n && (body.at(i) == QLatin1Char('{') || body.at(i) == QLatin1Char('['))) {
			const int close = findMatchingValue(body, i);
			if (close < 0) {
				if (error)
					*error = QStringLiteral("unterminated structured value for %1").arg(key);
				return false;
			}
			QJsonParseError parseError;
			const QJsonDocument doc = QJsonDocument::fromJson(
				body.mid(i, close - i + 1).toUtf8(), &parseError);
			if (parseError.error != QJsonParseError::NoError) {
				if (error)
					*error = QStringLiteral("invalid JSON for %1: %2")
						.arg(key, parseError.errorString());
				return false;
			}
			out->insert(key, doc.isArray() ? QJsonValue(doc.array()) : QJsonValue(doc.object()));
			i = close + 1;
			while (i < n && isSpace(body.at(i)))
				++i;
			if (i < n && body.at(i) == QLatin1Char(','))
				++i;
			continue;
		}

		QString value;
		if (i < n && body.at(i) == QLatin1Char('"')) {
			const int start = i;
			++i;
			bool escape = false;
			while (i < n) {
				if (escape)
					escape = false;
				else if (body.at(i) == QLatin1Char('\\'))
					escape = true;
				else if (body.at(i) == QLatin1Char('"')) {
					++i;
					break;
				}
				++i;
			}
			value = body.mid(start, i - start);
		} else {
			const int start = i;
			while (i < n && body.at(i) != QLatin1Char(','))
				++i;
			value = body.mid(start, i - start).trimmed();
		}

		if (key.isEmpty()) {
			if (error)
				*error = QStringLiteral("empty key");
			return false;
		}
		out->insert(key, parseScalar(value));

		while (i < n && isSpace(body.at(i)))
			++i;
		if (i < n && body.at(i) == QLatin1Char(','))
			++i;
	}
	return true;
}

QJsonValue valueIgnoreCase(const QJsonObject& object, const QString& key)
{
	for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
		if (it.key().compare(key, Qt::CaseInsensitive) == 0)
			return it.value();
	}
	return QJsonValue(QJsonValue::Undefined);
}

bool boolValue(const QJsonObject& object, const QString& key, bool* has, bool* value)
{
	const QJsonValue json = valueIgnoreCase(object, key);
	if (json.isUndefined())
		return false;
	if (has)
		*has = true;
	if (json.isBool())
		*value = json.toBool();
	else
		*value = json.toString().compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0;
	return true;
}
} // namespace

UlinCoreInvocation UlinCoreParser::parse(const QString& rawText)
{
	UlinCoreInvocation invocation;
	invocation.rawText = rawText;

	const QString text = rawText.trimmed();
	if (!text.startsWith(QStringLiteral("/it"))) {
		invocation.kind = InvocationKind::Prompt;
		invocation.promptText = rawText;
		return invocation;
	}

	const QChar next = text.size() > 3 ? text.at(3) : QChar();
	if (text.size() > 3 && !next.isSpace() && next != QLatin1Char('{') && next != QLatin1Char('-')) {
		invocation.kind = InvocationKind::Prompt;
		invocation.promptText = rawText;
		return invocation;
	}

	QString rest = text.mid(3).trimmed();
	if (rest.isEmpty()) {
		invocation.kind = InvocationKind::Prompt;
		invocation.promptText = rest;
		return invocation;
	}

	int pos = 0;
	while (pos < rest.size() && isSpace(rest.at(pos)))
		++pos;
	const int nameStart = pos;
	while (pos < rest.size()) {
		const QChar c = rest.at(pos);
		if (isSpace(c) || c == QLatin1Char('{'))
			break;
		if (c == QLatin1Char('-') && pos + 1 < rest.size() && rest.at(pos + 1) == QLatin1Char('>'))
			break;
		++pos;
	}
	invocation.intentName = rest.mid(nameStart, pos - nameStart).trimmed();
	if (invocation.intentName.isEmpty()) {
		invocation.kind = InvocationKind::Prompt;
		invocation.promptText = rest;
		return invocation;
	}

	// 原始剩余文本，作为未知 intent 的 prompt fallback。
	const QString originalRest = rest;
	while (pos < rest.size() && isSpace(rest.at(pos)))
		++pos;

	if (pos < rest.size() && rest.at(pos) == QLatin1Char('{')) {
		const int close = findMatching(rest, pos);
		if (close < 0) {
			invocation.kind = InvocationKind::Error;
			invocation.error = QStringLiteral("unterminated argument block");
			return invocation;
		}
		const QString body = rest.mid(pos + 1, close - pos - 1);
		QString error;
		if (!parseKeyValueBody(body, true, &invocation.args, &error)) {
			invocation.kind = InvocationKind::Error;
			invocation.error = error;
			return invocation;
		}
		pos = close + 1;
	}

	while (pos < rest.size() && isSpace(rest.at(pos)))
		++pos;

	if (pos + 1 < rest.size() && rest.at(pos) == QLatin1Char('-') && rest.at(pos + 1) == QLatin1Char('>')) {
		pos += 2;
		while (pos < rest.size() && isSpace(rest.at(pos)))
			++pos;
		if (pos >= rest.size() || rest.at(pos) != QLatin1Char('{')) {
			invocation.kind = InvocationKind::Error;
			invocation.error = QStringLiteral("expected '{' after '->'");
			return invocation;
		}
		const int close = findMatching(rest, pos);
		if (close < 0) {
			invocation.kind = InvocationKind::Error;
			invocation.error = QStringLiteral("unterminated metadata block");
			return invocation;
		}
		QJsonObject metadata;
		QString error;
		if (!parseKeyValueBody(rest.mid(pos + 1, close - pos - 1), true, &metadata, &error)) {
			invocation.kind = InvocationKind::Error;
			invocation.error = error;
			return invocation;
		}
		pos = close + 1;

		const QJsonValue prompt = metadata.value(QStringLiteral("prompt"));
		if (prompt.isString()) {
			invocation.promptOverride = prompt.toString();
			invocation.hasPromptOverride = true;
		}
		const QJsonValue skill = metadata.value(QStringLiteral("skill"));
		if (skill.isString()) {
			invocation.skillOverride = skill.toString();
			invocation.hasSkillOverride = true;
		}

		const QJsonValue tagsValue = valueIgnoreCase(metadata, QStringLiteral("tags"));
		if (tagsValue.isArray()) {
			for (const QJsonValue& tag : tagsValue.toArray()) {
				if (tag.isString() && !tag.toString().trimmed().isEmpty())
					invocation.tags.append(tag.toString().trimmed());
			}
		}

		const QJsonValue definition = valueIgnoreCase(metadata, QStringLiteral("Intent"));
		if (definition.isObject()) {
			invocation.definition = Intent::fromJson(definition.toObject());
			invocation.hasDefinition = true;
		}

		const QStringList& tags = invocation.tags;
		if (tags.contains(QStringLiteral("defineOnly")))
			invocation.execute = false;

		// 标签先做最小合法性校验；具体 Intent 是否存在/可执行由 Kernel 校验。
		static const QStringList knownTags{
			QStringLiteral("asPrompt"), QStringLiteral("asSkill"),
			QStringLiteral("withSkill"), QStringLiteral("generateOnly"),
			QStringLiteral("save"), QStringLiteral("agentEngaged"),
			QStringLiteral("skillRequired"), QStringLiteral("defineOnly")
		};
		QStringList seenTags;
		for (const QString& tag : tags) {
			if (!knownTags.contains(tag)) {
				invocation.kind = InvocationKind::Error;
				invocation.error = QStringLiteral("unknown tag: %1").arg(tag);
				return invocation;
			}
			if (seenTags.contains(tag)) {
				invocation.kind = InvocationKind::Error;
				invocation.error = QStringLiteral("duplicate tag: %1").arg(tag);
				return invocation;
			}
			seenTags.append(tag);
		}
		const bool tagAsPrompt = tags.contains(QStringLiteral("asPrompt"));
		const bool tagAsSkill = tags.contains(QStringLiteral("asSkill"));
		const bool tagWithSkill = tags.contains(QStringLiteral("withSkill"));
		const bool tagGenerateOnly = tags.contains(QStringLiteral("generateOnly"));
		const bool tagSave = tags.contains(QStringLiteral("save"));
		const bool tagAgent = tags.contains(QStringLiteral("agentEngaged"));
		const bool tagSkill = tags.contains(QStringLiteral("skillRequired"));
		const bool tagDefineOnly = tags.contains(QStringLiteral("defineOnly"));
		if (tagAsPrompt && tagAsSkill) {
			invocation.kind = InvocationKind::Error;
			invocation.error = QStringLiteral("asPrompt and asSkill cannot both be set");
			return invocation;
		}
		if (tagAsSkill && tagWithSkill) {
			invocation.kind = InvocationKind::Error;
			invocation.error = QStringLiteral("asSkill and withSkill cannot both be set");
			return invocation;
		}
		// asPrompt + withSkill 合法：内置 prompt + 内置/外部 skill。
		if (tagAsPrompt && invocation.hasPromptOverride) {
			invocation.kind = InvocationKind::Error;
			invocation.error = QStringLiteral("asPrompt rejects external prompt");
			return invocation;
		}
		if (tagAsPrompt && invocation.hasSkillOverride && !tagWithSkill) {
			invocation.kind = InvocationKind::Error;
			invocation.error = QStringLiteral("asPrompt rejects external skill unless withSkill is set");
			return invocation;
		}
		if (tagAsSkill && !invocation.hasPromptOverride) {
			invocation.kind = InvocationKind::Error;
			invocation.error = QStringLiteral("asSkill requires an external prompt");
			return invocation;
		}
		if (tagWithSkill && invocation.hasPromptOverride) {
			invocation.kind = InvocationKind::Error;
			invocation.error = QStringLiteral("withSkill rejects external prompt");
			return invocation;
		}
		if (tagSave && (tagAsPrompt || tagAsSkill || tagWithSkill)) {
			invocation.kind = InvocationKind::Error;
			invocation.error = QStringLiteral("save cannot be combined with execution tags");
			return invocation;
		}
		if (tagSkill && !tagAgent) {
			invocation.kind = InvocationKind::Error;
			invocation.error = QStringLiteral("skillRequired requires agentEngaged");
			return invocation;
		}
		if (tagSkill && !invocation.hasPromptOverride) {
			invocation.kind = InvocationKind::Error;
			invocation.error = QStringLiteral("skillRequired requires prompt");
			return invocation;
		}
		if (tagSkill && invocation.hasSkillOverride) {
			invocation.kind = InvocationKind::Error;
			invocation.error = QStringLiteral("skillRequired forbids external skill");
			return invocation;
		}
		if (tagAgent && !invocation.hasPromptOverride) {
			invocation.kind = InvocationKind::Error;
			invocation.error = QStringLiteral("agentEngaged requires prompt");
			return invocation;
		}
		if (tagGenerateOnly) {
			if (!invocation.hasPromptOverride) {
				invocation.kind = InvocationKind::Error;
				invocation.error = QStringLiteral("generateOnly requires prompt");
				return invocation;
			}
			if (!tagSave) {
				invocation.kind = InvocationKind::Error;
				invocation.error = QStringLiteral("generateOnly requires save");
				return invocation;
			}
			if (invocation.hasDefinition || tagDefineOnly
					|| tagAsPrompt || tagAsSkill || tagWithSkill) {
				invocation.kind = InvocationKind::Error;
				invocation.error = QStringLiteral("generateOnly cannot be combined with definition/execution");
				return invocation;
			}
		}
		if (invocation.hasDefinition
				&& (invocation.hasPromptOverride || invocation.hasSkillOverride)) {
			invocation.kind = InvocationKind::Error;
			invocation.error = QStringLiteral("Intent definition rejects external prompt/skill");
			return invocation;
		}
		if (tagDefineOnly) {
			if (!invocation.hasDefinition) {
				invocation.kind = InvocationKind::Error;
				invocation.error = QStringLiteral("defineOnly requires Intent:{...}");
				return invocation;
			}
			if (!tagSave) {
				invocation.kind = InvocationKind::Error;
				invocation.error = QStringLiteral("defineOnly requires save");
				return invocation;
			}
			if (tagAsPrompt || tagAsSkill || tagWithSkill || tagAgent || tagSkill || tagGenerateOnly) {
				invocation.kind = InvocationKind::Error;
				invocation.error = QStringLiteral("defineOnly cannot be combined with execution/agent/generation tags");
				return invocation;
			}
		}
		if (invocation.hasDefinition
				&& (tagAsPrompt || tagAsSkill || tagWithSkill || tagAgent || tagSkill || tagGenerateOnly)) {
			invocation.kind = InvocationKind::Error;
			invocation.error = QStringLiteral("Intent definition cannot be combined with execution/agent/generation tags");
			return invocation;
		}
	}

	// 参数/元数据之后剩下的部分不再当作命令参数；有内容就作为 fallback 提示词。
	const QString tail = rest.mid(pos).trimmed();
	invocation.promptText = tail.isEmpty() ? originalRest : tail;
	invocation.kind = InvocationKind::Intent;
	if (invocation.hasDefinition)
		invocation.mode = IntentMode::Definition;
	return invocation;
}
} // namespace UlinCore
