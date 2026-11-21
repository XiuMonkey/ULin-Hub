#pragma once

// 拆分后的桥共用依赖。只放类型，不放逻辑。
#include "ExtensionSystem/HostUi.h"
#include "common/agent/ToolsFilter.h"
#include "common/agent/ulincore/settings/UlinCoreSettings.h"
#include "common/appearance/StyleResolver.h"
#include "common/agent/dsh/pluginmarket/PluginMarketController.h"
#include "common/session/CommandController.h"
#include "common/session/SessionListModel.h"
#include "common/settings/SettingsController.h"
#include "common/shell/PanelBarDockModel.h"
#include "common/util/ConnectionManager.h"

#include <QAbstractListModel>
#include <QHash>
#include <QModelIndex>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

class DshApiClient;
