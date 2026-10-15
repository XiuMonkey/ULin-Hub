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
import QtQuick.Window
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

    // ---------------- 整个右栏脱离成独立窗口（本次的驱动路径）----------------
    // 状态在 Shell（它拥有独立窗口），这里只拿两条注入进来的事实：
    //   · sideDetached —— **确认脱离之后**为真（本 Item 此刻在独立窗口里，reparent 过去了，
    //     见 Shell.liftSidebarForDrag / finishSidebarDrag）。三个点于是改成"搬那扇窗 + 落回右栏位置就回附"；
    //   · attachZoneProvider —— 取"主窗口里右栏附着时所在的**屏幕矩形**"的函数。
    //     跨窗口判断只能用屏幕坐标（各自窗口的场景原点不是一个）。
    //     ⚠️ 用函数而不是 rect 属性：主窗口被搬动/改尺寸后属性绑定不会重算，拖动那一刻现算才是真的。
    property bool sideDetached: false
    property var attachZoneProvider: null

    // 抓手的四条请求，全部由 Shell 接住（它才知道窗口与壳层几何）：
    //   · sidebarLiftRequested —— 附着态下三点越过手感阈值：**现在**就把右栏"拿起来"
    //     （进无边框临时窗口跟手，需求 ①）。跟手期间主窗口布局一动不动（壳层保证）；
    //   · sidebarDropRequested —— 松手（壳层按落点决定回附还是独立）；
    //   · sidebarAttachRequested / sidebarPreviewChanged —— 独立态：拖回右栏 + 落点提示。
    signal sidebarLiftRequested()
    signal sidebarDropRequested(real screenX, real screenY)
    signal sidebarAttachRequested()
    // 拖动中的落点预览开关（拖动期间主窗口收不到 hover，只能由正在拖的抓手推过去）
    signal sidebarPreviewChanged(bool over)

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
                    // 三点**常驻**（需求 ①）：只要右栏这张卡片在显示，三个点就在。
                    // 上一版那条 `!Dock.homeVisible && Dock.activeIndex >= 0 && !activeDetached` 是
                    // "按块脱离"时代的可见性（坞里没块就没得拖）—— 现在拖的是**整个右栏**，
                    // 与坞里有没有活动块无关，所以整条去掉（`enabled: visible` 照旧）。
                    visible: root.visible
                    enabled: visible
                    // 尺寸用 PanelGrip 自己的缺省（4 / 间距 5，与 PageOverlay 那颗一致），这里**不赋值**：
                    // 曾经在这里赋 readonly 属性 ⇒ 整张右栏实例化失败 ⇒ 壳层什么都画不出来（空白窗口）。
                    // 要改大小就改 widgets/PanelGrip.qml 的缺省（那是这个控件的规格，只有一份）。
                    // ⚠️ 这里刻意**不**登记进原生命中测试（nativeHost 保持缺省 false）：
                    //    原生只在 `m_captionItem`（自绘标题栏，Main.qml 的 WindowBridge.setCaptionItem）
                    //    的矩形里才判 HTCAPTION，而右栏标题行在 caption 矩形之下、收到的本来就是
                    //    普通客户端事件（同一个 header 里的 ✕ 按钮一直就是这么收点击的）。
                    //    在无边框窗口里登记是为了抢在标题栏之前，这里没有要抢的东西。

                    // 阈值（沿用搬家前的数值）：附着态只留"手感阈值"（PanelGrip 的 6px，越过就
                    // **立刻拿起来**跟手，见下）；独立态"光标落回右栏原位置 ±24px ⇒ 够回附"。
                    readonly property real attachSlack: 24
                    // 已经越过回附阈值（独立态拖动中实时算，松手时按它决定做什么）
                    property bool armed: false
                    // 这一次按下之后真的拖动过吗？⚠️ 不能看 PanelGrip.dragging ——
                    // 它发 dragFinished 之前**先把 dragging 置 false**，这边读到的一定是 false。
                    property bool dragged: false
                    // 这一次按压是否已经"交接"给壳层（附着态越过阈值 ⇒ 右栏进临时窗口跟手）。
                    // ⚠️ 交接那一瞬间 reparent 会让本 MouseArea **立刻**收到 cancel（item 换窗口时
                    //    Qt 必然 un-grab），那次 cancel 不是松手 —— 不能让它去定结局（否则松手前
                    //    就把面板定成回附/独立了）。所以交接后把 finished/cancel 一律丢掉。
                    property bool handedOff: false
                    // 独立态搬窗口：按下那一刻窗口在哪（后面用"它 + 屏幕位移"1:1 跟手）
                    property real pressWinX: 0
                    property real pressWinY: 0

                    // 「右栏附着时所在的屏幕矩形」：回附方向的落点判定基准。
                    // 由 Shell 注入（它才知道主窗口与壳层几何）；**现算**而不是缓存成属性 ——
                    // 主窗口被搬动/改尺寸后属性绑定不会重算（那是 C++ 侧改的窗口坐标）。
                    // ⚠️ 这里必须用 root（本文件根 id）：上一轮从 PanelDock.qml 抄过来时写的是 `dock`，
                    //    未定义 ⇒ 整张 RightSidebar 编译失败 ⇒ Shell 报 "Type RightSidebar unavailable"，
                    //    表现是**整个窗口空白**（踩过一次）。
                    function attachZone() {
                        if (!root.attachZoneProvider)
                            return null
                        return root.attachZoneProvider()
                    }
                    function insideAttachZone(gp) {
                        const r = attachZone()
                        if (!r)
                            return false
                        const s = attachSlack
                        return gp.x >= r.x - s && gp.x <= r.x + r.width + s
                               && gp.y >= r.y - s && gp.y <= r.y + r.height + s
                    }

                    onPressStarted: {
                        armed = false
                        dragged = false
                        handedOff = false
                        const win = root.Window.window
                        pressWinX = win ? win.x : 0
                        pressWinY = win ? win.y : 0
                    }

                    onDragMoved: (dx, dy, globalPos, scenePos) => {
                        // 没过 PanelGrip 的手感阈值（6px）之前什么都不做：那只是"点了一下"
                        if (!dockGrip.dragging)
                            return
                        dragged = true

                        if (root.sideDetached) {
                            // 独立态：三个点**照样能拖**（需求要保留）—— 拖的就是那扇独立窗口。
                            // 位移用**屏幕坐标差**加在按下时的窗口位置上：窗口自己在动，
                            // 拿局部坐标算会自己追自己。
                            const win = root.Window.window
                            if (!win)
                                return
                            win.x = Math.round(pressWinX + dx)
                            win.y = Math.round(pressWinY + dy)
                            // 光标落回右栏原位置附近 ⇒ 亮起落点提示（松手才真的回附）
                            const over = insideAttachZone(globalPos)
                            if (over !== armed) {
                                armed = over
                                root.sidebarPreviewChanged(over)
                            }
                            return
                        }

                        // 附着态：越过手感阈值 ⇒ **立刻拿起来**（需求 ①）。
                        // 之后这扇窗跟着光标 1:1 走、主窗口布局一动不动，松手才定结局；
                        // 位置由壳层按"屏幕坐标差"跟（见 Shell.sidebarFollowTick）。
                        if (!handedOff) {
                            handedOff = true
                            root.sidebarLiftRequested()
                        }
                    }
                    onDragFinished: (dx, dy, globalPos) => {
                        const wasArmed = armed
                        armed = false
                        // 预览一律先收（松手之后就没有"正在拖"这件事了）
                        root.sidebarPreviewChanged(false)
                        const wasDragged = dragged
                        dragged = false
                        if (handedOff) {
                            // 已经交接给壳层：这次 finished/cancel（reparent 引起的那次）不作数，
                            // 收尾由壳层的跟手 Timer（物理按键状态）负责。
                            handedOff = false
                            return
                        }
                        if (!wasDragged)      // 只点击、没拖动：不动任何状态
                            return
                        if (root.sideDetached) {
                            if (wasArmed)
                                root.sidebarAttachRequested()   // 落回右栏位置 ⇒ 重新附着
                        } else {
                            // 附着态没交接就松手 = 没过手感阈值（点了一下）⇒ 什么都不做。
                            // 这里仍然报一次"松手"，壳层按落点决定（它自己会判"没在跟手"直接返回），
                            // 免得日后有人把阈值调大后忘了这条线。
                            root.sidebarDropRequested(globalPos.x, globalPos.y)
                        }
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
