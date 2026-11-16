// 右侧栏外框面（B 层可替换单元）
// 只画"面"：侧栏内容（标题、标签页、终端/浏览器）由 PanelBar 自己排，几何也归它。
// 默认保持原来的浮起卡片；皮肤可以让它齐平（透明 + 发丝线）。
import QtQuick
import ULinHub 1.0
import "../../widgets"

Card {
    id: root

    property bool shadow: true

    radius: Theme.radius.panel
    padding: 0
    shadowEnabled: root.shadow
    color: Theme.color.panelBg
}
