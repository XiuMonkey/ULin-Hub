#include "common/agent/ulincore/UlinCoreJournal.h"

#include <QDateTime>
#include <QFile>
#include <QJsonDocument>
#include <QMutexLocker>

#include <utility>

namespace UlinCore
{
UlinCoreJournal::UlinCoreJournal(QString filePath)
	: m_filePath(std::move(filePath))
{
}

void UlinCoreJournal::setFilePath(const QString& filePath)
{
	QMutexLocker locker(&m_mutex);
	m_filePath = filePath;
}

QString UlinCoreJournal::filePath() const
{
	QMutexLocker locker(&m_mutex);
	return m_filePath;
}

void UlinCoreJournal::append(const QString& type, const QJsonObject& payload)
{
	QMutexLocker locker(&m_mutex);
	if (m_filePath.isEmpty())
		return;

	QFile file(m_filePath);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
		return;

	QJsonObject entry = payload;
	entry.insert(QStringLiteral("type"), type);
	entry.insert(QStringLiteral("ts"), QDateTime::currentMSecsSinceEpoch());
	file.write(QJsonDocument(entry).toJson(QJsonDocument::Compact));
	file.write("\n", 1);
}

QList<QJsonObject> UlinCoreJournal::readAll() const
{
	QMutexLocker locker(&m_mutex);
	QList<QJsonObject> entries;

	QFile file(m_filePath);
	if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
		return entries;

	while (!file.atEnd()) {
		const QByteArray line = file.readLine().trimmed();
		if (line.isEmpty())
			continue;
		const QJsonDocument doc = QJsonDocument::fromJson(line);
		if (doc.isObject())
			entries.append(doc.object());
	}
	return entries;
}

void UlinCoreJournal::clear()
{
	QMutexLocker locker(&m_mutex);
	if (!m_filePath.isEmpty())
		QFile::remove(m_filePath);
}
} // namespace UlinCore
