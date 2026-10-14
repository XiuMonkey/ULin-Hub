// 右栏的"面板坞"：顶部一条块（页签）+ 主界面按钮，下面是当前块的内容或启动器。
//
// 交互（按需求）：
//   · 打开右栏**不是**直接进浏览器，而是先看到启动器：终端 / 网页浏览 / 文件浏览；
//   · 点其中一个就**在顶部开一个块**（页签），内容 = 该块的界面；
//   · 顶部左侧的 ⌂ 主界面：回到启动器，**已开的块保留**，可以再开新块；
//   · 块可以关（页签上的 ✕），关光了自动回主界面。
//
// ⚠️ 网页块需要 Qt WebEngine：它是个**运行时** QML 模块依赖，所以这里用 webEngineAvailable 门控，
//    未就绪时给出说明而不是去建 WebEngineView —— 在没调 QtWebEngineQuick::initialize() 的构建里
//    直接建 WebEngineView 会崩（不是报错）。

import QtQuick
import ULinHub 1.0
import "../../widgets"
import "../browser"

Item {
    id: dock

    // 已打开的块：[{ kind: "browser"|"terminal"|"files", title: "…" }]
    property var tabs: []
    property int activeIndex: -1
    // 主界面（启动器）是否显示
    property bool homeVisible: true
    // 本构建是否带 Qt WebEngine（装好后连同 vcxproj 的 webenginequick 与 main.cpp 的 initialize 一起打开）
    readonly property bool webEngineAvailable: true

    readonly property var activeTab: (activeIndex >= 0 && activeIndex < tabs.length) ? tabs[activeIndex] : null

    function kindName(kind) {
        if (kind === "browser")
            return "网页浏览"
        if (kind === "terminal")
            return "终端"
        return "文件浏览"
    }

    function openTab(kind) {
        const list = tabs.slice()
        let seq = 0
        for (let i = 0; i < list.length; ++i)
            if (list[i].kind === kind)
                seq++
        list.push({ kind: kind, title: kindName(kind) + " " + (seq + 1) })
        tabs = list
        activeIndex = list.length - 1
        homeVisible = false
    }

    function closeTab(index) {
        const list = tabs.slice()
        list.splice(index, 1)
        tabs = list
        if (activeIndex >= list.length)
            activeIndex = list.length - 1
        if (list.length === 0)
            homeVisible = true
    }

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
                onClicked: dock.homeVisible = true
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

                    Repeater {
                        model: dock.tabs

                        delegate: Rectangle {
                            id: block
                            required property var modelData
                            required property int index

                            // ✕ 命中区宽度：右侧这一条点下去是"关块"，其余地方是"切到该块"
                            readonly property real closeZone: 22
                            readonly property bool closeHover: blockMouse.containsMouse
                                                               && blockMouse.mouseX > width - closeZone

                            height: row.height
                            width: Math.min(150, blockLabel.implicitWidth + 34)
                            radius: Theme.radius.small
                            color: (!dock.homeVisible && block.index === dock.activeIndex)
                                   ? Theme.color.hoverBg : "transparent"
                            border.width: 1
                            border.color: (!dock.homeVisible && block.index === dock.activeIndex)
                                          ? Theme.color.border : "transparent"

                            Text {
                                id: blockLabel
                                anchors.left: parent.left
                                anchors.leftMargin: 8
                                anchors.right: closeGlyph.left
                                anchors.rightMargin: 4
                                anchors.verticalCenter: parent.verticalCenter
                                text: block.modelData.title
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
                                    if (block.closeHover)
                                        dock.closeTab(block.index)
                                    else {
                                        dock.activeIndex = block.index
                                        dock.homeVisible = false
                                    }
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

            // 启动器（主界面）
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
                    model: [
                        { kind: "terminal", label: "终端", hint: "在右栏里跑命令（待接入 dsh 的 terminal 面）" },
                        { kind: "browser", label: "网页浏览", hint: "用 Qt WebEngine 在右栏看网页" },
                        { kind: "files", label: "文件浏览", hint: "浏览工作区文件（待接入 workspaceFiles）" }
                    ]

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
                            onClicked: dock.openTab(launcherRow.modelData.kind)
                        }
                    }
                }
            }

            // 当前块
            Loader {
                id: tabContent
                anchors.fill: parent
                visible: !dock.homeVisible

                sourceComponent: {
                    if (dock.homeVisible || dock.activeTab === null)
                        return null
                    if (dock.activeTab.kind === "browser")
                        return dock.webEngineAvailable ? browserComp : browserMissingComp
                    return placeholderComp
                }
            }

            Component {
                id: browserComp
                WebBrowserPanel { anchors.fill: parent }
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
                    title: dock.activeTab ? dock.activeTab.title : ""
                    detail: dock.activeTab && dock.activeTab.kind === "terminal"
                            ? "终端还没接入：需要挂上 dsh 的 api-terminal-controller（terminal/*）再把输出接进一个字符网格。"
                            : "文件浏览还没接入：需要挂上 api-workspace-files（workspaceFiles/*）做目录树与预览。"
                }
            }
        }
    }
}
