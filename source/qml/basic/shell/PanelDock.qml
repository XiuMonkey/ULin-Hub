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
// ⚠️ 网页/终端块需要 Qt WebEngine：它是**运行时** QML 模块依赖（vcxproj 的 webenginequick +
//    main.cpp 的 QtWebEngineQuick::initialize()）。缺了它这两个块会被降级成说明页，判定在
//    C++ 侧（PanelDockModel::viewFor）—— 未 initialize 的构建里建 WebEngineView 是直接崩，
//    不是报错，所以不能"先试后错"，判定权必须在决定装载之前。
//
// 子界面一律经 Style.url() 装载（本仓库的既有范式），**不用相对 import** —— 相对 import 拿不到
// 风格包的遮蔽能力。所以本文件不 import ../browser 与 ../terminal，视图路径由 C++ 的视图表给。

import QtQuick
import ULinHub 1.0
import "../../widgets"

Item {
    id: dock

    // 块列表 / 显示状态都在模型里；这两个只读镜像**只为接信号**：
    // 值一变（开块、切块、回主界面）就重放一次内容动画。判定语义与搬家前一致 ——
    // 模型侧"值没变不发通知"，所以点当前块不会白播一次动画。
    readonly property int activeIndex: Dock.activeIndex
    readonly property bool homeVisible: Dock.homeVisible

    // 当前块是否**已经脱离**成独立窗口（那个块的内容由 basic/shell/DetachedBlockWindow.qml 装载，
    // 坞里不能同时再装一份 WebEngineView）。判定读 Dock.detachedBlocks（状态在模型里）。
    readonly property bool activeDetached: {
        const list = Dock.detachedBlocks
        for (let i = 0; i < list.length; ++i) {
            if (list[i].blockId === Dock.activeBlockId)
                return true
        }
        return false
    }

    // 三个点抓手拖出的判定（阈值按需求："离开右栏区域 >48px，或水平位移 >60px"）。
    // 只在**活动块**上生效：坞里显示的就是活动块的内容。
    signal blockDetachRequested(point atScenePos)
    signal detachDragPreview(bool overTarget)

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

        // ---- ① 顶部：主界面按钮 + 三个点抓手 + 块（页签）----
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

            // 面板的"三个点"：拖出去 ⇒ 这个块变成独立窗口（需求 ①②）。
            // 视觉与命中区规则全在 widgets/PanelGrip.qml（逐条照 PageOverlay 那颗：只有三个浅灰点、
            // 不要背景、不要悬浮/光标反馈，命中区外放 10px）。
            // ⚠️ 只在"有活动块且没脱离"时可用：没块可拖时三个点还留着就是哑控件。
            PanelGrip {
                id: dockGrip
                anchors.left: homeButton.right
                anchors.leftMargin: 6
                anchors.verticalCenter: parent.verticalCenter
                visible: !dock.homeVisible && Dock.activeIndex >= 0 && !dock.activeDetached
                enabled: visible
                // 主窗口是无边框窗口：抓手必须登记进原生命中测试，否则标题栏那条 HTCAPTION
                // 会先把按下事件吃掉（PageOverlay 的抓手就是这个原因登记的）
                nativeHost: true

                // 离开右栏区域 >48px，或水平位移 >60px ⇒ 够脱离（需求给的阈值）
                readonly property real detachSlack: 48
                readonly property real detachShiftX: 60
                property bool detachArmed: false

                onDragMoved: (dx, dy, globalPos, scenePos) => {
                    const r = dock.mapToGlobal(0, 0)
                    const outside = globalPos.x < r.x - detachSlack
                                    || globalPos.x > r.x + dock.width + detachSlack
                                    || globalPos.y < r.y - detachSlack
                                    || globalPos.y > r.y + dock.height + detachSlack
                    const armed = outside || Math.abs(dx) > detachShiftX
                    if (armed !== detachArmed) {
                        detachArmed = armed
                        dock.detachDragPreview(armed)
                    }
                }
                onDragFinished: (dx, dy, globalPos) => {
                    const armed = detachArmed
                    detachArmed = false
                    dock.detachDragPreview(false)
                    if (armed)
                        dock.blockDetachRequested(dock.mapFromItem(null, globalPos.x, globalPos.y))
                }
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
                            required property string kind
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
                                        // 后端被扩展顶替时也一样调：桥会把 closeBlock 转给扩展的后端
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
            // ⚠️ 块脱离后这里**不装**（active=false）也不会画（visible=false）：那个块的界面对不起
            //    第二份（终端/网页都是 WebEngineView，同页两份会各自跑一份渲染进程）。它此时在
            //    DetachedBlockWindow 里，坞里留一块干净的空面 + 一句说明。
            Loader {
                id: tabContent
                anchors.fill: parent
                active: !dock.activeDetached
                visible: !dock.homeVisible && !dock.activeDetached

                // 内容路由：`kind → 视图` 的映射表在 C++（PanelDockModel::viewFor），这里只按 C++ 给的
                // 那组值装机 —— 路由里不再出现任何 kind 字符串（与启动器表、中文块名同一条分层）。
                // 活动块的那组值都绑在带通知的属性上，关掉活动块前面那块时会跟着重算，不会停在旧块上。
                sourceComponent: {
                    if (dock.homeVisible || Dock.activeIndex < 0)
                        return null
                    if (Dock.activeViewPath.length === 0)
                        return placeholderComp  // 没有视图：未接入 or 缺运行时依赖（文案由 C++ 给）
                    return Dock.activeViewBlockScoped ? blockScopedComp : plainComp
                }
            }

            // 纯 url 视图：路径来自 C++ 的视图表，这里只过风格包解析（<exe>/ui/ 同名文件优先于 qrc）
            Component {
                id: plainComp
                Loader {
                    anchors.fill: parent
                    source: Style.url(Dock.activeViewPath)
                }
            }

            // 要跟着"当前块"走的视图（终端）：url 同样来自视图表，但面板还要拿到当前块的稳定 id。
            // ⚠️ 用 Qt.binding 而不是 setSource 的初始属性：切到另一个同类块时宿主**不重建** item
            //    （契约与路径都没变），靠这条活绑定把新块推给面板（TerminalPanel.onBlockIdChanged
            //    → 换绑会话）。初始属性只能给一次快照，切块就会串台。
            //    传的是**稳定 id** 而不是索引：关掉前面那块时索引会前移，拿索引当会话 key 会串台。
            Component {
                id: blockScopedComp
                Loader {
                    anchors.fill: parent
                    source: Style.url(Dock.activeViewPath)

                    onLoaded: {
                        if (item)
                            item.blockId = Qt.binding(function () { return Dock.activeBlockId })
                    }
                }
            }

            // 没有视图的块：标题照常是块名，说明文案由 C++ 给（与中文块名同一层，QML 不拼）
            Component {
                id: placeholderComp
                PanelPlaceholder {
                    anchors.fill: parent
                    title: Dock.activeTitle
                    detail: Dock.activeViewDetail
                }
            }

            // 活动块已脱离：坞里这块画一句说明（界面文案与块名同层，QML 只摆位置）。
            // 文案刻意说清"还能拖回来"，否则用户会以为块丢了（独立窗口上也有同样的抓手）。
            PanelPlaceholder {
                anchors.fill: parent
                visible: !dock.homeVisible && dock.activeDetached
                title: Dock.activeTitle
                detail: "这个块已经在独立窗口里。\n拖动它顶部的三个点回到右栏位置，可以重新附着。"
            }
        }
    }
}
