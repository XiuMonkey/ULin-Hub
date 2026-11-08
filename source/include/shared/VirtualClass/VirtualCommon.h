#pragma once

// 兼容伞头：旧的扩展/宿主代码仍可 #include "VirtualClass/VirtualCommon.h"。
// 新代码请按需包含具体接口头，避免一个头里塞进全部宿主能力。
//
// 接口文件：
//   VirtualTheme.h              主题
//   VirtualMain.h               主窗口 / 数据注入
//   VirtualConnectionManager.h  信号槽登记表
//   VirtualTranslation.h        多语言
//   VirtualUiHost.h             UI 注入 / 账户入口
//   VirtualBlockBackendHost.h   面板块后端替换

#include "VirtualClass/VirtualTheme.h"
#include "VirtualClass/VirtualMain.h"
#include "VirtualClass/VirtualConnectionManager.h"
#include "VirtualClass/VirtualTranslation.h"
#include "VirtualClass/VirtualUiHost.h"
#include "VirtualClass/VirtualBlockBackendHost.h"
