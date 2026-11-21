#pragma once

#include "common/agent/ulincore/types/UlinCoreTypes.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <memory>

namespace UlinCore
{
struct ExecutorBackendContext
{
	QString workspaceRoot;
	QString sessionId;
	QString turnId;
	bool allowWrites = false;   // write_file 默认关闭
	bool allowShell = false;    // 预留：run_shell 之类的高危工具
};

class UlinCoreExecutorBackend
{
public:
	virtual ~UlinCoreExecutorBackend() = default;

	virtual QString name() const = 0;
	virtual QString description() const = 0;
	virtual QJsonObject parametersSchema() const = 0;
	virtual ToolResult execute(const QJsonObject& arguments, const ExecutorBackendContext& context) = 0;

	QJsonObject schema() const;   // 包成 OpenAI function schema
};

class UlinCoreExecutorBackendRegistry
{
public:
	void add(std::shared_ptr<UlinCoreExecutorBackend> tool);
	void addBuiltins();
	UlinCoreExecutorBackend* find(const QString& name) const;
	std::shared_ptr<UlinCoreExecutorBackend> findShared(const QString& name) const;
	QList<QString> names() const;
	QJsonArray schemas() const;

private:
	QHash<QString, std::shared_ptr<UlinCoreExecutorBackend>> m_tools;
};
} // namespace UlinCore
