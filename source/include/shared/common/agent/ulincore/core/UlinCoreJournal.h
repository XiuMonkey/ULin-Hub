#pragma once

#include <QJsonObject>
#include <QList>
#include <QMutex>
#include <QString>

namespace UlinCore
{
// 极简 JSONL 账本：一行一个事件，方便回放/排查，也给以后做投影留原始日志。
class UlinCoreJournal
{
public:
	explicit UlinCoreJournal(QString filePath = QString());

	void setFilePath(const QString& filePath);
	QString filePath() const;

	void append(const QString& type, const QJsonObject& payload = {});
	QList<QJsonObject> readAll() const;
	void clear();

private:
	QString m_filePath;
	mutable QMutex m_mutex;
};
} // namespace UlinCore
