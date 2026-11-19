#pragma once

#include "common/agent/ulincore/UlinCoreIntent.h"

#include <QHash>
#include <QString>

namespace UlinCore
{
// 已保存 Intent 的本地存储。MVP 使用一个 JSON 文件，不引入数据库。
class UlinCoreIntentStore
{
public:
	explicit UlinCoreIntentStore(QString filePath = QString());

	void setFilePath(const QString& filePath);
	QString filePath() const { return m_filePath; }

	bool load(QString* error = nullptr);
	bool save(QString* error = nullptr) const;

	void add(const Intent& intent);
	bool contains(const QString& name) const;
	Intent find(const QString& name) const;
	QList<Intent> all() const;
	int size() const { return m_intents.size(); }

	// 给系统提示词用的简短目录。
	QString catalogText() const;

private:
	QString m_filePath;
	QHash<QString, Intent> m_intents;
};
} // namespace UlinCore
