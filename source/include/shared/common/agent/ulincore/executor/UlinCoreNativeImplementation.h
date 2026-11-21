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
struct NativeContext
{
	QString workspaceRoot;
	QString sessionId;
	QString turnId;
	bool allowWrites = false;   // write_file 默认关闭
	bool allowShell = false;    // 预留：run_shell 之类的高危工具
};

class UlinCoreNativeImplementation
{
public:
	virtual ~UlinCoreNativeImplementation() = default;

	virtual QString name() const = 0;
	virtual QString description() const = 0;
	virtual QJsonObject parametersSchema() const = 0;
	virtual IntentResult execute(const QJsonObject& arguments, const NativeContext& context) = 0;

	QJsonObject schema() const;   // 包成 OpenAI function schema
};

class UlinCoreNativeImplementationRegistry
{
public:
	void add(std::shared_ptr<UlinCoreNativeImplementation> tool);
	void addBuiltins();
	UlinCoreNativeImplementation* find(const QString& name) const;
	std::shared_ptr<UlinCoreNativeImplementation> findShared(const QString& name) const;
	QList<QString> names() const;
	QJsonArray schemas() const;

private:
	QHash<QString, std::shared_ptr<UlinCoreNativeImplementation>> m_tools;
};
} // namespace UlinCore
