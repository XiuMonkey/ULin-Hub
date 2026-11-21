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

class UlinCoreExecutor
{
public:
	virtual ~UlinCoreExecutor() = default;
	virtual ToolResult execute(const QJsonObject& arguments, const ExecutorContext& context) = 0;
};

class UlinCoreExecutorRegistry
{
public:
	void add(const QString& id, std::shared_ptr<UlinCoreExecutor> executor);
	void addBuiltins();
	std::shared_ptr<UlinCoreExecutor> findShared(const QString& id) const;

private:
	QHash<QString, std::shared_ptr<UlinCoreExecutor>> m_executors;
};
} // namespace UlinCore
