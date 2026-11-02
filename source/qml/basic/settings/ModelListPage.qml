// 设置 · 模型列表（按原版 ui/ModelListPanel.cpp + styles/model-list.qss 复刻）。
// 结构自上而下：标题/说明 → 状态行 → 瞬时提示（4 秒自隐）→ 滚动区（分组标题 + 成员卡 + 空态 + 新增表单）
// → 「添加模型」按钮在滚动区**外**（表单在滚动区内，面板高度才不随表单开合变化）。
// 要点：成员卡无列/无选中态，表头最小高 48、右侧 ▸/▾，徽章按 userDeclared/declared 决定；
// 删除走右键菜单（common_delete）不是行内按钮；表单提交先（可选）写凭据，失败整次中止，再整份写回 models。

import QtQuick
import QtQuick.Controls.Basic
import ULinHub 1.0
import "../../widgets"
import "../../skin/settings"
import "../../skin/chat"

Item {
    id: page

    property var expandedKeys: ({})   // 展开过的成员 key（刷新后按它恢复展开态，与原版 m_expanded 同思路）
    property bool formOpen: false
    property bool candidatesVisible: false
    readonly property int fieldHeight: 30
    readonly property int labelWidth: 96
    readonly property int detailLabelWidth: 96

    readonly property var currentProvider: {   // 当前表单选中的路由
        const rows = Hub.settings.providers
        if (rows.length === 0)
            return null
        const index = providerCombo.currentIndex
        return rows[index >= 0 && index < rows.length ? index : 0]
    }

    function keyOf(row) {
        return row.provider + "\u001f" + row.id
    }

    function isExpanded(row) {
        return page.expandedKeys[page.keyOf(row)] === true
    }

    // 模型行只带 settings 侧信息（settingsNs / settingsWritable）；凭据状态挂在 provider 行上，
    // 按 provider 路由回查一行 —— 同一路由下的所有模型共用一条凭据引用（如
    // deepseek-official → DEEPSEEK_API_KEY），所以 key 是**路由级**的，不是模型级的。
    function providerRowFor(providerId) {
        const rows = Hub.settings.providers
        for (let i = 0; i < rows.length; ++i) {
            if (rows[i].provider === providerId)
                return rows[i]
        }
        return null
    }

    function keyReadonly(providerId) {   // 只读 = 服务端已确认这条引用由 profile / 环境提供，写不进去
        const row = page.providerRowFor(providerId)
        return row !== null && row.keyKnown && !row.keyWritable
    }

    function toggleRow(row) {
        const key = page.keyOf(row)
        const next = Object.assign({}, page.expandedKeys)
        if (next[key])
            delete next[key]
        else
            next[key] = true
        page.expandedKeys = next
    }

    function statusText() {   // 状态行文案（原版 updateStatus）
        if (!Hub.settings.busy && Hub.serverState !== "running" && Hub.settings.models.length === 0)
            return qsTrId("model_server_not_ready")

        let text = ""
        if (Hub.settings.busy && Hub.settings.models.length === 0) {
            text = qsTrId("model_loading_catalog")
        }
        else {
            text = qsTrId("model_catalog_summary_fmt")
                .arg(Hub.settings.models.length)
                .arg(Hub.settings.groupCount)
            if (!Hub.settings.writable)
                text += " " + qsTrId("model_add_readonly")
            for (let i = 0; i < Hub.settings.failures.length; ++i) {
                const failure = Hub.settings.failures[i]
                text += " " + qsTrId("model_provider_load_failed_fmt").arg(failure.name).arg(failure.message)
            }
        }
        if (Hub.settings.status.length > 0)
            text += " " + Hub.settings.status
        return text
    }

    function credentialText(row) {   // 凭据引用行：基础句 + 状态后缀（原版 refreshCredentialRowText）
        if (!row || !row.keyRef || row.keyRef.length === 0)
            return ""

        let text = row.keyDeclaredByProfile
            ? qsTrId("model_credential_from_profile_fmt").arg(row.keyRef)
            : qsTrId("model_credential_derived_fmt").arg(row.keyRef)

        if (!row.keyKnown)
            text += qsTrId("model_credential_unknown_suffix")
        else if (!row.keyWritable)
            text += qsTrId("model_credential_readonly_suffix")
        else if (row.keyConfigured) {
            text += qsTrId("model_credential_configured_fmt").arg(
                row.keySource && row.keySource.length > 0
                    ? qsTrId("model_credential_source_fmt").arg(row.keySource)
                    : "")
        }
        else {
            text += qsTrId("model_credential_unconfigured_suffix")
        }
        return text
    }

    function reasoningText(row) {   // 详情行：值为空时不显示（原版 addDetailRow）
        if (row.reasoningDisabled)
            return qsTrId("model_think_not_offered")
        if (row.hasReasoning)
            return row.levels.join(" / ")
        const fallback = ["off", "low", "high", "max"]
        return qsTrId("model_think_undeclared_fmt").arg(fallback.join("/"))
    }

    function configSourceText(row) {
        if (row.userDeclared)
            return qsTrId("model_source_settings_user")
        if (row.declared)
            return qsTrId("model_source_settings_bundled")
        return qsTrId("model_source_adapter_only")
    }

    function writebackText(row) {
        if (!row.settingsNs || row.settingsNs.length === 0)
            return ""
        let text = row.settingsNs + " · models"
        if (!row.settingsWritable)
            text += qsTrId("model_settings_readonly_suffix")
        return text
    }

    Column {
        id: root
        anchors.fill: parent
        spacing: 3

        Item {   // 标题行：只放标题，新增动作统一收在底部
            id: titleRow
            width: parent.width
            implicitHeight: titleLabel.implicitHeight

            Text {
                id: titleLabel
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                text: qsTrId("model_list_title")
                color: Theme.color.textPrimary
                font.family: "Microsoft YaHei UI"
                font.pixelSize: Theme.font.title
                font.weight: Theme.font.weightBold
            }
        }

        Text {
            width: parent.width
            text: qsTrId("model_list_desc")
            wrapMode: Text.WordWrap
            color: Theme.color.textSecondary
            font.family: "Microsoft YaHei UI"
            font.pixelSize: Theme.font.caption
        }

        Text {
            width: parent.width
            text: page.statusText()
            wrapMode: Text.WordWrap
            color: Theme.color.textSecondary
            font.family: "Microsoft YaHei UI"
            font.pixelSize: Theme.font.caption
        }

        Text {
            width: parent.width
            visible: Hub.settings.notice.length > 0
            text: Hub.settings.notice
            wrapMode: Text.WordWrap
            color: Theme.color.textSecondary   // 瞬时提示：原版走强调色（蓝），设置页不出蓝 ⇒ 与状态行同档的中性灰
            font.family: "Microsoft YaHei UI"
            font.pixelSize: Theme.font.caption
        }

        Flickable {
            id: scroller
            width: parent.width
            height: Math.max(120, root.height - scroller.y - actionFooter.height - 4)
            contentHeight: listColumn.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: AppScrollBar { }

            Column {
                id: listColumn
                width: scroller.width
                spacing: 4

                Repeater {
                    model: Hub.settings.models

                    delegate: Column {
                        id: rowHost

                        required property var modelData
                        required property int index

                        readonly property var providerRow: page.providerRowFor(modelData.provider)   // 该模型所属 provider 的那一行（凭据状态都在那边）

                        width: listColumn.width
                        spacing: 2

                        // 分组标题：provider 与上一行不同时插入（原版 modelListGroup）
                        // 分组标题：压到 textTertiary + 一点字距，读起来是"分区"而不是"正文"
                        Text {
                            width: parent.width
                            visible: rowHost.index === 0
                                || Hub.settings.models[rowHost.index - 1].provider !== rowHost.modelData.provider
                            text: rowHost.modelData.providerName
                            color: Theme.color.textTertiary
                            font.family: "Microsoft YaHei UI"
                            font.pixelSize: Theme.font.small
                            font.weight: Theme.font.weightBold
                            font.letterSpacing: 0.4
                            topPadding: 4
                            leftPadding: 2
                        }

                        Rectangle {   // 成员卡（原版 #modelListEntry：panelBg + 1px border + radius 10）
                            id: entry
                            width: parent.width
                            implicitHeight: entryColumn.implicitHeight
                            radius: Theme.radius.card
                            color: Theme.color.panelBg
                            border.width: 1

                            // 原描边是 border(4% 黑)，和白底几乎同色 —— 卡片边界只能靠 4px 间距"猜"。
                            // 提到 menuBorder(6%)，展开时 inputBorder(10%)：一屏四张才分得清。
                            border.color: page.isExpanded(rowHost.modelData)
                                ? Theme.color.inputBorder
                                : Theme.color.menuBorder

                            Column {
                                id: entryColumn
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.top: parent.top

                                Item {   // 表头（原版最小高 48、内边距 (10,3,10,3)）
                                    id: header
                                    width: parent.width
                                    implicitHeight: Math.max(48, headerRow.implicitHeight + 6)

                                    Rectangle {
                                        anchors.fill: parent
                                        radius: Theme.radius.inner
                                        color: headerMouse.containsMouse ? Theme.color.hoverBg : "transparent"
                                    }

                                    Row {
                                        id: headerRow
                                        anchors.left: parent.left
                                        anchors.right: parent.right
                                        anchors.leftMargin: 10
                                        anchors.rightMargin: 10
                                        anchors.verticalCenter: parent.verticalCenter
                                        spacing: 8

                                        Column {
                                            width: parent.width - badge.width - chevron.width - 2 * parent.spacing
                                            spacing: 0

                                            Text {
                                                width: parent.width
                                                text: rowHost.modelData.name
                                                elide: Text.ElideRight
                                                color: Theme.color.textPrimary
                                                font.family: "Microsoft YaHei UI"
                                                // 行标题取 body(13)：原来和页面标题同为 14，
                                                // 一屏里两个"14"抢层级，模型名反而比"模型列表"更显眼。
                                                font.pixelSize: Theme.font.body
                                                font.weight: Theme.font.weightMedium
                                            }

                                            Text {
                                                width: parent.width
                                                text: rowHost.modelData.providerName + " · " + rowHost.modelData.id
                                                elide: Text.ElideRight
                                                color: Theme.color.textTertiary
                                                font.family: "Microsoft YaHei UI"
                                                font.pixelSize: Theme.font.caption
                                            }

                                            // 这条路由还没配凭据时给个入口提示：否则"已接上的模型在哪填 key"
                                            // 在界面上没有任何线索。前面的 5px 琥珀点是全页唯一的饱和度来源，
                                            // 只出现在需要动手的那一行。
                                            Row {
                                                width: parent.width
                                                visible: rowHost.providerRow !== null
                                                    && rowHost.providerRow.keyKnown
                                                    && !rowHost.providerRow.keyConfigured
                                                topPadding: 1
                                                spacing: 5

                                                Rectangle {
                                                    width: 5
                                                    height: 5
                                                    radius: 2.5
                                                    anchors.verticalCenter: parent.verticalCenter
                                                    color: "#F59E0B"
                                                }

                                                Text {
                                                    width: parent.width - 10
                                                    text: qsTrId("model_key_missing")
                                                    elide: Text.ElideRight
                                                    color: Theme.color.textTertiary
                                                    font.family: "Microsoft YaHei UI"
                                                    font.pixelSize: Theme.font.small
                                                }
                                            }
                                        }

                                        Rectangle {
                                            id: badge
                                            visible: rowHost.modelData.userDeclared || !rowHost.modelData.declared
                                            implicitWidth: badgeText.implicitWidth + 16
                                            implicitHeight: badgeText.implicitHeight + 2
                                            radius: Theme.radius.control   // 徽章走中性面：tagBg 是蓝调底（#EEF2FF / #1E3A5F）
                                            color: Theme.color.buttonBg
                                            anchors.verticalCenter: parent.verticalCenter

                                            Text {
                                                id: badgeText
                                                anchors.centerIn: parent
                                                text: rowHost.modelData.userDeclared
                                                    ? qsTrId("model_source_custom")
                                                    : qsTrId("model_source_adapter_builtin")
                                                color: Theme.color.textSecondary
                                                font.family: "Microsoft YaHei UI"
                                                font.pixelSize: Theme.font.small
                                            }
                                        }

                                        Text {
                                            id: chevron
                                            anchors.verticalCenter: parent.verticalCenter
                                            text: page.isExpanded(rowHost.modelData) ? "▾" : "▸"
                                            color: Theme.color.textCaption
                                            font.pixelSize: Theme.font.caption
                                        }
                                    }

                                    MouseArea {
                                        id: headerMouse
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        cursorShape: Qt.PointingHandCursor
                                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                                        onClicked: (mouse) => {
                                            if (mouse.button === Qt.RightButton)
                                                rowMenu.popup()
                                            else
                                                page.toggleRow(rowHost.modelData)
                                        }
                                    }

                                    Menu {
                                        id: rowMenu

                                        MenuItem {
                                            text: qsTrId("common_delete")
                                            enabled: Hub.settings.writable
                                            onTriggered: Hub.settings.removeModel(rowHost.modelData.provider,
                                                                                   rowHost.modelData.id)
                                        }

                                        MenuItem {
                                            visible: !Hub.settings.writable
                                            enabled: false
                                            text: qsTrId("model_delete_readonly")
                                        }
                                    }
                                }

                                Column {   // 详情（原版边距 (10,0,10,10)、spacing 2；名称列固定 96）
                                    id: detail
                                    width: parent.width
                                    visible: page.isExpanded(rowHost.modelData)
                                    topPadding: 0
                                    bottomPadding: 10
                                    leftPadding: 10
                                    rightPadding: 10
                                    spacing: 2

                                    Repeater {
                                        model: {
                                            const row = rowHost.modelData
                                            const rows = [
                                                [qsTrId("model_provider_route"), row.provider],
                                                [qsTrId("model_id_label"), row.id],
                                                [qsTrId("model_display_name"), row.name]
                                            ]
                                            if (row.description && row.description.length > 0)
                                                rows.push([qsTrId("model_description_label"), row.description])
                                            if (row.hasContextWindow)
                                                rows.push([qsTrId("model_context_window"), String(row.contextWindow)])
                                            if (row.hasMaxTokens)
                                                rows.push([qsTrId("model_max_output"), String(row.maxTokens)])
                                            rows.push([qsTrId("model_think_level"), page.reasoningText(row)])
                                            rows.push([qsTrId("model_config_source"), page.configSourceText(row)])
                                            const writeback = page.writebackText(row)
                                            if (writeback.length > 0)
                                                rows.push([qsTrId("model_writeback_target"), writeback])
                                            return rows
                                        }

                                        delegate: Row {
                                            required property var modelData
                                            width: detail.width - detail.leftPadding - detail.rightPadding
                                            spacing: 8

                                            Text {
                                                width: page.detailLabelWidth
                                                text: modelData[0]
                                                color: Theme.color.textTertiary
                                                font.family: "Microsoft YaHei UI"
                                                font.pixelSize: Theme.font.caption
                                            }

                                            Text {
                                                width: parent.width - page.detailLabelWidth - parent.spacing
                                                text: modelData[1]
                                                wrapMode: Text.Wrap
                                                color: Theme.color.textSecondary
                                                font.family: "Microsoft YaHei UI"
                                                font.pixelSize: Theme.font.caption
                                            }
                                        }
                                    }
                                }

                                // 凭据是**路由级**的（一个 provider 一条引用），所以展开任意一条模型就地写，
                                // 同路由下所有模型共用；只写 credentials/set，不动 models 数组。
                                Column {
                                    id: keyPanel
                                    width: parent.width
                                    visible: page.isExpanded(rowHost.modelData)
                                    bottomPadding: 10
                                    leftPadding: 10
                                    rightPadding: 10
                                    spacing: 4

                                    Row {
                                        width: keyPanel.width - keyPanel.leftPadding - keyPanel.rightPadding
                                        spacing: 8

                                        Text {
                                            width: page.detailLabelWidth
                                            anchors.verticalCenter: parent.verticalCenter
                                            text: "API Key"
                                            color: Theme.color.textTertiary
                                            font.family: "Microsoft YaHei UI"
                                            font.pixelSize: Theme.font.caption
                                        }

                                        FieldBox {
                                            id: rowKeyField
                                            width: parent.width - page.detailLabelWidth - saveKeyButton.width - 2 * parent.spacing
                                            height: page.fieldHeight
                                            placeholder: page.keyReadonly(rowHost.modelData.provider)
                                                ? qsTrId("model_credential_readonly_hint")
                                                : qsTrId("model_key_hint")
                                            password: true
                                            enabled: !page.keyReadonly(rowHost.modelData.provider)
                                        }

                                        IconButton {
                                            id: saveKeyButton
                                            height: page.fieldHeight
                                            label: qsTrId("common_save")
                                            enabled: !page.keyReadonly(rowHost.modelData.provider)
                                                && rowKeyField.text.trim().length > 0
                                                && !Hub.settings.busy
                                            onClicked: {
                                                Hub.settings.saveApiKey(rowHost.modelData.provider, rowKeyField.text)
                                                rowKeyField.text = ""
                                            }
                                        }
                                    }

                                    Text {   // 凭据状态（与新增表单那一行同一套文案：引用名 + 已/未配置 + 来源）
                                        width: keyPanel.width - keyPanel.leftPadding - keyPanel.rightPadding
                                        text: page.credentialText(rowHost.providerRow)
                                        wrapMode: Text.WordWrap
                                        color: Theme.color.textCaption
                                        font.family: "Microsoft YaHei UI"
                                        font.pixelSize: Theme.font.small
                                    }
                                }
                            }

                            Rectangle {   // 多张卡里找不到是哪一张被打开了。声明在 entryColumn 之后，保证在最上层
                                visible: page.isExpanded(rowHost.modelData)
                                x: 1
                                y: 10
                                width: 2
                                height: Math.max(0, parent.height - 20)
                                radius: 1
                                color: Theme.color.textPrimary
                            }
                        }
                    }
                }

                Text {
                    width: parent.width
                    visible: Hub.settings.models.length === 0
                    text: qsTrId("model_catalog_empty")
                    wrapMode: Text.WordWrap
                    color: Theme.color.textSecondary
                    font.family: "Microsoft YaHei UI"
                    font.pixelSize: Theme.font.caption
                }

                Rectangle {
                    id: form
                    width: parent.width
                    visible: page.formOpen
                    implicitHeight: formColumn.implicitHeight + 20
                    radius: Theme.radius.card
                    color: Theme.color.inputBg
                    border.width: 1
                    border.color: Theme.color.border

                    Column {
                        id: formColumn
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: 10
                        spacing: 6

                        Text {
                            text: qsTrId("model_add_title")
                            color: Theme.color.textPrimary
                            font.family: "Microsoft YaHei UI"
                            font.pixelSize: Theme.font.body
                            font.weight: Theme.font.weightBold
                        }

                        Row {   // 1 提供方路由
                            width: parent.width
                            spacing: 8

                            Text {
                                width: page.labelWidth
                                text: qsTrId("model_provider_route")
                                horizontalAlignment: Text.AlignRight
                                anchors.verticalCenter: parent.verticalCenter
                                color: Theme.color.textSecondary
                                font.family: "Microsoft YaHei UI"
                                font.pixelSize: Theme.font.caption
                            }

                            ComboBox {   // 无圆角，且键盘焦点时描边是 palette.highlight = 强调蓝）
                                id: providerCombo
                                width: parent.width - page.labelWidth - parent.spacing
                                height: page.fieldHeight
                                model: Hub.settings.providers
                                textRole: "label"
                                font.family: "Microsoft YaHei UI"
                                font.pixelSize: Theme.font.body
                                leftPadding: Theme.spacing.fieldPadX
                                rightPadding: Theme.spacing.fieldPadX + Theme.size.dropdownArrowWidth

                                HoverHandler {
                                    id: providerHover
                                    cursorShape: Qt.PointingHandCursor
                                }

                                contentItem: Text {
                                    leftPadding: 10
                                    rightPadding: 28
                                    text: providerCombo.displayText
                                    elide: Text.ElideRight
                                    verticalAlignment: Text.AlignVCenter
                                    color: Theme.color.textPrimary
                                    font.family: "Microsoft YaHei UI"
                                    font.pixelSize: Theme.font.body
                                }

                                indicator: LineIcon {
                                    x: providerCombo.width - width - 9
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: 12
                                    height: 12
                                    name: "chevron-down"
                                    size: 12
                                    color: providerCombo.activeFocus
                                        ? Theme.color.textPrimary
                                        : Theme.color.textSecondary
                                    rotation: providerCombo.popup.visible ? 180 : 0
                                    Behavior on rotation {
                                        NumberAnimation { duration: Theme.motion.fast }
                                    }
                                }

                                background: Rectangle {
                                    color: Theme.color.inputBg
                                    border.width: 1
                                    border.color: providerCombo.activeFocus
                                        ? Theme.color.textSecondary
                                        : (providerHover.hovered ? Theme.color.scrollbarHover : Theme.color.inputBorder)
                                    radius: Theme.radius.control
                                }

                                delegate: MenuRow {
                                    required property var modelData
                                    required property int index

                                    width: ListView.view ? ListView.view.width : 0
                                    title: modelData.label
                                    checked: providerCombo.currentIndex === index
                                    onChosen: {
                                        providerCombo.currentIndex = index
                                        providerCombo.popup.close()
                                    }
                                }

                                popup: Popup {
                                    y: providerCombo.height + 4
                                    width: providerCombo.width
                                    implicitHeight: Math.min(contentItem.implicitHeight + 12, 280)
                                    padding: 6

                                    background: Rectangle {
                                        radius: 12
                                        color: Theme.color.menuBg
                                        border.width: 1
                                        border.color: Theme.color.menuBorder
                                    }

                                    contentItem: ListView {
                                        clip: true
                                        implicitHeight: contentHeight
                                        model: providerCombo.delegateModel
                                        currentIndex: providerCombo.highlightedIndex
                                        ScrollBar.vertical: AppScrollBar { }
                                    }
                                }
                            }
                        }

                        Row {   // 2 模型 ID + 获取模型
                            width: parent.width
                            spacing: 8

                            Text {
                                width: page.labelWidth
                                text: qsTrId("model_id_label")
                                horizontalAlignment: Text.AlignRight
                                anchors.verticalCenter: parent.verticalCenter
                                color: Theme.color.textSecondary
                                font.family: "Microsoft YaHei UI"
                                font.pixelSize: Theme.font.caption
                            }

                            FieldBox {
                                id: idField
                                width: parent.width - page.labelWidth - fetchButton.width - 2 * parent.spacing
                                placeholder: qsTrId("model_id_hint")
                            }

                            IconButton {
                                id: fetchButton
                                height: page.fieldHeight
                                label: qsTrId("model_fetch_models")
                                tooltip: qsTrId("model_fetch_hint")
                                onClicked: {
                                    page.candidatesVisible = true
                                    Hub.settings.discover(providerCombo.currentIndex >= 0
                                        ? Hub.settings.providers[providerCombo.currentIndex].provider : "",
                                        apiKeyField.text)
                                }
                            }
                        }

                        Column {   // 候选模型（原版是独立弹层；这里就地铺在 ID 输入框下方，点一条回填）
                            width: parent.width
                            visible: page.candidatesVisible && Hub.settings.candidates.length > 0
                            leftPadding: page.labelWidth + 8
                            spacing: 2

                            Repeater {
                                model: Hub.settings.candidates

                                delegate: Rectangle {
                                    required property var modelData
                                    width: parent.width
                                    implicitHeight: 26
                                    radius: 5
                                    color: optionMouse.containsMouse ? Theme.color.activeBg : "transparent"

                                    Text {
                                        anchors.left: parent.left
                                        anchors.leftMargin: 8
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: modelData.id
                                        color: Theme.color.textPrimary
                                        font.family: "Microsoft YaHei UI"
                                        font.pixelSize: Theme.font.caption
                                    }

                                    MouseArea {
                                        id: optionMouse
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: {
                                            idField.text = modelData.id
                                            page.candidatesVisible = false
                                        }
                                    }
                                }
                            }
                        }

                        Row {   // 3 显示名称
                            width: parent.width
                            spacing: 8

                            Text {
                                width: page.labelWidth
                                text: qsTrId("model_display_name")
                                horizontalAlignment: Text.AlignRight
                                anchors.verticalCenter: parent.verticalCenter
                                color: Theme.color.textSecondary
                                font.family: "Microsoft YaHei UI"
                                font.pixelSize: Theme.font.caption
                            }

                            FieldBox {
                                id: nameField
                                width: parent.width - page.labelWidth - parent.spacing
                                placeholder: qsTrId("model_display_name_hint")
                            }
                        }

                        Row {   // 4 上下文窗口
                            width: parent.width
                            spacing: 8

                            Text {
                                width: page.labelWidth
                                text: qsTrId("model_context_window")
                                horizontalAlignment: Text.AlignRight
                                anchors.verticalCenter: parent.verticalCenter
                                color: Theme.color.textSecondary
                                font.family: "Microsoft YaHei UI"
                                font.pixelSize: Theme.font.caption
                            }

                            FieldBox {
                                id: contextField
                                width: parent.width - page.labelWidth - parent.spacing
                                placeholder: qsTrId("model_context_window_hint")
                                numeric: true
                            }
                        }

                        Row {   // 5 最大输出
                            width: parent.width
                            spacing: 8

                            Text {
                                width: page.labelWidth
                                text: qsTrId("model_max_output")
                                horizontalAlignment: Text.AlignRight
                                anchors.verticalCenter: parent.verticalCenter
                                color: Theme.color.textSecondary
                                font.family: "Microsoft YaHei UI"
                                font.pixelSize: Theme.font.caption
                            }

                            FieldBox {
                                id: maxTokensField
                                width: parent.width - page.labelWidth - parent.spacing
                                placeholder: qsTrId("model_max_output_hint")
                                numeric: true
                            }
                        }

                        Row {   // 6 思考档位（仅 pi-ai 路由显示，与原版一致）
                            width: parent.width
                            spacing: 8
                            visible: page.currentProvider !== null && Hub.settings.routeIsPiAi(page.currentProvider.provider)

                            Text {
                                width: page.labelWidth
                                text: qsTrId("model_think_level")
                                horizontalAlignment: Text.AlignRight
                                anchors.verticalCenter: parent.verticalCenter
                                color: Theme.color.textSecondary
                                font.family: "Microsoft YaHei UI"
                                font.pixelSize: Theme.font.caption
                            }

                            FieldBox {
                                id: effortsField
                                width: parent.width - page.labelWidth - parent.spacing
                                placeholder: qsTrId("model_think_level_hint")
                            }
                        }

                        Row {   // 7 输入模态（仅非 pi-ai 路由显示）
                            width: parent.width
                            spacing: 8
                            visible: page.currentProvider !== null && !Hub.settings.routeIsPiAi(page.currentProvider.provider)

                            Text {
                                width: page.labelWidth
                                text: qsTrId("model_input_modality")
                                horizontalAlignment: Text.AlignRight
                                anchors.verticalCenter: parent.verticalCenter
                                color: Theme.color.textSecondary
                                font.family: "Microsoft YaHei UI"
                                font.pixelSize: Theme.font.caption
                            }

                            // 勾选控件：默认 Basic 风格的指示器是 28×28 方角 + 1px 平台灰描边，
                            // 且 **键盘焦点时描边取 palette.highlight（ThemeManager 装的是强调蓝）**。
                            // 悬停浅灰底、选中 = textPrimary 近黑面 + 反相勾（panelBg），
                            // 禁用退回 border 描边。整体高 = 18 + 上下 padding 12 = 30px（命中区 > 24px）。
                            CheckBox {
                                id: imageInputBox
                                text: qsTrId("model_supports_image")
                                font.family: "Microsoft YaHei UI"
                                font.pixelSize: Theme.font.body
                                spacing: Theme.spacing.labelGap
                                palette.windowText: Theme.color.textPrimary   // 标签色/高亮色也钉在 token 上：不再受全局 QPalette 影响（深色主题下尤其重要）
                                palette.text: Theme.color.textPrimary
                                palette.highlight: Theme.color.textSecondary
                                palette.highlightedText: Theme.color.panelBg

                                indicator: Rectangle {
                                    implicitWidth: Theme.size.checkbox
                                    implicitHeight: Theme.size.checkbox
                                    x: imageInputBox.leftPadding
                                    y: imageInputBox.topPadding
                                        + (imageInputBox.availableHeight - height) / 2
                                    radius: Theme.radius.check
                                    color: {
                                        if (!imageInputBox.enabled)
                                            return "transparent"
                                        if (imageInputBox.checked)
                                            return Theme.color.textPrimary
                                        return imageInputBox.hovered ? Theme.color.hoverBg : "transparent"
                                    }
                                    border.width: 1
                                    border.color: {
                                        if (!imageInputBox.enabled)
                                            return Theme.color.border
                                        if (imageInputBox.checked)
                                            return Theme.color.textPrimary

                                        // 未勾选的静息描边用 textSecondary / inputBorder（scrollbarHover 那档太深）
                                        return (imageInputBox.hovered || imageInputBox.visualFocus)
                                            ? Theme.color.textSecondary
                                            : Theme.color.inputBorder
                                    }

                                    Text {
                                        anchors.centerIn: parent
                                        visible: imageInputBox.checkState === Qt.Checked   // 勾：反相中性色（浅色下"近黑面 + 白勾"，深色下"近白面 + 深灰勾"）
                                        text: "✓"
                                        color: Theme.color.panelBg
                                        font.family: "Microsoft YaHei UI"
                                        font.pixelSize: Theme.font.small
                                        font.weight: Theme.font.weightBold
                                    }
                                }
                            }
                        }

                        Row {   // 8 API Key（原版是字面量标签，无 id）
                            width: parent.width
                            spacing: 8

                            Text {
                                width: page.labelWidth
                                text: "API Key"
                                horizontalAlignment: Text.AlignRight
                                anchors.verticalCenter: parent.verticalCenter
                                color: Theme.color.textSecondary
                                font.family: "Microsoft YaHei UI"
                                font.pixelSize: Theme.font.caption
                            }

                            FieldBox {
                                id: apiKeyField
                                width: parent.width - page.labelWidth - parent.spacing
                                placeholder: page.credentialReadonly()
                                    ? qsTrId("model_credential_readonly_hint")
                                    : qsTrId("model_credential_ref_hint")
                                password: true
                            }
                        }

                        Text {
                            width: parent.width
                            text: page.credentialText(page.currentProvider)
                            wrapMode: Text.WordWrap
                            color: Theme.color.textCaption
                            font.family: "Microsoft YaHei UI"
                            font.pixelSize: Theme.font.small
                        }

                        Text {
                            width: parent.width
                            text: page.currentProvider
                                ? Hub.settings.routeHint(page.currentProvider.provider)
                                : qsTrId("model_no_configurable_provider")
                            wrapMode: Text.WordWrap
                            color: Theme.color.textTertiary
                            font.family: "Microsoft YaHei UI"
                            font.pixelSize: Theme.font.caption
                        }

                        Text {
                            width: parent.width
                            text: Hub.settings.formFeedback
                            wrapMode: Text.WordWrap
                            color: Theme.color.textSecondary   // 表单反馈：原版走强调色（蓝）⇒ 中性灰
                            font.family: "Microsoft YaHei UI"
                            font.pixelSize: Theme.font.caption
                        }

                        Row {
                            width: parent.width
                            spacing: 6
                            layoutDirection: Qt.RightToLeft

                            IconButton {
                                id: submitButton
                                height: page.fieldHeight
                                label: qsTrId("common_add")
                                onClicked: {
                                    const provider = page.currentProvider
                                    if (!provider)
                                        return
                                    Hub.settings.submitModelForm(provider.provider, idField.text, nameField.text,
                                        contextField.text, maxTokensField.text, effortsField.text, apiKeyField.text)
                                }
                            }

                            IconButton {
                                id: cancelButton
                                height: page.fieldHeight
                                label: qsTrId("common_cancel")
                                onClicked: page.formOpen = false
                            }
                        }
                    }
                }
            }
        }

        Item {
            id: actionFooter
            width: parent.width
            height: 34

            ActionButton {
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                width: 116
                height: 30
                // 新增模型的动作和表单都在页面底部，不再把入口放在标题栏。
                text: page.formOpen ? qsTrId("common_cancel") : qsTrId("model_add")
                variant: page.formOpen ? "ghost" : "elevated"
                enabled: page.formOpen
                    || (Hub.settings.writable && Hub.settings.providers.length > 0)
                tooltip: enabled ? "" : qsTrId("model_add_requires_api")
                onClicked: {
                    if (page.formOpen) {
                        page.formOpen = false
                        page.candidatesVisible = false
                        return
                    }
                    page.formOpen = true
                    page.candidatesVisible = false
                    Qt.callLater(function() {
                        scroller.contentY = Math.max(0, scroller.contentHeight - scroller.height)
                    })
                }
            }
        }
    }

    function credentialReadonly() {
        const row = page.currentProvider
        return row !== null && row.keyKnown && !row.keyWritable
    }
}
