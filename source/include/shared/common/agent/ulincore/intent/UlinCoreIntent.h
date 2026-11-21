#pragma once

// ULinCore 的统一 Intent 数据模型。
// Prompt-only Intent、工具/Recipe Intent 都走这一套；命令和工具只是 Intent 的入口视图。

#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QVector>

namespace UlinCore
{
struct IntentParameter
{
	QString name;
	QString type = QStringLiteral("string");
	QJsonValue defaultValue;   // undefined = 必填

	QJsonObject toJson() const;
	static IntentParameter fromJson(const QJsonObject& json);
};

struct IntentStep
{
	QString intent;        // 引用另一个 Intent；旧字段名 tool 仍兼容读取
	QJsonObject arguments; // 可包含 {"$param":"name"} 引用

	QJsonObject toJson() const;
	static IntentStep fromJson(const QJsonObject& json);
};

struct Intent
{
	QString name;
	QString description;
	QString promptTemplate;   // 非空 = 可以按 Prompt 执行
	QString skill;            // 非空 = 方法说明；不能单独执行
	QVector<IntentParameter> parameters;
	QVector<IntentStep> steps; // 非空 = 有调用链
	QString executor;          // 非空 = 有客户端原生执行器

	bool hasPrompt() const { return !promptTemplate.trimmed().isEmpty(); }
	bool hasSkill() const { return !skill.trimmed().isEmpty(); }
	bool hasChain() const { return !steps.isEmpty(); }
	bool hasNative() const { return !executor.trimmed().isEmpty(); }

	// Prompt-only：默认走 prompt；有链或原生执行器：默认走对应执行方式。
	bool defaultPromptMode() const { return hasPrompt() && !hasChain() && !hasNative(); }

	QJsonObject toJson() const;
	static Intent fromJson(const QJsonObject& json);
};
} // namespace UlinCore
