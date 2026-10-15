// 右侧栏（面板坞）：顶栏那颗 ◫ 开关；展开时把「顶栏卡片 + 会话卡片」一起压窄。
//
// 为什么"压窄"由 Shell 驱动、面板自己只做位移 + 淡入（见 Shell.qml 的 rightInset）：
//   · 压窄是**布局**（顶栏与会话区必须一起变窄），只有父层能推动；
//   · 面板是固定宽的卡片，滑动期间若改自己的 width，里面的文字每帧都要重排（白花 CPU 且会闪），
//     只改 x/opacity 的话每帧就一次平移。
//
// 内容用默认属性注入：`RightSidebar { 你的面板 {} }`；没注入时显示占位文案。
// 后续要挂的东西（见 build/temp/dsh-official/GAP-REPORT.md）：终端 / 工作区文件 / 文档预览 / 轨迹。

import QtQuick
import ULinHub 1.0
import "../../widgets"

Item {
    id: root

    // 内容注入点：外部子项落到 body 里（不是落到卡片根上）
    default property alias contentData: body.data
    // 头部标题
    property string title: qsTrId("right_sidebar_title")
    // 没有注入内容时的占位
    property string emptyHint: qsTrId("right_sidebar_empty_hint")
    // 关闭请求：Shell 接到 Hub.closeRightSidebar()
    signal closeRequested()

    // 当前活动块是否**已经脱离**成独立窗口：抓手的可见性要用（与 PanelDock.qml 里那条判定同一套，
    // 读的是模型 Dock.detachedBlocks）。脱离中的块在坞里没有内容可拖 ⇒ 三个点藏起来。
    readonly property bool activeDetached: {
        const list = Dock.detachedBlocks
        for (let i = 0; i < list.length; ++i) {
            if (list[i].blockId === Dock.activeBlockId)
                return true
        }
        return false
    }

    Card {
        anchors.fill: parent
        radius: Theme.radius.panel
        padding: 0
        shadowEnabled: true

        Column {
            anchors.fill: parent
            spacing: 0

            // ---- 头部：标题 + 关闭 ----
            Item {
                id: header
                width: parent.width
                height: 44          // 与 TopBar 同高，两条卡片看起来是一套

                Text {
                    id: titleText
                    anchors.left: parent.left
                    anchors.leftMargin: Theme.spacing.columnClearance
                    anchors.right: closeButton.left
                    anchors.rightMargin: Theme.spacing.columnClearance
                    anchors.verticalCenter: parent.verticalCenter
                    text: root.title
                    color: Theme.color.textPrimary
                    font.family: "Microsoft YaHei UI"
                    font.pixelSize: Theme.font.title
                    font.weight: Theme.font.weightMedium
                    elide: Text.ElideRight
                }

                // 面板的「三个点」抓手：从 PanelDock.qml 的页签条搬到这里（需求 ②）。
                //
                // 为什么搬：原来它锚在页签条 `⌂` 右边 6px，而页签条 Flickable 锚在 `⌂` 右边 4px、
                // **声明在后面 ⇒ 画在上层**，把只有 ~30px 宽的三点整个盖住了（用户看不见三点）。
                // 标题目右边的空地**没有别的子项**，天然不会再被谁盖住。
                //
                // 位置取**标题行正中间**（用户两次反馈的落点：
                //   ① "还是在最右边而不是中间" ⇒ 先改成"标题右缘 ↔ ✕ 左缘"的中点；
                //   ② "还是不够中间啊，要在正中间" ⇒ 就是整条 header 的水平中线，不再相对左右元素取中点）。
                // 所以用 `anchors.horizontalCenter: header.horizontalCenter`（锚在头部 Item 自己身上，
                // 不掺任何兄弟元素的宽度）；`x` 不再手算 —— 两套定位同时留会互相打架。
                // ⚠️ 不要退回"贴着标题右边"：那正是用户否掉的位置（看着像贴着 ✕ 那一侧）。
                PanelGrip {
                    id: dockGrip
                    anchors.horizontalCenter: header.horizontalCenter
                    anchors.verticalCenter: parent.verticalCenter
                    // 与搬家前**同一条**可见性语义：只在"有活动块且该块没脱离"时显示
                    // （不在主界面 / 没有块时不留哑控件）
                    visible: !Dock.homeVisible && Dock.activeIndex >= 0 && !activeDetached
                    enabled: visible
                    // 尺寸用 PanelGrip 自己的缺省（4 / 间距 5，与 PageOverlay 那颗一致），这里**不赋值**：
                    // 曾经在这里赋 readonly 属性 ⇒ 整张右栏实例化失败 ⇒ 壳层什么都画不出来（空白窗口）。
                    // 要改大小就改 widgets/PanelGrip.qml 的缺省（那是这个控件的规格，只有一份）。
                    // ⚠️ 这里刻意**不**登记进原生命中测试（nativeHost 保持缺省 false）：
                    //    原生只在 `m_captionItem`（自绘标题栏，Main.qml 的 WindowBridge.setCaptionItem）
                    //    的矩形里才判 HTCAPTION，而右栏标题行在 caption 矩形之下、收到的本来就是
                    //    普通客户端事件（同一个 header 里的 ✕ 按钮一直就是这么收点击的）。
                    //    在无边框窗口里登记是为了抢在标题栏之前，这里没有要抢的东西。

                    // 离开右栏区域 >48px，或水平位移 >60px ⇒ 够脱离（与搬家前逐字同一条阈值）
                    readonly property real detachSlack: 48
                    readonly property real detachShiftX: 60
                    property bool detachArmed: false

                    // 右栏在**屏幕坐标**里的矩形：跨窗口判断只能用屏幕坐标（各自窗口的场景原点不是一个）
                    // ⚠️ 这里必须用 root（本文件根 id）：这段是从 PanelDock.qml 搬过来的，那边写的是 `dock`，
                    //    照抄过来 `dock` 就是未定义 ⇒ 整张 RightSidebar 编译失败 ⇒ Shell 报
                    //    "Type RightSidebar unavailable"，表现是**整个窗口空白**（踩过一次）。
                    function dockRectGlobal() {
                        const tl = root.mapToGlobal(0, 0)
                        return Qt.rect(tl.x, tl.y, root.width, root.height)
                    }

                    onDragMoved: (dx, dy, globalPos, scenePos) => {
                        const r = dockRectGlobal()
                        const outside = globalPos.x < r.x - detachSlack
                                        || globalPos.x > r.x + r.width + detachSlack
                                        || globalPos.y < r.y - detachSlack
                                        || globalPos.y > r.y + r.height + detachSlack
                        const armed = outside || Math.abs(dx) > detachShiftX
                        if (armed !== detachArmed) {
                            detachArmed = armed
                            Dock.setDetachPreview(armed)
                        }
                    }
                    onDragFinished: (dx, dy, globalPos) => {
                        const armed = detachArmed
                        detachArmed = false
                        // 预览一律先收（与搬家前一致：预览开着的时候模型可能已经不认这个块了）
                        Dock.setDetachPreview(false)
                        if (armed)
                            Dock.requestDetach(Dock.activeBlockId, globalPos.x, globalPos.y)
                    }
                }

                IconButton {
                    id: closeButton
                    anchors.right: parent.right
                    anchors.rightMargin: Theme.spacing.columnClearance
                    anchors.verticalCenter: parent.verticalCenter
                    glyph: "✕"
                    tooltip: qsTrId("common_close")
                    onClicked: root.closeRequested()
                }
            }

            // ---- 内容区：注入点 + 空态 ----
            Item {
                id: bodyArea
                width: parent.width
                height: parent.height - header.height

                // 注入点自己是空的：占位文案靠 body.children.length 判断（所以占位不能放进 body）
                Item {
                    id: body
                    anchors.fill: parent
                }

                Text {
                    anchors.centerIn: parent
                    width: parent.width - 2 * Theme.spacing.columnClearance
                    visible: body.children.length === 0
                    text: root.emptyHint
                    color: Theme.color.textTertiary
                    font.family: "Microsoft YaHei UI"
                    font.pixelSize: Theme.font.caption
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    lineHeight: 1.4
                }
            }
        }
    }
}
