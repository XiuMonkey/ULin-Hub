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
	QString tool;
	QJsonObject arguments;   // 可包含 {"$param":"name"} 引用

	QJsonObject toJson() const;
	static IntentStep fromJson(const QJsonObject& json);
};

struct Intent
{
	QString name;
	QString description;
	QString promptTemplate;   // 非空 = 可以按 Prompt 执行
	QVector<IntentParameter> parameters;
	QVector<IntentStep> steps; // 非空 = 有调用链

	bool hasPrompt() const { return !promptTemplate.trimmed().isEmpty(); }
	bool hasChain() const { return !steps.isEmpty(); }

	// Prompt-only：默认走 prompt；有链：默认走 chain。
	bool defaultPromptMode() const { return hasPrompt() && !hasChain(); }

	QJsonObject toJson() const;
	static Intent fromJson(const QJsonObject& json);
};
} // namespace UlinCore
