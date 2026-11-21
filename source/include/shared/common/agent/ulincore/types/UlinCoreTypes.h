#pragma once

// 自研 Agent 内核的基础数据类型。
//
// 这里刻意不依赖 DSH 的 wire 格式：dsh/ 适配层负责把 DSH 的 session/event 翻译成
// Message / IntentCall / IntentResult，内核只认这些类型。这样换模型、换传输、做单测时，
// 内核本身不用改。

#include <QJsonObject>
#include <QList>
#include <QMetaType>
#include <QString>

namespace UlinCore
{
enum class Role
{
	System,
	User,
	Assistant,
	IntentResponse
};

QString roleName(Role role);
Role roleFromName(const QString& name);

struct IntentCall
{
	QString id;
	QString name;
	QJsonObject arguments;
	QJsonObject metadata;   // tags / Intent；由 ULin 自有工具协议携带

	QJsonObject toJson() const;
	static IntentCall fromJson(const QJsonObject& json);
};

struct Message
{
	Role role = Role::User;
	QString text;
	QString thinking;
	QList<IntentCall> intentCalls;   // Assistant 消息里的工具调用
	QString intentCallId;          // Tool 消息对应哪次调用
	QString intentName;
	bool isError = false;
	qint64 createdAt = 0;

	QJsonObject toJson() const;
	static Message fromJson(const QJsonObject& json);

	static Message user(const QString& text);
	static Message assistant(const QString& text, const QString& thinking = QString());
	static Message intentResult(const QString& callId, const QString& name, const QString& text, bool isError);
};

struct Usage
{
	int promptTokens = 0;
	int completionTokens = 0;

	int totalTokens() const { return promptTokens + completionTokens; }
	void add(const Usage& other);
	QJsonObject toJson() const;
	static Usage fromJson(const QJsonObject& json);
};

struct ExecuteResult
{
	bool ok = false;
	QString content;
	QJsonObject data;
	QString error;

	static ExecuteResult success(const QString& content, const QJsonObject& data = {});
	static ExecuteResult failure(const QString& error);
};

struct IntentResult
{
	bool ok = false;
	QString content;
	QJsonObject data;
	QString error;

	static IntentResult success(const QString& content, const QJsonObject& data = {});
	static IntentResult failure(const QString& error);
};

struct TurnResult
{
	bool ok = false;
	bool cancelled = false;
	QString error;
	QString finalText;
	int steps = 0;
	Usage usage;
};
} // namespace UlinCore

Q_DECLARE_METATYPE(UlinCore::IntentCall)
Q_DECLARE_METATYPE(UlinCore::Message)
Q_DECLARE_METATYPE(UlinCore::Usage)
Q_DECLARE_METATYPE(UlinCore::IntentResult)
Q_DECLARE_METATYPE(UlinCore::TurnResult)
