// 离线渲染试验入口：把风格包覆盖件拼成一张"模拟主界面"，用于出预览图。
// 只在 harness 里跑（宿主真实界面见 source/qml/Main.qml），不随包发布。
import QtQuick
import QtQuick.Window
import ULinHub 1.0
import "_work/ui/widgets"
import "_work/ui/skin/session"
import "_work/ui/skin/settings"
import "_work/ui/skin/chat"

Window {
    id: win

    property string outPath: ""

    width: 1280
    height: 800
    visible: true
    color: "transparent"
    title: "harness"

    Rectangle {
        id: surface
        anchors.fill: parent
        radius: Theme.radius.window
        color: Theme.color.windowBg
        clip: true

        Loader {
            anchors.fill: parent
            source: Style.url("skin/window/Backdrop.qml")
        }

        Column {
            anchors.fill: parent

            Loader {
                id: titleBar
                width: parent.width
                source: Style.url("skin/window/TitleBar.qml")
            }

            Row {
                width: parent.width
                height: parent.height - titleBar.height

                // ---------------- 左侧会话栏 ----------------
                Item {
                    id: sideBar
                    width: 240
                    height: parent.height

                    Card {
                        anchors.fill: parent
                        radius: Theme.radius.panel
                        color: Theme.color.sessionBarBg
                        padding: 0
                        shadowEnabled: true
                        bordered: false
                    }

                    Column {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: 10
                        spacing: 6

                        SessionSearchBox { width: parent.width; placeholder: "搜索会话" }

                        Text {
                            text: "工作区"
                            color: Theme.color.textCaption
                            font.family: "Microsoft YaHei UI"
                            font.pixelSize: 11
                            leftPadding: 8
                            bottomPadding: 4
                        }

                        SessionRow { width: parent.width; title: "重构插件转译器的 IR 约定" }
                        SessionRow { width: parent.width; title: "把右侧栏展开开销降下来"; selected: true; timeText: "12 分" }
                        SessionRow { width: parent.width; title: "主题包：壁纸与玻璃拟物"; running: true }
                        SessionRow { width: parent.width; title: "WorkBuddy 当后端：ACP 探针"; timeText: "昨天" }
                        SessionRow { width: parent.width; title: "扩展描述文件分流规则" ; timeText: "昨天" }
                        SessionRow { width: parent.width; title: "把自己叠进宿主的窗口" }
                    }

                    Row {
                        anchors.left: parent.left
                        anchors.bottom: parent.bottom
                        anchors.margins: 10
                        spacing: 4

                        IconButton { glyph: "⚙"; tooltip: "设置" }
                        IconButton { glyph: "◎"; tooltip: "外观" }
                        IconButton { glyph: "☰"; tooltip: "更多" }
                    }
                }

                // ---------------- 中间会话区 ----------------
                Item {
                    id: center
                    width: parent.width - sideBar.width - rightDock.width
                    height: parent.height

                    Column {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: 18
                        spacing: 14

                        Column {
                            spacing: 3

                            Text {
                                text: "重构插件转译器的 IR 约定"
                                color: Theme.color.textPrimary
                                font.family: "Microsoft YaHei UI"
                                font.pixelSize: Theme.font.display
                                font.weight: Theme.font.weightBold
                            }

                            Text {
                                text: "deepseek-v4 · 12 分钟前 · 工具调用 3 次"
                                color: Theme.color.textTertiary
                                font.family: "Microsoft YaHei UI"
                                font.pixelSize: Theme.font.caption
                            }
                        }

                        Row {
                            spacing: 6
                            PillButton { text: "deepseek-v4"; pillHeight: 22 }
                            PillButton { text: "思索引擎"; pillHeight: 22; selected: true }
                            PillButton { text: "工具 3"; pillHeight: 22 }
                        }

                        SegmentedTabs {
                            model: [{ key: "conversation", label: "对话" },
                                    { key: "trace", label: "轨迹" },
                                    { key: "files", label: "文件" }]
                            currentKey: "conversation"
                        }

                        Text {
                            text: "按你说的：不贴壁纸，只改形状、描边、图案和颜色。\n" +
                                  "下面这套是白天的样子，切到深色会自动换成同一套材质的暗色档。"
                            wrapMode: Text.Wrap
                            width: parent.width
                            lineHeight: 1.45
                            color: Theme.color.textPrimary
                            font.family: "Microsoft YaHei UI"
                            font.pixelSize: Theme.font.body
                        }

                        UserBubble {
                            width: parent.width
                            text: "把窗口、卡片、按钮、气泡的造型都换掉，各来一套风格。"
                        }

                        Card {
                            width: parent.width
                            height: 150
                            color: Theme.color.cardBg
                            padding: Theme.spacing.cardPadding

                            Column {
                                spacing: 8

                                Text {
                                    text: "ULin Hub · 风格包"
                                    color: Theme.color.textPrimary
                                    font.family: "Microsoft YaHei UI"
                                    font.pixelSize: Theme.font.title
                                    font.weight: Theme.font.weightBold
                                }

                                Text {
                                    width: center.width - 72
                                    text: "风格包是 B 层覆盖件：同名文件遮蔽内置，没覆盖的自动回退。"
                                          + "所以同一份皮肤既能换色板，也能换形状、描边、图案与动效。"
                                    wrapMode: Text.Wrap
                                    color: Theme.color.textSecondary
                                    font.family: "Microsoft YaHei UI"
                                    font.pixelSize: Theme.font.body
                                }

                                Row {
                                    spacing: 8

                                    ActionButton { width: 150; text: "应用这个风格"; variant: "primary" }
                                    ActionButton { width: 120; text: "预览"; variant: "elevated" }
                                    ActionButton { width: 96; text: "取消"; variant: "ghost" }
                                }
                            }
                        }
                    }

                    // 输入卡
                    Card {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        anchors.margins: 18
                        height: 104
                        radius: Theme.radius.composer
                        color: Theme.color.composerBg
                        borderColor: Theme.color.composerBorder
                        padding: 14

                        Column {
                            spacing: 8

                            Text {
                                text: "描述你想要的样子…"
                                color: Theme.color.textTertiary
                                font.family: "Microsoft YaHei UI"
                                font.pixelSize: Theme.font.body
                            }

                            Row {
                                spacing: 6

                                PillButton { text: "+"; pillHeight: 24 }
                                PillButton { text: "工具"; pillHeight: 24 }
                                IconButton { boxSize: 30; glyph: "↑"; glyphColor: Theme.color.textOnAccent; circleHoverOverlay: true }
                            }
                        }
                    }
                }

                // ---------------- 右侧面板坞 ----------------
                Item {
                    id: rightDock
                    width: 300
                    height: parent.height

                    Card {
                        anchors.fill: parent
                        radius: Theme.radius.panel
                        color: Theme.color.panelBg
                        padding: 0
                        shadowEnabled: true
                        bordered: false
                    }

                    Column {
                        anchors.fill: parent
                        anchors.margins: 12
                        spacing: 10

                        Text {
                            text: "设置"
                            color: Theme.color.textPrimary
                            font.family: "Microsoft YaHei UI"
                            font.pixelSize: Theme.font.title
                            font.weight: Theme.font.weightBold
                        }

                        Text {
                            text: "服务地址"
                            color: Theme.color.textSecondary
                            font.family: "Microsoft YaHei UI"
                            font.pixelSize: Theme.font.caption
                        }

                        FieldBox { width: parent.width; placeholder: "https://api.example.com" }

                        Text {
                            text: "接口密钥"
                            color: Theme.color.textSecondary
                            font.family: "Microsoft YaHei UI"
                            font.pixelSize: Theme.font.caption
                            topPadding: 2
                        }

                        FieldBox { width: parent.width; placeholder: "sk-…"; password: true }

                        ActionButton { width: parent.width; text: "保存"; variant: "primary" }
                        ActionButton { width: parent.width; text: "测试连接"; variant: "elevated" }
                    }
                }
            }
        }
    }

    // 渲染完成后把整窗抓成 PNG
    Timer {
        interval: 900
        running: true
        repeat: false
        onTriggered: {
            if (win.outPath.length > 0)
                surface.grabToImage(function (result) {
                    result.saveToFile(win.outPath)
                    Qt.quit()
                })   // 不给 targetSize：按窗口 DPR 抓（本机 2×，1280 逻辑宽 -> 2560 像素）
            else
                Qt.quit()
        }
    }

    Component.onCompleted: {
        for (var i = 0; i < Qt.application.arguments.length; ++i) {
            if (Qt.application.arguments[i] === "--out" && i + 1 < Qt.application.arguments.length)
                win.outPath = Qt.application.arguments[i + 1]
        }
    }
}
