#include "common/agent/ulincore/core/UlinCoreSession.h"

#include <QDateTime>
#include <QJsonArray>
#include <QUuid>
#include <utility>

namespace UlinCore
{
UlinCoreSession::UlinCoreSession(QString id, QObject* parent)
	: QObject(parent)
	, m_id(id.isEmpty() ? QUuid::createUuid().toString(QUuid::WithoutBraces) : std::move(id))
{
}

void UlinCoreSession::setSystemPrompt(const QString& prompt)
{
	if (m_systemPrompt == prompt)
		return;
	m_systemPrompt = prompt;
	emit changed();
}

void UlinCoreSession::append(const Message& message)
{
	// createdAt 为 0 时补当前时间，随后广播追加事件。
	Message stored = message;
	if (stored.createdAt == 0)
		stored.createdAt = QDateTime::currentMSecsSinceEpoch();
	m_messages.append(stored);
	emit messageAppended(stored);
	emit changed();
}

void UlinCoreSession::appendUserText(const QString& text)
{
	append(Message::user(text));
}

void UlinCoreSession::clear()
{
	m_messages.clear();
	m_usage = Usage();
	emit changed();
}

void UlinCoreSession::addUsage(const Usage& usage)
{
	m_usage.add(usage);
	emit changed();
}

QString UlinCoreSession::lastUserText() const
{
	for (int i = m_messages.size() - 1; i >= 0; --i) {
		if (m_messages.at(i).role == Role::User)
			return m_messages.at(i).text;
	}
	return QString();
}

QJsonObject UlinCoreSession::snapshot() const
{
	QJsonArray messages;
	for (const Message& message : m_messages)
		messages.append(message.toJson());

	return QJsonObject{
		{ QStringLiteral("id"), m_id },
		{ QStringLiteral("systemPrompt"), m_systemPrompt },
		{ QStringLiteral("messages"), messages },
		{ QStringLiteral("usage"), m_usage.toJson() }
	};
}

bool UlinCoreSession::restore(const QJsonObject& snapshot)
{
	// 快照与恢复共用同一份 JSON 结构；id 缺失时沿用当前会话 id。
	const QString id = snapshot.value(QStringLiteral("id")).toString();
	if (!id.isEmpty())
		m_id = id;
	m_systemPrompt = snapshot.value(QStringLiteral("systemPrompt")).toString();
	m_messages.clear();
	const QJsonArray messages = snapshot.value(QStringLiteral("messages")).toArray();
	for (const QJsonValue& value : messages)
		m_messages.append(Message::fromJson(value.toObject()));
	m_usage = Usage::fromJson(snapshot.value(QStringLiteral("usage")).toObject());
	emit changed();
	return true;
}
} // namespace UlinCore
