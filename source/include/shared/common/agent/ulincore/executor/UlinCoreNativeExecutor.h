#pragma once

#include "common/agent/ulincore/types/UlinCoreTypes.h"

#include <QHash>
#include <QJsonObject>
#include <QString>
#include <memory>

namespace UlinCore
{
struct ExecutorContext
{
	QString workspaceRoot;
	QString sessionId;
	QString turnId;
	bool allowWrites = false;
	bool allowShell = false;
};

class UlinCoreNativeExecutor
{
public:
	virtual ~UlinCoreNativeExecutor() = default;
	virtual ExecuteResult execute(const QJsonObject& arguments, const ExecutorContext& context) = 0;
};

class UlinCoreNativeRegistry
{
public:
	void add(const QString& id, std::shared_ptr<UlinCoreNativeExecutor> executor);
	void remove(const QString& id);
	bool contains(const QString& id) const;
	void addBuiltins();
	std::shared_ptr<UlinCoreNativeExecutor> findShared(const QString& id) const;

private:
	QHash<QString, std::shared_ptr<UlinCoreNativeExecutor>> m_executors;
};
} // namespace UlinCore
