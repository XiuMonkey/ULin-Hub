// 设置 · 模型列表（按原版 ui/ModelListPanel.cpp + styles/model-list.qss 复刻）
//
// 结构（自上而下，与原版一一对应）：
//   1 标题 model_list_title(14/600)   2 说明 model_list_desc(12 secondary)
//   3 状态行(12 secondary，加载/汇总/失败逐条)   4 瞬时提示(12 accent，4 秒自隐)
//   5 滚动区：#modelListGroup 分组标题 + #modelListEntry 成员卡（点击展开详情）+ 空态 + 新增表单
//   6 「添加模型」按钮在滚动区**外**（表单在滚动区内，面板高度才不随表单开合变化）
//
// 与原版一致的几个要点：
//   * 成员卡无列、无选中态；表头最小高 48，右侧 ▸/▾；徽章按 userDeclared / declared 决定；
//   * 删除走右键菜单（common_delete），不是行内按钮；
//   * 表单提交 = 先（可选）写凭据 credentials/set，失败则**整次中止**，再整份写回 models；
//   * 凭据引用行 = 基础句（profile 点名 / 按约定派生）+ 状态后缀。

import QtQuick
import QtQuick.Controls.Basic
import ULinHub 1.0
import "../../widgets"
import "../../skin/settings"

Item {
    id: page

    // 展开过的成员 key（刷新后按它恢复展开态，与原版 m_expanded 同思路）
    property var expandedKeys: ({})
    property bool formOpen: false
    property bool candidatesVisible: false
    readonly property int fieldHeight: 30
    readonly property int labelWidth: 96
    readonly property int detailLabelWidth: 96

    // 当前表单选中的路由
    readonly property var currentProvider: {
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

    function toggleRow(row) {
        const key = page.keyOf(row)
        const next = Object.assign({}, page.expandedKeys)
        if (next[key])
            delete next[key]
        else
            next[key] = true
        page.expandedKeys = next
    }

    // 状态行文案（原版 updateStatus）
    function statusText() {
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

    // 凭据引用行：基础句 + 状态后缀（原版 refreshCredentialRowText）
    function credentialText(row) {
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

    // 详情行：值为空时不显示（原版 addDetailRow）
    function reasoningText(row) {
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

        Text {
            width: parent.width
            text: qsTrId("model_list_title")
            color: Theme.color.textPrimary
            font.family: "Microsoft YaHei UI"
            font.pixelSize: Theme.font.title
            font.weight: Theme.font.weightBold
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
            color: Theme.color.accent
            font.family: "Microsoft YaHei UI"
            font.pixelSize: Theme.font.caption
        }

        // ---- 滚动区（原版 #modelListScroll，最小高 120）----
        Flickable {
            id: scroller
            width: parent.width
            height: Math.max(120, root.height - scroller.y - addButton.height - root.spacing)
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

                        width: listColumn.width
                        spacing: 2

                        // 分组标题：provider 与上一行不同时插入（原版 modelListGroup）
                        Text {
                            width: parent.width
                            visible: rowHost.index === 0
                                || Hub.settings.models[rowHost.index - 1].provider !== rowHost.modelData.provider
                            text: rowHost.modelData.providerName
                            color: Theme.color.textCaption
                            font.family: "Microsoft YaHei UI"
                            font.pixelSize: Theme.font.small
                            font.weight: Theme.font.weightBold
                            topPadding: 4
                            leftPadding: 2
                        }

                        // 成员卡（原版 #modelListEntry：panelBg + 1px border + radius 10）
                        Rectangle {
                            id: entry
                            width: parent.width
                            implicitHeight: entryColumn.implicitHeight
                            radius: Theme.radius.card
                            color: Theme.color.panelBg
                            border.width: 1
                            border.color: Theme.color.border

                            Column {
                                id: entryColumn
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.top: parent.top

                                // 表头（原版最小高 48、内边距 (10,3,10,3)）
                                Item {
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
                                                font.pixelSize: Theme.font.title
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
                                        }

                                        Rectangle {
                                            id: badge
                                            visible: rowHost.modelData.userDeclared || !rowHost.modelData.declared
                                            implicitWidth: badgeText.implicitWidth + 16
                                            implicitHeight: badgeText.implicitHeight + 2
                                            radius: 8
                                            color: Theme.color.tagBg
                                            anchors.verticalCenter: parent.verticalCenter

                                            Text {
                                                id: badgeText
                                                anchors.centerIn: parent
                                                text: rowHost.modelData.userDeclared
                                                    ? qsTrId("model_source_custom")
                                                    : qsTrId("model_source_adapter_builtin")
                                                color: Theme.color.accent
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

                                // 详情（原版边距 (10,0,10,10)、spacing 2；名称列固定 96）
                                Column {
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

                // ---- 新增模型表单（在滚动区内，原版 #modelListForm）----
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

                        // 1 提供方路由
                        Row {
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

                            ComboBox {
                                id: providerCombo
                                width: parent.width - page.labelWidth - parent.spacing
                                height: page.fieldHeight
                                model: Hub.settings.providers
                                textRole: "label"
                                font.family: "Microsoft YaHei UI"
                                font.pixelSize: Theme.font.body
                            }
                        }

                        // 2 模型 ID + 获取模型
                        Row {
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

                        // 候选模型（原版是独立弹层；这里就地铺在 ID 输入框下方，点一条回填）
                        Column {
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

                        // 3 显示名称
                        Row {
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

                        // 4 上下文窗口
                        Row {
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

                        // 5 最大输出
                        Row {
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

                        // 6 思考档位（仅 pi-ai 路由显示，与原版一致）
                        Row {
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

                        // 7 输入模态（仅非 pi-ai 路由显示）
                        Row {
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

                            CheckBox {
                                id: imageInputBox
                                text: qsTrId("model_supports_image")
                                font.family: "Microsoft YaHei UI"
                                font.pixelSize: Theme.font.body
                            }
                        }

                        // 8 API Key（原版是字面量标签，无 id）
                        Row {
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
                            color: Theme.color.accent
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

        // ---- 添加模型按钮（滚动区外，原版 #modelListAddButton）----
        // 原版可用性：settingsWritable && 有可配置提供方；禁用时 tooltip 说明原因（model_add_requires_api）
        IconButton {
            id: addButton
            enabled: Hub.settings.writable && Hub.settings.providers.length > 0
            label: qsTrId("model_add")
            tooltip: enabled ? qsTrId("model_add") : qsTrId("model_add_requires_api")
            onClicked: {
                page.formOpen = true
                page.candidatesVisible = false
            }
        }
    }

    function credentialReadonly() {
        const row = page.currentProvider
        return row !== null && row.keyKnown && !row.keyWritable
    }
}
