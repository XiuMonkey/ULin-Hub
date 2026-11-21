#pragma once

// 内置插件台账：<profile>/.ulin-hub-builtin.json。
// revision = 内置插件源目录（qrc 里的插件源码）逐文件 SHA1 前缀；内容变了下次启动就重装。
// 原先 windows-64 / linux-64 两份 ServerManager.cpp 各有一份逐字相同的实现，提取到这里。

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

namespace BuiltinMarker
{
	inline QString path(const QString& profileDir)
	{
		return profileDir + QStringLiteral("/.ulin-hub-builtin.json");
	}

	// 目录内容摘要：文件名 + 内容，按文件名排序，取 SHA1 前 12 位。
	inline QString sourceRevision(const QString& sourceDir)
	{
		QCryptographicHash hash(QCryptographicHash::Sha1);
		const QDir dir(sourceDir);
		QStringList names = dir.entryList(QDir::Files, QDir::Name);
		names.sort();

		for (const QString& name : names) {
			QFile file(sourceDir + QLatin1Char('/') + name);
			if (!file.open(QIODevice::ReadOnly))
				return QString();

			hash.addData(name.toUtf8());
			hash.addData(file.readAll());
			file.close();
		}

		return QString::fromLatin1(hash.result().toHex().left(12));
	}

	inline QString recordedRevision(const QString& profileDir, const QString& pluginName)
	{
		QFile file(path(profileDir));
		if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
			return QString();

		const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
		file.close();
		if (!doc.isObject())
			return QString();

		return doc.object().value(QStringLiteral("plugins")).toObject()
			.value(pluginName).toObject().value(QStringLiteral("revision")).toString();
	}

	inline void recordRevision(const QString& profileDir, const QString& pluginName, const QString& revision)
	{
		QJsonObject plugins;
		QFile existing(path(profileDir));
		if (existing.open(QIODevice::ReadOnly | QIODevice::Text)) {
			const QJsonDocument doc = QJsonDocument::fromJson(existing.readAll());
			existing.close();
			if (doc.isObject())
				plugins = doc.object().value(QStringLiteral("plugins")).toObject();
		}

		QJsonObject entry = plugins.value(pluginName).toObject();
		entry.insert(QStringLiteral("revision"), revision);
		entry.insert(QStringLiteral("installedAt"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
		plugins.insert(pluginName, entry);

		QJsonObject root;
		root.insert(QStringLiteral("comment"),
			QStringLiteral("ULin Hub builtin plugins: revision of the embedded source that is currently installed."));
		root.insert(QStringLiteral("plugins"), plugins);

		QFile out(path(profileDir));
		if (out.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
			out.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
			out.close();
		}
	}
}
