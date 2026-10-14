// 右栏的"面板坞"：顶部一条块（页签）+ 主界面按钮，下面是当前块的内容或启动器。
//
// 交互（按需求）：
//   · 打开右栏**不是**直接进浏览器，而是先看到启动器：终端 / 网页浏览 / 文件浏览；
//   · 点其中一个就**在顶部开一个块**（页签），内容 = 该块的界面；
//   · 顶部左侧的 ⌂ 主界面：回到启动器，**已开的块保留**，可以再开新块；
//   · 块可以关（页签上的 ✕），关光了自动回主界面。
//
// 本文件是**纯 delegate**：块列表（增删、同类编号、活动索引收敛、关光了回主界面、kind 的中文名）
// 全在 C++ 模型 source/src/common/shell/PanelDockModel.cpp，QML 面是单例 `Dock`；
// 这里只剩布局/绘制与那几个交互细节（✕ 的命中区、内容重放动画、内容路由）。
//
// ⚠️ 网页块需要 Qt WebEngine：它是个**运行时** QML 模块依赖，所以这里用 webEngineAvailable 门控，
//    未就绪时给出说明而不是去建 WebEngineView —— 在没调 QtWebEngineQuick::initialize() 的构建里
//    直接建 WebEngineView 会崩（不是报错）。

import QtQuick
import ULinHub 1.0
import "../../widgets"
import "../browser"
import "../terminal"

