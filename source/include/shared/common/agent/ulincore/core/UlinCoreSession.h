#pragma once

#include "common/agent/ulincore/types/UlinCoreTypes.h"

#include <QJsonObject>
#include <QObject>

namespace UlinCore
{
// 一次会话的消息账本。内核只往里追加，不直接改 UI 状态。
class UlinCoreSession : public QObject
{
	Q_OBJECT

public:
	explicit UlinCoreSession(QString id = QString(), QObject* parent = nullptr);

	QString id() const { return m_id; }
	QString systemPrompt() const { return m_systemPrompt; }
	void setSystemPrompt(const QString& prompt);

	const QList<Message>& messages() const { return m_messages; }
	void append(const Message& message);
	void appendUserText(const QString& text);
	void clear();

	Usage usage() const { return m_usage; }
	void addUsage(const Usage& usage);

	QString lastUserText() const;

	QJsonObject snapshot() const;
	bool restore(const QJsonObject& snapshot);

signals:
	void messageAppended(const UlinCore::Message& message);
	void changed();

private:
	QString m_id;
	QString m_systemPrompt;
	QList<Message> m_messages;
	Usage m_usage;
};
} // namespace UlinCore
