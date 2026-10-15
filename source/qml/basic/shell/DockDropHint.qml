// 拖回附着时的**落点提示**：在"原来那块右栏位置"画一个半透明轮廓。
//
// 为什么必须有：独立窗口拖到主窗口上时，主窗口里没有任何东西会因为"你正在往这儿放"而变化
// —— 用户不知道能不能放、放了会去哪。这块轮廓就是那句回答（画的就是它将要回到的那个矩形）。
//
// 数据与判定都不在这里：矩形由 Shell 在拖动开始时按右栏的真实位置算一次（拖动期间面板可能已经收起，
// 不能每帧重算），"要不要画"由拖动的那个窗口推给 Shell（拖动中主窗口收不到 hover）。
// 这里只负责画：半透明填充 + 一圈描边 + 跟主题 token 走，**不吃鼠标事件**（纯提示）。

import QtQuick
import ULinHub 1.0

Item {
    id: root

    // 要画的矩形（**本窗口场景坐标**）：由 Shell 在拖动开始时算好
    property rect zone: Qt.rect(0, 0, 0, 0)
    // 光标是不是已经落在这块里面（里面的样式更实一点：能不能放要一眼看出来）
    property bool active: false
    // 两个方向的提示文案不一样：坞里往外拖（块**已经在**这儿）vs 独立窗口往回拖（正要放回来）
    property bool fromDock: false

    readonly property bool anyZone: zone.width > 0 && zone.height > 0

    x: zone.x
    y: zone.y
    width: Math.max(1, zone.width)
    height: Math.max(1, zone.height)
    visible: anyZone

    // 面与描边：与右栏卡片同一个圆角 token，颜色取主题的强调色（提示"这里是有效落点"）
    Rectangle {
        anchors.fill: parent
        radius: Theme.radius.panel
        color: Theme.color.accent
        opacity: root.active ? 0.16 : 0.07
        border.width: 2
        border.color: Theme.color.accent

        Behavior on opacity {
            NumberAnimation { duration: Theme.motion.fast }
        }
    }

    // 一句说明，免得只有个框让人猜（文案说清"松手就回到这里"）
    Text {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: 10
        text: root.fromDock
              ? (root.active ? "松手：块留在这里" : "拖离右栏即可把块变成独立窗口")
              : (root.active ? "松手：重新附着回右栏" : "把块拖到右栏位置可重新附着")
        color: Theme.color.textPrimary
        font.family: "Microsoft YaHei UI"
        font.pixelSize: Theme.font.caption
        opacity: root.active ? 1.0 : 0.72
    }

    // 纯提示：不吃鼠标事件。QML 的 Item 默认不接收鼠标事件（要 MouseArea 才收），所以这里
    // **刻意什么都不放** —— 放一个 `enabled: false` 的 MouseArea 是自欺（它本来就不吃）。
}