Item {
    id: dock

    // 块列表 / 显示状态都在模型里；这两个只读镜像**只为接信号**：
    // 值一变（开块、切块、回主界面）就重放一次内容动画。判定语义与搬家前一致 ——
    // 模型侧"值没变不发通知"，所以点当前块不会白播一次动画。
    readonly property int activeIndex: Dock.activeIndex
    readonly property bool homeVisible: Dock.homeVisible
    // 本构建是否带 Qt WebEngine（装好后连同 vcxproj 的 webenginequick 与 main.cpp 的 initialize 一起打开）
    readonly property bool webEngineAvailable: true

    // 内容出现/切换的动态：开块、切块、回主界面都重放一次（0 → 1）
    property real contentT: 1

    NumberAnimation {
        id: contentIn
        target: dock
        property: "contentT"
        from: 0
        to: 1
        duration: Theme.motion.normal
        easing.type: Easing.OutCubic
    }

    function replayContent() {
        contentT = 0;
        contentIn.start();
    }

    onActiveIndexChanged: replayContent()
    onHomeVisibleChanged: replayContent()

    Column {
        anchors.fill: parent
        spacing: 0

        // ---- ① 顶部：主界面按钮 + 块（页签）----
        Item {
            id: tabStrip
            width: parent.width
            height: 38

            IconButton {
                id: homeButton
                anchors.left: parent.left
                anchors.leftMargin: 4
                anchors.verticalCenter: parent.verticalCenter
                glyph: "⌂"
                tooltip: "主界面"
                selected: dock.homeVisible
                onClicked: Dock.showHome()   // ⌂ 只切显示，块保留
            }

            // 块：横向排，多了可滚动
            Flickable {
                anchors.left: homeButton.right
                anchors.leftMargin: 4
                anchors.right: parent.right
                anchors.rightMargin: 4
                anchors.verticalCenter: parent.verticalCenter
                height: 26
                contentWidth: row.implicitWidth
                clip: true
                flickableDirection: Flickable.HorizontalFlick
                boundsBehavior: Flickable.StopAtBounds

                Row {
                    id: row
                    height: parent.height
                    spacing: 4

                    // 模型角色有 kind / title 两个；这里只用得上 title（内容路由读 Dock.activeKind）
                    Repeater {
                        model: Dock

                        delegate: Rectangle {
                            id: block
                            required property string title
                            required property string blockId
                            required property int index

                            // ✕ 命中区宽度：右侧这一条点下去是"关块"，其余地方是"切到该块"
                            readonly property real closeZone: 22
                            readonly property bool closeHover: blockMouse.containsMouse
                                                               && blockMouse.mouseX > width - closeZone

                            height: row.height
                            width: Math.min(150, blockLabel.implicitWidth + 34)
                            radius: Theme.radius.small
                            color: (!dock.homeVisible && block.index === Dock.activeIndex)
                                   ? Theme.color.hoverBg : "transparent"
                            border.width: 1
                            border.color: (!dock.homeVisible && block.index === Dock.activeIndex)
                                          ? Theme.color.border : "transparent"

                            Text {
                                id: blockLabel
                                anchors.left: parent.left
                                anchors.leftMargin: 8
                                anchors.right: closeGlyph.left
                                anchors.rightMargin: 4
                                anchors.verticalCenter: parent.verticalCenter
                                text: block.title
                                elide: Text.ElideRight
                                color: Theme.color.textSecondary
                                font.family: "Microsoft YaHei UI"
                                font.pixelSize: Theme.font.caption
                            }

                            Text {
                                id: closeGlyph
                                anchors.right: parent.right
                                anchors.rightMargin: 6
                                anchors.verticalCenter: parent.verticalCenter
                                text: "✕"
                                color: block.closeHover ? Theme.color.textPrimary : Theme.color.textTertiary
                                font.family: "Segoe UI Symbol"
                                font.pixelSize: Theme.font.small
                            }

                            // 块内**唯一**的 MouseArea：右侧 closeZone 那条是"关块"，其余是"切到该块"。
                            // ⚠️ 别给 ✕ 单独放 MouseArea：同级里后声明的铺满 MouseArea 在更上层，
                            //    会把 ✕ 的点击整个吃掉（"关闭按钮点了没反应"就是这个原因）。
                            MouseArea {
                                id: blockMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: block.closeHover ? Qt.PointingHandCursor : Qt.ArrowCursor
                                onClicked: {
                                    if (block.closeHover) {
                                        // 关块：终端要真的收掉（terminal/close，之后 terminal/list 里没有它）。
                                        // 切块/收右栏只是解绑，终端进程继续跑 —— 所以只在这一条路上调它。
                                        Terminal.closeBlock(block.blockId)
                                        Dock.close(block.index)
                                    }
                                    else
                                        Dock.activate(block.index)
                                }
                            }
                        }
                    }
                }
            }
        }

        Rectangle {
            width: parent.width
            height: 1
            color: Theme.color.border
        }

        // ---- ② 内容：启动器 或 当前块 ----
        Item {
            id: body
            width: parent.width
            height: parent.height - tabStrip.height - 1

            // 出现/切换时淡入 + 轻微放大（顶栏那条块不动，只动内容区）
            opacity: dock.contentT
            scale: 0.985 + 0.015 * dock.contentT

            // 启动器（主界面）：目录也来自模型（三种块的中文名只有 C++ 那一份）
            Column {
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.margins: Theme.spacing.columnClearance
                anchors.topMargin: Theme.spacing.columnClearance
                spacing: 8
                visible: dock.homeVisible

                Text {
                    text: "打开一个块"
                    color: Theme.color.textTertiary
                    font.family: "Microsoft YaHei UI"
                    font.pixelSize: Theme.font.caption
                }

                Repeater {
                    model: Dock.launcherItems

                    delegate: Rectangle {
                        id: launcherRow
                        required property var modelData

                        width: parent.width
                        height: 44
                        radius: Theme.radius.control
                        color: launcherMouse.containsMouse ? Theme.color.hoverBg : "transparent"
                        border.width: 1
                        border.color: launcherMouse.containsMouse ? Theme.color.border : "transparent"

                        Column {
                            anchors.left: parent.left
                            anchors.leftMargin: 10
                            anchors.right: parent.right
                            anchors.rightMargin: 10
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 2

                            Text {
                                text: launcherRow.modelData.label
                                color: Theme.color.textPrimary
                                font.family: "Microsoft YaHei UI"
                                font.pixelSize: Theme.font.body
                            }
                            Text {
                                width: parent.width
                                text: launcherRow.modelData.hint
                                elide: Text.ElideRight
                                color: Theme.color.textTertiary
                                font.family: "Microsoft YaHei UI"
                                font.pixelSize: Theme.font.caption
                            }
                        }

                        MouseArea {
                            id: launcherMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: Dock.open(launcherRow.modelData.kind)
                        }
                    }
                }
            }

            // 当前块
            Loader {
                id: tabContent
                anchors.fill: parent
                visible: !dock.homeVisible

                // 内容路由：活动块的 kind 也来自模型（绑定在带通知的属性上，
                // 关掉活动块前面那块时这里会跟着重算，不会停在旧块上）
                sourceComponent: {
                    if (dock.homeVisible || Dock.activeIndex < 0)
                        return null
                    if (Dock.activeKind === "browser")
                        return dock.webEngineAvailable ? browserComp : browserMissingComp
                    if (Dock.activeKind === "terminal")
                        return dock.webEngineAvailable ? terminalComp : terminalMissingComp
                    return placeholderComp
                }
            }

            Component {
                id: browserComp
                WebBrowserPanel { anchors.fill: parent }
            }

            Component {
                id: terminalComp
                // ⚠️ 传的是**稳定 id** 而不是索引：关掉前面那块时索引会前移，拿索引当会话 key 会串台
                TerminalPanel {
                    anchors.fill: parent
                    blockId: Dock.activeBlockId
                }
            }

            Component {
                id: terminalMissingComp
                PanelPlaceholder {
                    anchors.fill: parent
                    title: "终端需要 Qt WebEngine"
                    detail: "本构建未包含 Qt WebEngine：终端面板里的 xterm.js 跑在 WebEngine 里"
                            + "（引擎只给原始字节与序列化 VT 串，没有网格接口，所以不能自绘）。"
                }
            }

            Component {
                id: browserMissingComp
                PanelPlaceholder {
                    anchors.fill: parent
                    title: "网页浏览需要 Qt WebEngine"
                    detail: "本构建未包含 Qt WebEngine（安装器里的 Qt WebChannel / Qt Positioning 还没装完）。"
                            + "装好后打开 dock.webEngineAvailable、vcxproj 的 webenginequick 与 main.cpp 的 "
                            + "QtWebEngineQuick::initialize()，这个块就会变成真正的浏览器。"
                }
            }

            Component {
                id: placeholderComp
                PanelPlaceholder {
                    anchors.fill: parent
                    title: Dock.activeTitle
                    detail: "文件浏览还没接入：需要挂上 api-workspace-files（workspaceFiles/*）做目录树与预览。"
                }
            }
        }
    }
}
