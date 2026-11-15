#pragma once

#include "common/agent/ulincore/UlinCoreTypes.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <memory>

namespace UlinCore
{
struct ToolContext
{
	QString workspaceRoot;
	QString sessionId;
	QString turnId;
	bool allowWrites = false;   // write_file 默认关闭
	bool allowShell = false;    // 预留：run_shell 之类的高危工具
};

class UlinCoreTool
{
public:
	virtual ~UlinCoreTool() = default;

	virtual QString name() const = 0;
	virtual QString description() const = 0;
	virtual QJsonObject parametersSchema() const = 0;
	virtual ToolResult execute(const QJsonObject& arguments, const ToolContext& context) = 0;

	QJsonObject schema() const;   // 包成 OpenAI function schema
};

class UlinCoreToolRegistry
{
public:
	void add(std::shared_ptr<UlinCoreTool> tool);
	void addBuiltins();
	UlinCoreTool* find(const QString& name) const;
	std::shared_ptr<UlinCoreTool> findShared(const QString& name) const;
	QList<QString> names() const;
	QJsonArray schemas() const;

private:
	QHash<QString, std::shared_ptr<UlinCoreTool>> m_tools;
};
} // namespace UlinCore
