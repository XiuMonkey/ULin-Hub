#pragma once

#include "common/agent/ulincore/UlinCoreIntent.h"
#include "common/agent/ulincore/UlinCoreTool.h"

namespace UlinCore
{
// 把一个有调用链的 Intent 暴露成 UlinCoreTool。
// 工具本身只做“绑定参数 → 依次调用底层工具”，不参与解析。
class UlinCoreRecipeTool : public UlinCoreTool
{
public:
	UlinCoreRecipeTool(Intent intent, UlinCoreToolRegistry* registry);

	QString name() const override;
	QString description() const override;
	QJsonObject parametersSchema() const override;
	ToolResult execute(const QJsonObject& arguments, const ToolContext& context) override;

private:
	Intent m_intent;
	UlinCoreToolRegistry* m_registry = nullptr;
};
} // namespace UlinCore
