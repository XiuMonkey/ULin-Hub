// 顶栏外框面（B 层可替换单元）
// 契约：透传子项（children → 内容区）；shadow 由壳层按"宽度动画中"开关。
import QtQuick
import ULinHub 1.0
import "../../widgets"

Card {
    id: root

    property bool shadow: true

    radius: Theme.radius.panel
    padding: 0
    shadowEnabled: root.shadow
    color: Theme.color.topBarBg || Theme.color.cardBg
}
