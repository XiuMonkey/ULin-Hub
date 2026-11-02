// iOS 玻璃 · 自绘标题栏（扁平玻璃条）
// ---------------------------------------------------------------------------
// 契约与内置 skin/window/TitleBar.qml 一致：整块是拖动区（HostUi.registerItem），
// 三个按钮各自 addWindowControl 登记；不自己写拖动逻辑。
//
// 造型改过一轮：第一版是"离窗口边缘留白的悬空胶囊 + 重投影"，那是 iOS 的浮层语言，
// 搬到桌面窗口上会显得像玩具。现在恢复成**贴边的一整条**：满宽、无圆角、无投影，
// 底色是压得极淡的玻璃（tint 0.5），靠底部一条 1px 分隔线跟内容区分开。
// 折射仍然开着 —— 拖动窗口时条里的色温会跟着壁纸走，这是"活的玻璃"，
// 但因为壁纸本身已经很淡，读起来就是一条干净的白条，不会抢戏。
//
// 窗口键也换了语言：不再是带描边的白色圆片（那是 macOS 的红绿灯换皮），
// 改成无底的 32×32 命中区 + 只有悬停时浮出的一层淡灰，图标用 1px 细线，
// 关掉那颗悬停时变 systemRed 实心。
import QtQuick
import ULinHub 1.0

Item {
    id: bar

    Component.onCompleted: HostUi.registerItem("titleBar", this)

    implicitHeight: Theme.size.titleBarHeight

    // readonly 是为了让外部（宿主的拖动区）拿到"玻璃条真正的边界"。
    // 贴边之后没有留白，两个值退化成一个 —— 保留名字是因为宿主按名字取。
    readonly property real barInset: 0
    readonly property real barHeight: implicitHeight

    Glass {
        id: surface

        anchors.fill: parent
        radius: 0

        refract: true
        refractSoften: 1.4
        refractAlpha: 0.86
        tint: Theme.isDark ? Qt.rgba(0.106, 0.106, 0.118, 0.46) : Qt.rgba(1, 1, 1, 0.34)
        // 贴边的条不该有完整一圈描边，只留底部那条线（见下）
        bordered: false
        // 顶部那道白光在贴边时会被窗口上沿切掉一半，看着像脏边，关掉
        specular: false
        // 竖向厚度也压到几乎为零：整条要读成平面
        sheen: 0.35
        shadowBlur: 0
    }

    // 底部分隔线：现代界面里这一条比任何阴影都更能说明"这里是标题栏"
    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        color: Theme.color.border
    }

    // 滚动边缘光晕（iOS 26 的 scroll edge effect）：玻璃条下面拉一条 12px 的柔影，
    // 内容从它底下"过"过去，标题栏才有"浮在内容上"的层次。
    // z 要高过同一个 Column 里的兄弟节点，否则会被下面的内容整块盖住。
    Rectangle {
        z: 100
        anchors.top: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        height: 12
        gradient: Gradient {
            GradientStop {
                position: 0.0
                color: Qt.rgba(0.06, 0.09, 0.16, Theme.isDark ? 0.30 : 0.055)
            }
            GradientStop { position: 1.0; color: Qt.rgba(0.06, 0.09, 0.16, 0.0) }
        }
    }

    Image {
        id: logo
        anchors.left: parent.left
        anchors.leftMargin: 16
        anchors.verticalCenter: parent.verticalCenter
        width: Theme.size.logoHeight
        height: Theme.size.logoHeight
        source: Theme.isDark
            ? "qrc:/ULinHub/icons/ulin-hub-icon-dark.png"
            : "qrc:/ULinHub/icons/ulin-hub-icon.png"
        fillMode: Image.PreserveAspectFit
        smooth: true
    }

    // 词标：SF 风格的字重 + 负字距。这里没有 SF Pro，用系统雅黑的中黑 + 收紧一点
    // 的字距去贴，比直接拉粗字重更接近。字号比内置件小一档，标题不该比正文更响。
    Text {
        id: titleText
        anchors.left: logo.right
        anchors.leftMargin: 9
        anchors.verticalCenter: parent.verticalCenter
        text: "ULin Hub"
        color: Theme.color.textPrimary
        font.family: "Microsoft YaHei UI"
        font.pixelSize: Theme.font.title
        font.weight: 600
        font.letterSpacing: 0.1
    }

    Row {
        id: buttons
        anchors.right: parent.right
        anchors.rightMargin: 10
        anchors.verticalCenter: parent.verticalCenter
        spacing: 2

        Repeater {
            model: ["min", "max", "close"]

            delegate: Item {
                id: button

                required property string modelData
                readonly property string kind: modelData

                readonly property bool hovered: mouseArea.containsMouse
                readonly property bool pressed: mouseArea.pressed
                readonly property bool isClose: kind === "close"

                // 命中区 32×32，可见的底也是 32×32 —— 但默认是透明的，
                // 只有指针上来才浮出一层淡灰。静止时整条上没有任何"按钮"的形状。
                implicitWidth: 32
                implicitHeight: 32

                readonly property string glyph: kind === "min"
                    ? "−"
                    : (isClose ? "✕" : (WindowBridge.maximized ? "❐" : "□"))

                Component.onCompleted: WindowBridge.addWindowControl(button, kind)
                Component.onDestruction: WindowBridge.removeWindowControl(button)

                Rectangle {
                    anchors.fill: parent
                    radius: 9
                    color: {
                        if (button.isClose && (button.hovered || button.pressed))
                            return Theme.color.danger
                        if (button.pressed)
                            return Qt.rgba(0, 0, 0, Theme.isDark ? 0.30 : 0.09)
                        if (button.hovered)
                            return Qt.rgba(0, 0, 0, Theme.isDark ? 0.22 : 0.055)
                        return "transparent"
                    }
                    Behavior on color { ColorAnimation { duration: 120 } }
                }

                Text {
                    anchors.centerIn: parent
                    text: button.glyph
                    // 符号字体，小一号，细笔画 —— 雅黑画这个符号会发胖
                    font.family: "Segoe UI Symbol"
                    font.pixelSize: button.isClose ? 11 : 12
                    color: (button.isClose && (button.hovered || button.pressed))
                        ? "#FFFFFF"
                        : (button.hovered ? Theme.color.textPrimary : Theme.color.textSecondary)
                    Behavior on color { ColorAnimation { duration: 120 } }
                }

                MouseArea {
                    id: mouseArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.ArrowCursor
                    onClicked: {
                        if (button.kind === "min")
                            WindowBridge.minimize()
                        else if (button.kind === "max")
                            WindowBridge.toggleMaximize()
                        else
                            WindowBridge.closeWindow()
                    }
                }
            }
        }
    }
}
