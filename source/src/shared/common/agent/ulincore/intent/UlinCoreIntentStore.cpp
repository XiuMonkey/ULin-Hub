#include "common/agent/ulincore/intent/UlinCoreIntentStore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QStringList>

#include <utility>

namespace UlinCore
{
UlinCoreIntentStore::UlinCoreIntentStore(QString filePath)
	: m_filePath(std::move(filePath))
{
}

void UlinCoreIntentStore::setFilePath(const QString& filePath)
{
	m_filePath = filePath;
}

bool UlinCoreIntentStore::load(QString* error)
{
	m_intents.clear();
	if (m_filePath.isEmpty())
		return true;

	QFile file(m_filePath);
	if (!file.exists())
		return true;
	if (!file.open(QIODevice::ReadOnly)) {
		if (error)
			*error = QStringLiteral("cannot open intent store: %1").arg(m_filePath);
		return false;
	}

	QJsonParseError parseError;
	const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
	if (parseError.error != QJsonParseError::NoError || !document.isArray()) {
		if (error) {
			*error = QStringLiteral("invalid intent store %1: %2").arg(m_filePath, parseError.errorString());
		}
		return false;
	}

	// 存储是 Intent 数组；同名条目后者覆盖前者。
	for (const QJsonValue& value : document.array()) {
		const Intent intent = Intent::fromJson(value.toObject());
		if (!intent.name.trimmed().isEmpty())
			m_intents.insert(intent.name, intent);
	}
	return true;
}

// 排序后写盘；QSaveFile 原子提交，失败时保留原文件。
bool UlinCoreIntentStore::save(QString* error) const
{
	if (m_filePath.isEmpty())
		return true;

	const QFileInfo info(m_filePath);
	if (!info.absoluteDir().exists() && !QDir().mkpath(info.absolutePath())) {
		if (error)
			*error = QStringLiteral("cannot create intent store dir: %1").arg(info.absolutePath());
		return false;
	}

	QJsonArray array;
	QStringList names = m_intents.keys();
	names.sort();
	for (const QString& name : names)
		array.append(m_intents.value(name).toJson());

	QSaveFile file(m_filePath);
	if (!file.open(QIODevice::WriteOnly)) {
		if (error)
			*error = QStringLiteral("cannot write intent store: %1").arg(m_filePath);
		return false;
	}
	file.write(QJsonDocument(array).toJson(QJsonDocument::Indented));
	if (!file.commit()) {
		if (error)
			*error = QStringLiteral("cannot commit intent store: %1").arg(m_filePath);
		return false;
	}
	return true;
}

void UlinCoreIntentStore::add(const Intent& intent)
{
	if (!intent.name.trimmed().isEmpty())
		m_intents.insert(intent.name, intent);
}

bool UlinCoreIntentStore::contains(const QString& name) const
{
	return m_intents.contains(name);
}

Intent UlinCoreIntentStore::find(const QString& name) const
{
	return m_intents.value(name);
}

QList<Intent> UlinCoreIntentStore::all() const
{
	QList<Intent> result;
	QStringList names = m_intents.keys();
	names.sort();
	for (const QString& name : names)
		result.append(m_intents.value(name));
	return result;
}

// 生成喂给模型的 Intent 目录：名字 - 描述 (参数) [能力标签]。
QString UlinCoreIntentStore::catalogText() const
{
	QStringList lines;
	for (const Intent& intent : all()) {
		QString line = intent.name;
		if (!intent.description.isEmpty())
			line += QStringLiteral(" - ") + intent.description;
		QStringList parameters;
		for (const IntentParameter& parameter : intent.parameters)
			parameters.append(parameter.name + QLatin1Char(':') + parameter.type);
		if (!parameters.isEmpty())
			line += QStringLiteral(" (") + parameters.join(QStringLiteral(", ")) + QLatin1Char(')');
		line += intent.hasPrompt() ? QStringLiteral(" [prompt]") : QString();
		line += intent.hasSkill() ? QStringLiteral(" [skill]") : QString();
		line += intent.hasChain() ? QStringLiteral(" [chain]") : QString();
		line += intent.hasNative() ? QStringLiteral(" [native]") : QString();
		lines.append(line);
	}
	return lines.join(QLatin1Char('\n'));
}
} // namespace UlinCore
