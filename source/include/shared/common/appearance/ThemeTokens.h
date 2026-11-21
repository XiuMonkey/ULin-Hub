#pragma once

// 纯 token 数据与 metrics.json 覆盖规则：放 common，ThemeBridge 只做 QML 属性面。
#include <QVariantMap>

class QJsonObject;

namespace ThemeTokens
{
QVariantMap radius();
QVariantMap font();
QVariantMap spacing();
QVariantMap size();
QVariantMap shadow();
QVariantMap motion();
void applyMetrics(QVariantMap& target, const QJsonObject& root, const QString& group);
}
