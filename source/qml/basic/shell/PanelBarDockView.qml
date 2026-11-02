// 右栏的"面板坞"：顶部块页签 + 主界面按钮，下面是当前块内容或启动器。
// 打开右栏先进启动器（终端 / 网页浏览 / 文件浏览），点一项在顶部开块；⌂ 回启动器且保留已开块；
// ✕ 关块，关光自动回主界面。
// 本文件是纯 delegate：块列表、增删、活动索引、kind 中文名都在 C++ 的 PanelBarDockModel
// （QML 单例 `PanelBarDock`），这里只剩布局/绘制、命中区、内容重放动画与内容路由。
// ⚠️ 网页/终端块需要 Qt WebEngine（运行时 QML 模块依赖：vcxproj 的 webenginequick + main.cpp 的
// initialize）：缺了会降级成说明页，判定必须在决定装载之前 —— 未 initialize 时建 WebEngineView 直接崩。
// 子界面一律经 Style.url() 装载（不用相对 import，否则拿不到风格包的遮蔽能力）。

import QtQuick
import QtQuick.Controls.Basic   // ScrollBar.vertical（启动器条目多了要能滚动；与其它列表同一套）
import QtQuick.Window
import ULinHub 1.0
import "../../widgets"

Item {
    id: dock

    // 块列表 / 显示状态都在模型里；这两个只读镜像**只为接信号**：
    // 值一变（开块、切块、回主界面）就重放一次内容动画。判定语义保持一致 ——
    // 模型侧"值没变不发通知"，所以点当前块不会白播一次动画。
    readonly property int activeIndex: PanelBarDock.activeIndex
    readonly property bool homeVisible: PanelBarDock.homeVisible

    // （这里曾有 activeDetached：判断"当前块是否已脱离成独立窗口"，读 PanelBarDock.detachedBlocks。

    property real contentT: 1   // 内容出现/切换的动态：开块、切块、回主界面都重放一次（0 → 1）

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

        Item {
            id: tabStrip
            width: parent.width
            height: 40
            z: dragGhost.visible ? 1000 : 0

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 1
                color: Theme.color.border
            }

            IconButton {
                id: homeButton
                anchors.left: parent.left
                anchors.leftMargin: 5
                anchors.verticalCenter: parent.verticalCenter
                boxSize: 26
                glyph: "⌂"
                glyphSize: Theme.font.body
                tooltip: "主界面"
                selected: dock.homeVisible
                onClicked: PanelBarDock.showHome()   // ⌂ 只切显示，块保留
            }

            IconButton {   // 这里给一个 ＋，语义就是"回主界面（启动器）"，块一个不关
                id: addButton
                anchors.right: parent.right
                anchors.rightMargin: 5
                anchors.verticalCenter: parent.verticalCenter
                boxSize: 26
                iconSize: 14
                iconSource: "qrc:/ULinHub/icons/ui/plus.svg"
                iconColor: Theme.color.iconButtonText
                tooltip: "打开面板"
                onClicked: PanelBarDock.showHome()
            }

            // 块：横向排，多了可滚动
            // ⚠️ 这条 Flickable 锚在 `⌂` 右边 4px 且**声明在最后** —— 同 z 时后声明者在上层。
            // （"用户看不见三点"就是这个原因，不是尺寸/颜色问题）。抓手已搬到 PanelBar.qml 的
            // 标题行，页签条里不再有与它重叠的锚定兄弟。
            // 横向 ListView 而不是 Row：ListView 对 rowsMoved 有 move/displaced 过渡，
            // 拖动跨过邻居中点时直接 moveBlock，页签会平滑让位，不需要等松手。
            ListView {
                id: tabList
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                anchors.left: homeButton.right
                anchors.leftMargin: 4
                anchors.right: addButton.left
                anchors.rightMargin: 4
                orientation: ListView.Horizontal
                model: PanelBarDock
                spacing: 4
                clip: true
                interactive: false
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.horizontal: AppScrollBar { }
                // 短、直的位移，不要弹性/回弹；拖拽时只做 90ms 的线性让位，
                // 避免长缓动与实时换位叠加成来回抖动。
                move: Transition {
                    NumberAnimation { properties: "x"; duration: 90; easing.type: Easing.Linear }
                }
                displaced: Transition {
                    NumberAnimation { properties: "x"; duration: 90; easing.type: Easing.Linear }
                }

                delegate: Item {
                    id: block
                    required property string title
                    required property string kind
                    required property string blockId
                    required property int index

                    readonly property real closeZone: 22
                    readonly property bool closeHover: blockMouse.containsMouse
                                                       && blockMouse.mouseX > width - closeZone
                    readonly property bool current: !dock.homeVisible
                        && block.index === PanelBarDock.activeIndex

                    property bool dragging: false
                    property bool suppressClick: false
                    property bool reorderPending: false
                    property real pressX: 0
                    property real lastReorderX: 0

                    function beginGhost(pos) {
                        dragGhostWindow.blockKind = block.kind
                        dragGhostWindow.blockTitle = block.title
                        dragGhostWindow.width = block.width
                        dragGhostWindow.height = Math.max(24, block.height - 6)
                        dragGhostWindow.visible = true
                        updateGhost(pos)
                    }
                    function updateGhost(pos) {
                        if (!dragGhostWindow.visible)
                            return
                        // 用屏幕坐标而不是侧栏内坐标：窗口是独立顶层，不受右侧栏 body 的 layer/mask 裁剪。
                        const g = block.mapToGlobal(pos.x, pos.y)
                        dragGhostWindow.x = Math.round(g.x - dragGhostWindow.width / 2)
                        dragGhostWindow.y = Math.round(g.y - dragGhostWindow.height / 2)
                    }
                    function endGhost() {
                        dragGhostWindow.visible = false
                    }

                    height: tabList.height
                    z: dragging ? 100 : 0
                    opacity: dragging ? 0 : 1

                    readonly property real naturalWidth: blockLabel.implicitWidth + 50
                    readonly property real equalWidth: {
                        const count = Math.max(1, tabList.count)
                        return (tabList.width - tabList.spacing * (count - 1)) / count
                    }
                    width: Math.min(174, Math.min(naturalWidth, Math.max(64, equalWidth)))

                    Rectangle {
                        anchors.fill: parent
                        anchors.margins: 4
                        radius: Theme.radius.small
                        color: !block.current && blockMouse.containsMouse
                            ? Theme.color.hoverBg : "transparent"
                        Behavior on color { ColorAnimation { duration: Theme.motion.fast } }
                    }

                    Rectangle {
                        anchors.left: parent.left
                        anchors.leftMargin: 6
                        anchors.right: parent.right
                        anchors.rightMargin: 6
                        anchors.bottom: parent.bottom
                        height: 2
                        radius: 1
                        color: Theme.color.accent
                        opacity: block.current ? 1 : 0
                        Behavior on opacity { NumberAnimation { duration: Theme.motion.fast } }
                    }

                    LineIcon {
                        id: blockIcon
                        anchors.left: parent.left
                        anchors.leftMargin: 8
                        anchors.verticalCenter: parent.verticalCenter
                        name: block.kind === "terminal" ? "terminal"
                            : block.kind === "browser" ? "browser" : "file"
                        size: 12
                        color: block.current ? Theme.color.accent : Theme.color.textTertiary
                    }

                    Text {
                        id: blockLabel
                        anchors.left: blockIcon.right
                        anchors.leftMargin: 5
                        anchors.right: closeGlyph.left
                        anchors.rightMargin: 4
                        anchors.verticalCenter: parent.verticalCenter
                        text: block.title
                        elide: Text.ElideRight
                        color: block.current ? Theme.color.textPrimary : Theme.color.textSecondary
                        font.family: "Microsoft YaHei UI"
                        font.pixelSize: Theme.font.caption
                        font.weight: block.current ? Theme.font.weightMedium : Font.Normal
                    }

                    LineIcon {
                        id: closeGlyph
                        anchors.right: parent.right
                        anchors.rightMargin: 6
                        anchors.verticalCenter: parent.verticalCenter
                        name: "close"
                        size: 10
                        color: block.closeHover ? Theme.color.textPrimary : Theme.color.textTertiary
                        opacity: block.current || blockMouse.containsMouse ? 1 : 0.55
                    }

                    Timer {
                        id: reorderCooldown
                        interval: 120
                        repeat: false
                        onTriggered: block.reorderPending = false
                    }

                    MouseArea {
                        id: blockMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        preventStealing: true
                        cursorShape: block.dragging ? Qt.ClosedHandCursor
                            : block.closeHover ? Qt.PointingHandCursor : Qt.ArrowCursor

                        onPressed: (mouse) => {
                            block.pressX = mouse.x
                            block.lastReorderX = 0
                            block.suppressClick = false
                        }
                        onPositionChanged: (mouse) => {
                            if (!pressed || block.closeHover)
                                return
                            const dx = mouse.x - block.pressX
                            if (!block.dragging && Math.abs(dx) < 6)
                                return
                            block.dragging = true
                            block.beginGhost(mouse)

                            const cursorX = block.mapToItem(tabList.contentItem, mouse.x, 0).x
                            let target = tabList.count - 1
                            for (let i = 0; i < tabList.count; ++i) {
                                const item = tabList.itemAtIndex(i)
                                if (item && cursorX < item.x + item.width / 2) {
                                    target = i
                                    break
                                }
                            }

                            // 换位需要同时满足：不在动画冷却期、方向确实变了、
                            // 且光标比上一次换位点又多走了 12px。后者是防抖滞回，
                            // 否则鼠标停在中线附近时模型会反复左右换位。
                            block.updateGhost(mouse)

                            if (!block.reorderPending && target !== block.index
                                    && (block.lastReorderX === 0
                                        || Math.abs(cursorX - block.lastReorderX) >= 12)) {
                                block.reorderPending = true
                                block.lastReorderX = cursorX
                                const from = block.index
                                PanelBarDock.moveBlock(from, target)
                                reorderCooldown.restart()
                            }
                        }
                        onReleased: {
                            const wasDragging = block.dragging
                            if (wasDragging) {
                                // 拖出右栏边界 = 把这个块单独变成窗口；落在栏内仍按原逻辑重排。
                                const p = block.mapToItem(dock, mouse.x, mouse.y)
                                if (p.x < -8 || p.y < -8
                                    || p.x > dock.width + 8 || p.y > dock.height + 8) {
                                    block.dragging = false
                                    block.reorderPending = false
                                    reorderCooldown.stop()
                                    block.endGhost()
                                    PanelBarDock.detachBlock(block.blockId)
                                    return
                                }
                            }
                            block.dragging = false
                            block.reorderPending = false
                            reorderCooldown.stop()
                            block.endGhost()
                            if (wasDragging) {
                                block.suppressClick = true
                                Qt.callLater(function () { block.suppressClick = false })
                            }
                        }
                        onCanceled: {
                            block.dragging = false
                            block.reorderPending = false
                            reorderCooldown.stop()
                            block.endGhost()
                        }
                        onClicked: {
                            if (block.suppressClick)
                                return
                            if (block.closeHover) {
                                Terminal.closeBlock(block.blockId)
                                PanelBarDock.close(block.index)
                            } else {
                                PanelBarDock.activate(block.index)
                            }
                        }
                    }
                }
            }

            Rectangle {
                id: dragGhost
                property string blockKind: ""
                property string blockTitle: ""
                visible: false
                width: 0
                height: 34
                radius: Theme.radius.small
                color: Theme.color.panelBg
                border.width: 1
                border.color: Theme.color.inputBorder
                opacity: 0.97
                z: 200

                Row {
                    anchors.centerIn: parent
                    spacing: 5

                    LineIcon {
                        anchors.verticalCenter: parent.verticalCenter
                        name: dragGhost.blockKind === "terminal" ? "terminal"
                            : dragGhost.blockKind === "browser" ? "browser" : "file"
                        size: 12
                        color: Theme.color.accent
                    }

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        width: Math.max(0, dragGhost.width - 42)
                        text: dragGhost.blockTitle
                        elide: Text.ElideRight
                        color: Theme.color.textPrimary
                        font.family: "Microsoft YaHei UI"
                        font.pixelSize: Theme.font.caption
                        font.weight: Theme.font.weightMedium
                    }
                }
            }
        }

        // 下面 body 的高度也跟着只减 tabStrip.height。）

        Item {
            id: body
            width: parent.width

            // ⚠️ 夹 0 下界：**首帧/尺寸未定**时父链（右栏卡片的 Column）还没有高度，直接算
            // `parent.height - tabStrip.height - 1` 会得到负数 —— 真机日志里出现过
            // `PanelBarDock[0,9 260x-62]`（父链 vis=0）。负高度的 Item 本身不画东西，但它的子项
            // （启动器列、内容 Loader 都锚在它上面）会跟着变成负高；夹到 0 之后是"暂时没地方画
            // 内容"，可见行为与负高一致（都不画），所以这是纯防御，不改任何正常路径的尺寸。
            height: Math.max(0, parent.height - tabStrip.height)

            opacity: dock.contentT   // 出现/切换时淡入 + 轻微放大（顶栏那条块不动，只动内容区）
            scale: 0.985 + 0.015 * dock.contentT

            // 启动器（主界面）：目录也来自模型（三种块的中文名只有 C++ 那一份）
            // · 分区标题按设置页的分组头写（11px / textTertiary / 粗一档 + 一点字距）；
            // · 每项 = 28×28 的图标底 + 名称/说明 + 右侧 chevron（悬停才明显），
            // 悬停时卡片从 panelBg 抬到 hoverBg 并加一圈描边，命中范围一眼可见；
            // · 外面套 Flickable：扩展还能登记启动项（registerLauncher），
            // 条目多了会溢出面板，这里给滚动而不是让它压到卡片外。
            Flickable {
                id: launcherScroll

                anchors.fill: parent
                visible: dock.homeVisible
                contentWidth: width
                contentHeight: launcherColumn.height + 2 * Theme.spacing.columnClearance
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: AppScrollBar { }

                Column {
                    id: launcherColumn
                    x: Theme.spacing.columnClearance
                    y: Theme.spacing.columnClearance
                    width: Math.max(0, launcherScroll.width - 2 * Theme.spacing.columnClearance)
                    spacing: Theme.spacing.buttonGap

                    Text {
                        width: parent.width
                        text: "打开一个面板"
                        color: Theme.color.textTertiary
                        font.family: "Microsoft YaHei UI"
                        font.pixelSize: Theme.font.small
                        font.weight: Theme.font.weightBold
                        font.letterSpacing: 0.4
                        topPadding: 2
                        leftPadding: 2
                        bottomPadding: 2
                    }

                    Repeater {
                        model: PanelBarDock.launcherItems

                        delegate: Rectangle {
                            id: launcherRow
                            required property var modelData

                            // kind 决定图标：与块页签同一套映射，末位兜底给"扩展"（扩展登记的行）
                            readonly property string iconName: modelData.kind === "terminal" ? "terminal"
                                : modelData.kind === "browser" ? "browser"
                                : (modelData.kind === "files" ? "folder" : "extensions")

                            width: launcherColumn.width
                            height: 52
                            radius: Theme.radius.control
                            color: launcherMouse.containsMouse ? Theme.color.hoverBg : Theme.color.panelBg
                            border.width: 1
                            border.color: launcherMouse.containsMouse
                                ? Theme.color.inputBorder : Theme.color.menuBorder

                            Behavior on color { ColorAnimation { duration: Theme.motion.fast } }

                            Rectangle {   // 图标底：浅面小方块（设置页徽章那一档），把图标从"一行字里的符号"变成"一项的标识"
                                id: iconTile
                                anchors.left: parent.left
                                anchors.leftMargin: 8
                                anchors.verticalCenter: parent.verticalCenter
                                width: 28
                                height: 28
                                radius: Theme.radius.control
                                color: Theme.color.buttonBg

                                LineIcon {
                                    anchors.centerIn: parent
                                    name: launcherRow.iconName
                                    size: 15
                                    color: launcherMouse.containsMouse
                                        ? Theme.color.textPrimary : Theme.color.textSecondary
                                }
                            }

                            LineIcon {
                                id: hintChevron
                                anchors.right: parent.right
                                anchors.rightMargin: 10
                                anchors.verticalCenter: parent.verticalCenter
                                name: "chevron-right"
                                size: 12
                                color: Theme.color.textSecondary
                                opacity: launcherMouse.containsMouse ? 1 : 0
                                Behavior on opacity { NumberAnimation { duration: Theme.motion.fast } }
                            }

                            Column {
                                anchors.left: iconTile.right
                                anchors.leftMargin: 10
                                anchors.right: hintChevron.left
                                anchors.rightMargin: 8
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 2

                                Text {
                                    width: parent.width
                                    text: launcherRow.modelData.label
                                    color: Theme.color.textPrimary
                                    font.family: "Microsoft YaHei UI"
                                    font.pixelSize: Theme.font.body
                                    font.weight: Theme.font.weightMedium
                                    elide: Text.ElideRight
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
                                onClicked: PanelBarDock.open(launcherRow.modelData.kind)
                            }
                        }
                    }

                    // 页脚一句说明：右栏的块与会话是解绑的（切块/收右栏不会杀进程），
                    // 这句放这儿比藏在 tooltip 里更有效 —— 它是这一页最容易被误解的语义。
                    Text {
                        width: parent.width
                        topPadding: 6
                        leftPadding: 2
                        text: "面板会保留在右栏，可随时切换；关闭右栏不会结束终端会话。"
                        wrapMode: Text.WordWrap
                        color: Theme.color.textCaption
                        font.family: "Microsoft YaHei UI"
                        font.pixelSize: Theme.font.small
                        lineHeight: 1.3
                    }
                }
            }

            Loader {   // 当前块
                id: tabContent
                anchors.fill: parent
                visible: !dock.homeVisible

                // 内容路由：`kind → 视图` 的映射表在 C++（PanelBarDockModel::viewFor），这里只按 C++ 给的
                // 那组值装机 —— 路由里不再出现任何 kind 字符串（与启动器表、中文块名同一条分层）。
                // 活动块的那组值都绑在带通知的属性上，关掉活动块前面那块时会跟着重算，不会停在旧块上。
                sourceComponent: {
                    if (dock.homeVisible || PanelBarDock.activeIndex < 0)
                        return null
                    if (PanelBarDock.activeViewPath.length === 0)
                        return placeholderComp  // 没有视图：未接入 or 缺运行时依赖（文案由 C++ 给）
                    return PanelBarDock.activeViewBlockScoped ? blockScopedComp : plainComp
                }
            }

            Component {   // 纯 url 视图：路径来自 C++ 的视图表，这里只过风格包解析（<exe>/ui/ 同名文件优先于 qrc）
                id: plainComp
                Loader {
                    anchors.fill: parent
                    source: Style.url(PanelBarDock.activeViewPath)

                    // 扩展面板（kPanelBarDock 的 openUrl 写进块记录）的地址存在块记录里，这里**活绑**给面板
                    // 的 startUrl —— 与 blockScopedComp 把 blockId 绑给终端同一个手法：切到另一块同类
                    // 视图时 Loader 不重建，靠活绑定把新地址推下去。
                    // 用户自己开的浏览器块 URL 是空串 ⇒ openExternal 直接返回，行为与从前一致。
                    onLoaded: {
                        if (item && typeof item.openExternal === "function")
                            item.startUrl = Qt.binding(function () { return PanelBarDock.activeViewUrl })
                    }
                }
            }

            // 要跟着"当前块"走的视图（终端）：url 同样来自视图表，但面板还要拿到当前块的稳定 id。
            // ⚠️ 用 Qt.binding 而不是 setSource 的初始属性：切到另一个同类块时宿主**不重建** item
            // （契约与路径都没变），靠这条活绑定把新块推给面板（TerminalPanel.onBlockIdChanged
            // → 换绑会话）。初始属性只能给一次快照，切块就会串台。
            Component {
                id: blockScopedComp
                Loader {
                    anchors.fill: parent
                    source: Style.url(PanelBarDock.activeViewPath)

                    onLoaded: {
                        if (item)
                            item.blockId = Qt.binding(function () { return PanelBarDock.activeBlockId })
                    }
                }
            }

            Component {   // 没有视图的块：标题照常是块名，说明文案由 C++ 给（与中文块名同一层，QML 不拼）
                id: placeholderComp
                PanelPlaceholder {
                    anchors.fill: parent
                    title: PanelBarDock.activeTitle
                    detail: PanelBarDock.activeViewDetail
                }
            }
        }
    }



    // 拖块时的浮层必须是独立顶层窗口：右栏 body 有 layer+mask，
    // 把 ghost 留在栏内时一旦拖出侧栏就会被裁掉，看起来像“控件消失”。
    Window {
        id: dragGhostWindow
        transientParent: null
        flags: Qt.Tool | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.WindowDoesNotAcceptFocus
        color: "transparent"
        visible: false
        width: 0
        height: 34
        property string blockKind: ""
        property string blockTitle: ""

        Rectangle {
            anchors.fill: parent
            radius: Theme.radius.small
            color: Theme.color.panelBg
            border.width: 1
            border.color: Theme.color.inputBorder
            opacity: 0.97

            Row {
                anchors.centerIn: parent
                spacing: 5

                LineIcon {
                    anchors.verticalCenter: parent.verticalCenter
                    name: dragGhostWindow.blockKind === "terminal" ? "terminal"
                        : dragGhostWindow.blockKind === "browser" ? "browser" : "file"
                    size: 12
                    color: Theme.color.accent
                }

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    width: Math.max(0, dragGhostWindow.width - 42)
                    text: dragGhostWindow.blockTitle
                    elide: Text.ElideRight
                    color: Theme.color.textPrimary
                    font.family: "Microsoft YaHei UI"
                    font.pixelSize: Theme.font.caption
                    font.weight: Theme.font.weightMedium
                }
            }
        }
    }
}
