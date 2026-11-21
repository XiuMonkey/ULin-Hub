#pragma once

#include "common/agent/ulincore/intent/UlinCoreIntent.h"

#include <QList>

namespace UlinCore
{
// 内置 Intent 声明：只描述名字/参数/executor ID，不含执行代码。
QList<Intent> builtinIntents();
} // namespace UlinCore
