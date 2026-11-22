#pragma once

#include "common/agent/ulincore/intent/UlinCoreIntent.h"

#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace UlinCore
{
enum class InvocationKind
{
	Prompt,
	Intent,
	Empty,
	Error
};

// ULinCore 二次解析后的输入。原始文本始终保留。
struct UlinCoreInvocation
{
	InvocationKind kind = InvocationKind::Prompt;
	QString rawText;
	QString intentName;
	QString promptText;       // Prompt fallback 时真正发给模型的文本
	QJsonObject args;

	QStringList tags;         // 单数组标签
	bool hasDefinition = false;
	Intent definition;        // Intent:{...}
	bool execute = true;      // defineOnly -> false

	QString promptOverride;   // 元数据里的 prompt，可覆盖本次 Intent.prompt
	bool hasPromptOverride = false;
	QString skillOverride;    // 元数据里的 skill，可覆盖本次 Intent.skill
	bool hasSkillOverride = false;

	bool hasTag(const QString& tag) const { return tags.contains(tag); }
	bool asPrompt() const { return hasTag(QStringLiteral("asPrompt")); }
	bool asSkill() const { return hasTag(QStringLiteral("asSkill")); }
	bool save() const { return hasTag(QStringLiteral("save")); }
	bool agentEngaged() const { return hasTag(QStringLiteral("agentEngaged")); }
	bool skillRequired() const { return hasTag(QStringLiteral("skillRequired")); }
	bool withSkill() const { return hasTag(QStringLiteral("withSkill")); }
	bool defineOnly() const { return hasTag(QStringLiteral("defineOnly")); }

	QString error;
};
} // namespace UlinCore
