// 壳层布局：卡片式（与旧壳一致）——
//   左边一张侧栏卡片，右边一列：顶栏卡片 + 会话卡片；卡片之间留 panelGap、与窗口之间留 windowMargin。
// 旧壳的对应关系：`#sidebar`/`#topBar` 的 radius 12 + `ShadowPanel` 的 CardShadow{8,2,12}；
// 这里由 components/Card.qml 的 MultiEffect 阴影承担，尺寸/颜色全取 token。
// 这一层只做装配与遮罩/Toast 宿主，具体内容都在被 Style.url() 装载的单元里（可被风格包覆盖）。

import QtQuick
import ULinHub 1.0
import "../../widgets"
// ui/shell 与 ui/sidebar、ui/topbar 是并列目录：QML 只按同目录/已 import 的目录解析组件名，
// 所以跨目录用裸组件名（Sidebar / TopBar）必须显式 import
import "../sidebar"
import "../topbar"

Item {
    id: shell

    // 扩展注入点：扩展用 HostUi.item("shell") 拿到本对象后 setParentItem 进来（见 HostExports.h 的名字契约）
    Component.onCompleted: HostUi.registerItem("shell", this)

    // 供子界面报一句轻提示（未落地的入口用它，别放哑按钮）
    function notify(text) {
        toast.show(text)
    }

    // ---- 左侧：侧栏卡片 ----
    Card {
        id: sidebarCard
        anchors.left: parent.left
        anchors.leftMargin: Theme.spacing.windowMargin
        anchors.top: parent.top
        anchors.topMargin: Theme.spacing.contentSideInset
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Theme.spacing.contentSideInset
        width: Theme.size.sidebarWidth
        radius: Theme.radius.panel
        padding: 0
        shadowEnabled: true
        // 与旧壳对齐：sidebar.qss 的 `#sidebar` 只有 background + border-radius，**没有描边**；
        // 只有 `#topBar` 有 `border: 1px solid {{border}}`。少画这一条，壁纸皮肤下才不会多出一圈线。
        bordered: false

        Sidebar {
            anchors.fill: parent
            onNotify: shell.notify(text)
        }
    }

    // ---- 右侧：顶栏卡片 + 会话卡片 ----
    Item {
        id: rightColumn
        anchors.left: sidebarCard.right
        anchors.leftMargin: Theme.spacing.panelGap
        anchors.right: parent.right
        anchors.rightMargin: Theme.spacing.windowMargin
        anchors.top: parent.top
        anchors.topMargin: Theme.spacing.contentSideInset
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Theme.spacing.contentSideInset

        Card {
            id: topBarCard
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            implicitHeight: topBar.implicitHeight
            radius: Theme.radius.panel
            padding: 0
            shadowEnabled: true

            TopBar {
                id: topBar
                anchors.fill: parent
                onToolsFilterRequested: Hub.openToolsFilter()
            }
        }

        // 会话区：**不要圆角卡片背景**（圆角只给消息自己的气泡），所以这里用普通 Item 而非 Card
        Item {
            id: conversationCard
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: topBarCard.bottom
            // 与旧壳同值：顶栏卡片与会话卡片之间由阴影留白 + 这个间距共同决定
            anchors.topMargin: Theme.shadow.topBarGap
            anchors.bottom: parent.bottom

            // 选中会话时是消息列表 + 输入区，没选会话时给引导
            Loader {
                id: composerLoader
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.leftMargin: Theme.spacing.columnClearance
                anchors.rightMargin: Theme.spacing.columnClearance
                anchors.bottomMargin: Theme.spacing.columnClearance
                height: item ? item.implicitHeight : 0
                visible: Hub.currentSessionId.length > 0
                source: Style.url("basic/chat/Composer.qml")
            }

            // 提问 / 审批面板（M4b）：排在输入区上方。
            // 旧壳是把它插进消息列表里（MessageHost::handleMuxFrame → InteractionHandler 建面板），
            // QML 侧放在输入区上方更稳：不用改消息模型的角色，也不会打乱列表的滚动与高度。
            Loader {
                id: interactionLoader
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: composerLoader.top
                anchors.bottomMargin: Theme.spacing.columnClearance
                anchors.leftMargin: Theme.spacing.columnClearance
                anchors.rightMargin: Theme.spacing.columnClearance
                height: item ? item.implicitHeight : 0
                active: Hub.questionActive || Hub.approvalActive
                source: Style.url("basic/chat/InteractionPanel.qml")
            }

            Loader {
                id: messageLoader
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.bottom: interactionLoader.top
                anchors.bottomMargin: Theme.spacing.labelGap
                visible: Hub.currentSessionId.length > 0
                // ⚠️ 必须等宽度定下来再创建列表：首帧宽度≈0 时委托里的 Text 会按假宽度排版
                //    （行数翻几倍），量出的 contentHeight 是最终的 ~3 倍 → 滑块长度先长后短，
                //    表现就是"一启动滚动条乱跳、然后稳定"（实测 contentH 2400 → 903）。
                active: width > 200
                source: Style.url("basic/chat/MessageListView.qml")
            }

            ConversationPlaceholder {
                anchors.fill: parent
                visible: Hub.currentSessionId.length === 0
            }
        }
    }

    // 服务端还没连上时的遮罩：不挡住操作，只提示状态
    Rectangle {
        anchors.fill: parent
        visible: Hub.initializing
        color: Theme.color.overlayBg
        z: 10

        Column {
            anchors.centerIn: parent
            spacing: Theme.spacing.sectionGap

            BusySpinner {
                anchors.horizontalCenter: parent.horizontalCenter
                width: 32
                height: 32
                color: Theme.color.textOnAccent
                // ⚠️ 必须门控：BusySpinner.running 默认 true，而 RotationAnimator 是无限循环。
                // 只靠父级 visible 是不够的 —— 不可见的 item 上的动画照样每帧改属性、
                // 把场景标脏，于是应用**空闲时也在满速重绘**（实测：空闲 5.4% 单核 + GPU 3D 25~29%，
                // 换成壁纸皮肤后每帧还要多跑两个全窗离屏层，移动窗口就卡）。
                // Sidebar / PluginMarketPage 里的同类转圈都传了 running，这里漏了。
                running: Hub.initializing
                visible: Hub.initializing
            }

            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: Hub.statusText
                color: Theme.color.textOnAccent
                font.family: "Microsoft YaHei UI"
                font.pixelSize: Theme.font.body
            }
        }
    }

    Toast {
        id: toast
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 3 * Theme.spacing.cardPadding
        z: 20
    }

    // 插件市场（M6）：覆盖整壳，侧栏 ◧ 打开。
    // 三块整壳覆盖页统一走 PageOverlay —— 遮罩 + 从**点击处**长出来的卡片 + 收回播完才卸载，
    // 于是"点侧栏那颗图标"与"卡片从哪儿长出来"天然对得上（页面自己不用接线）。
    PageOverlay {
        anchors.fill: parent
        z: 35
        shown: Hub.plugins.open
        source: Style.url("basic/plugins/PluginMarketPage.qml")
    }

    // 扩展管理：覆盖整壳，侧栏扩展按钮打开（工具 / 客户端 / Qt Quick 运行时三种来源）
    PageOverlay {
        anchors.fill: parent
        z: 35
        shown: Hub.extensions.open
        source: Style.url("basic/extensions/ExtensionManagerPage.qml")
    }

    // 主题切换过渡卡片（原版 #themeSwitchBody）
    Loader {
        anchors.fill: parent
        active: Hub.themeSwitching
        visible: Hub.themeSwitching
        source: Style.url("skin/shell/ThemeSwitchOverlay.qml")
        z: 40
    }

    // 设置页（模型 / API key）：覆盖整壳，侧栏 ⚙ 打开
    PageOverlay {
        anchors.fill: parent
        z: 30
        shown: Hub.settings.open
        source: Style.url("basic/settings/SettingsPage.qml")
    }

    // 工具过滤面板：顶栏 ☰ 打开，覆盖整壳。
    // 为什么用"整壳覆盖 + 居中卡片"而不是 Popup：与 PluginTranspiler 扩展面板同一套做法，
    // 不吃 QtQuick.Controls 的 Overlay，行为可预测；数据/形状全在 C++（common/session/ToolsFilter）。
    Loader {
        id: toolsFilterLoader

        anchors.fill: parent
        active: Hub.toolsFilterOpen
        visible: Hub.toolsFilterOpen
        source: Style.url("basic/chat/ToolsFilterPanel.qml")
        z: 36

        // 面板自己读 Hub.toolsFilter（import ULinHub 1.0），这里只转它的关闭请求
        onLoaded: item.closeRequested.connect(Hub.closeToolsFilter)
    }
}
