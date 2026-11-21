#pragma once

#include "common/agent/ulincore/intent/UlinCoreIntent.h"

#include <QJsonObject>
#include <QString>

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

	QString promptTemplate;   // 元数据里的 Prompt，可覆盖本次 PromptTemplate
	bool hasPromptTemplate = false;
	bool hasAsPrompt = false;
	bool asPrompt = false;
	bool save = false;
	bool hasSave = false;
	bool agentEngaged = false;
	bool hasAgentEngaged = false;
	bool hasSkill = false;          // hasSkill 元数据是否存在
	bool hasSkillFlag = false;      // hasSkill 的布尔值：强制方案包含 skill
	bool asSkill = false;
	bool hasAsSkill = false;
	bool createPromptIntent = false;

	QString error;
};
} // namespace UlinCore
