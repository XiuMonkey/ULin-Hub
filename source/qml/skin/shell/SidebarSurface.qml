// 侧栏外框面（B 层可替换单元）
// 契约：透传子项（children → 内容区）、透传尺寸；只决定"面"本身的外观。
// 壳层只负责几何（x/width/opacity/anchor），不再设置颜色/描边/阴影 —— 那些归本件。
import QtQuick
import ULinHub 1.0
import "../../widgets"

Card {
    id: root

    radius: Theme.radius.panel
    padding: 0
    shadowEnabled: true
    color: Theme.color.sessionBarBg || Theme.color.cardBg
    bordered: false          // 原版侧栏没有描边
}
