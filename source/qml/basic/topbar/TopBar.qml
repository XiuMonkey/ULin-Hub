// 顶栏：左侧当前会话标题，右侧工具过滤入口。
// 面板本体在 basic/chat/ToolsFilterPanel.qml（由 Shell.qml 按 Hub.toolsFilterOpen 装载），
// 数据与形状全在 C++ 的 common/session/ToolsFilter 里 —— 这里只负责"点一下"。

import QtQuick
import ULinHub 1.0
import "../../widgets"

Item {
    id: topBar

    // 扩展注入点：扩展用 HostUi.item("topBar") 拿到本对象后 setParentItem 进来（见 HostExports.h 的名字契约）
    Component.onCompleted: HostUi.registerItem("topBar", this)

    // ⚠️ 保留这个信号与 `qml.topbar.toolsFilter` 登记项：宿主侧栏/扩展里还有人用"请求打开工具过滤"
    //    这条通用入口（自己直接开面板就成了）；改名的代价大于收益。
    signal toolsFilterRequested()
    // 右侧栏开合（面板本体在 basic/shell/RightSidebar.qml，由 Shell.qml 装载；这里只发"点了一下"）
    signal rightSidebarRequested()

    implicitHeight: 44

    // 背景与圆角由外层 Card 提供（Shell.qml）

    Text {
        id: titleText
        anchors.left: parent.left
        anchors.leftMargin: Theme.spacing.columnClearance
        anchors.right: rightSidebarButton.left
        anchors.rightMargin: Theme.spacing.columnClearance
        anchors.verticalCenter: parent.verticalCenter
        text: Hub.currentSessionTitle.length > 0 ? Hub.currentSessionTitle : "ULin Hub"
        color: Theme.color.textPrimary
        font.family: "Microsoft YaHei UI"
        font.pixelSize: Theme.font.title
        font.weight: Theme.font.weightMedium
        elide: Text.ElideRight
    }

    // 右侧栏开关：开合状态直接读 Hub.rightSidebarOpen（面板开着时高亮，当开关的手感）
    IconButton {
        id: rightSidebarButton
        anchors.right: toolsButton.left
        anchors.rightMargin: Theme.spacing.buttonGap
        anchors.verticalCenter: parent.verticalCenter
        glyph: "◫"
        tooltip: "右侧栏"      // TODO(i18n)：新文案 id 还没进 .ts，先留字面量
        selected: Hub.rightSidebarOpen
        onClicked: topBar.rightSidebarRequested()
    }

    IconButton {
        id: toolsButton
        anchors.right: parent.right
        anchors.rightMargin: Theme.spacing.columnClearance
        anchors.verticalCenter: parent.verticalCenter
        glyph: "☰"
        tooltip: qsTrId("toolfilter_title")
        // 面板开着时高亮这颗按钮（当开关用的手感）
        selected: Hub.toolsFilterOpen
        // ⚠️ 只发信号，**不要**在这里同时直调 Hub.openToolsFilter()：
        //    信号在 Shell.qml 里已经接到 Hub.openToolsFilter() 上了，两处都调 = 一次点击开一次又收一次，
        //    表现就是"点了没反应"（实测日志：同一次点击出现两条 openToolsFilter: open=false / open=true）。
        onClicked: topBar.toolsFilterRequested()
    }
}
