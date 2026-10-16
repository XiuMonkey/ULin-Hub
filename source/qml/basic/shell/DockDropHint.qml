// 拖动右栏时的**落点提示**：在"原来那块右栏位置"画一个半透明轮廓。
//
// 为什么必须有：独立窗口拖到主窗口上时，主窗口里没有任何东西会因为"你正在往这儿放"而变化
// —— 用户不知道能不能放、放了会去哪。这块轮廓就是那句回答（画的就是它将要回到的那个矩形）。
//
// 数据与判定都不在这里：矩形由 Shell 按右栏**附着时**的真实位置算（收起/脱离时读不到它的坐标，
// 所以用"开着的几何"算），"要不要画"由正在拖的那个抓手推给 Shell（拖动中主窗口收不到 hover）。
// 这里只负责画：半透明填充 + 一圈描边 + 跟主题 token 走，**不吃鼠标事件**（纯提示）。
//
// ⚠️ 文案说的是"面板/右栏"（本次需求：拖的是整个右栏）。上一轮"按块脱离"的措辞（"块"）已经
//    没有调用方（抓手不再请求脱离某个块），这里只在两个方向之间切换文案。

import QtQuick
import ULinHub 1.0

Item {
    id: root

    // 要画的矩形（**本窗口场景坐标**）：由 Shell 按右栏附着时的几何算
    property rect zone: Qt.rect(0, 0, 0, 0)
    // 光标是不是已经落在这块里面（里面的样式更实一点：能不能放要一眼看出来）
    property bool active: false
    // 两个方向的提示文案不一样：附着态往外拖（面板**已经在**这儿）vs 独立窗口往回拖（正要放回来）
    property bool fromDock: false
    // 第三种情况（本次新增）：三点刚把面板"拿起来"、正跟着光标走 —— 这时它已经不在原位了，
    // 文案要说的是"松手回哪儿"，而不是"把三个点拖离右栏"（那句已经不成立了）。
    property bool floating: false

    readonly property bool anyZone: zone.width > 0 && zone.height > 0

    x: zone.x
    y: zone.y
    width: Math.max(1, zone.width)
    height: Math.max(1, zone.height)
    visible: anyZone

    // 面与描边：与右栏卡片同一个圆角 token。颜色用**浅灰**（与三个点抓手同款
    // Theme.color.textTertiary）—— 原来取的是主题强调色 accent（蓝），实测观感不好看。
    // 灰的对比度比蓝低，所以把填充透明度略微调高，保证"亮起"这一档仍然看得出来。
    Rectangle {
        anchors.fill: parent
        radius: Theme.radius.panel
        color: Theme.color.textTertiary
        opacity: root.active ? 0.22 : 0.10
        border.width: 2
        border.color: Theme.color.textTertiary

        Behavior on opacity {
            NumberAnimation { duration: Theme.motion.fast }
        }
    }

    // 一句说明，免得只有个框让人猜（文案说清"松手会怎样"）
    Text {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: 10
        text: root.floating
              ? (root.active ? "松手：回到右栏" : "松手：面板成为独立窗口")
              : (root.fromDock
                 ? (root.active ? "松手：面板变成独立窗口" : "把三个点拖离右栏，面板就变成独立窗口")
                 : (root.active ? "松手：重新附着回右栏" : "把面板拖到右栏位置即重新附着"))
        color: Theme.color.textPrimary
        font.family: "Microsoft YaHei UI"
        font.pixelSize: Theme.font.caption
        opacity: root.active ? 1.0 : 0.72
    }

    // 纯提示：不吃鼠标事件。QML 的 Item 默认不接收鼠标事件（要 MouseArea 才收），所以这里
    // **刻意什么都不放** —— 放一个 `enabled: false` 的 MouseArea 是自欺（它本来就不吃）。
}
