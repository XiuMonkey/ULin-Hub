// 设置 · 模型列表（按原版 ui/ModelListPanel.cpp + styles/model-list.qss 复刻）。
// 结构自上而下：标题/说明 → 状态行 → 提示行（桥接 status）→ 滚动区（分组标题 + 成员卡 + 空态 + 新增表单）
// → 「添加模型」按钮在滚动区外（表单在滚动区内，面板高度才不随表单开合变化）。
// 要点：成员卡无列/无选中态，表头最小高 48、右侧 ▸/▾，徽章按 userDeclared/declared 决定；
// 删除走右键菜单（common_delete）不是行内按钮；表单提交先（可选）写凭据，再经适配层写入 models。

import QtQuick
import QtQuick.Controls.Basic
import QtQml
import ULinHub 1.0
import "../../../widgets"
import "../../../skin/settings"
import "../../../skin/chat"

Item {
    id: page

    readonly property var cfg: Hub.settings.ulincoreBridge   // 设置侧桥对象：providers/models/凭据状态与写回命令都由它转发

    // DSH ModelListPage 的数据适配层：页面结构/控件保持原样，只把数据源换成 ULinCore。
    property var api: UlinCoreModelAdapter { cfg: page.cfg }

    property var expandedKeys: ({})   // 展开过的成员 key（刷新后按它恢复展开态，与原版 m_expanded 同思路）
    property bool formOpen: false   // 新增表单展开态：表单在滚动区末尾，由底部按钮切换
    property bool candidatesVisible: false   // 「获取模型」拉回的候选列表是否可见，选中一条即收起
    // 表单行高 30；标签列宽 96（详情行沿用同一宽度）。
    readonly property int fieldHeight: 30
    readonly property int labelWidth: 96
    readonly property int detailLabelWidth: 96

    // 下拉项 = api 层汇总的 provider + 末尾的「自定义…」哨兵项。
    readonly property var providerOptions: {
        const rows = page.api.providers.slice()
        rows.push({ provider: "__custom__", label: "自定义…" })
        return rows
    }
    // 选中哨兵项即进入自定义表单（index 超出 provider 数量）。
    readonly property bool isCustomProvider: providerCombo.currentIndex >= page.api.providers.length

    // 当前下拉选中的 provider；选到「自定义…」时为 null。
    readonly property var currentProvider: {
        const rows = page.api.providers
        const index = providerCombo.currentIndex
        if (index < 0 || index >= rows.length)
            return null
        return rows[index]
    }

    // 展开态 key：provider 与模型 id 用不可见字符 0x1f 分隔，避免拼串时与 id 自身字符撞车。
    function keyOf(row) {
        return row.provider + "\u001f" + row.id
    }

    function isExpanded(row) {
        return page.expandedKeys[page.keyOf(row)] === true
    }

    // 模型行只带 settings 侧信息（settingsNs / settingsWritable）；凭据状态挂在 provider 行上，
    // 按 provider 路由回查一行 —— 同一路由下的所有模型共用一条凭据引用（如
    // deepseek-official → DEEPSEEK_API_KEY），所以 key 是路由级的，不是模型级的。
    function providerRowFor(providerId) {
        const rows = page.api.providers
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

    // 展开/收起：expandedKeys 按不可变对象处理，整体回写才能触发依赖它的绑定。
    function toggleRow(row) {
        const key = page.keyOf(row)
        const next = Object.assign({}, page.expandedKeys)   // 浅拷贝：QML 按引用判断属性是否变化
        if (next[key])
            delete next[key]
        else
            next[key] = true
        page.expandedKeys = next
    }

    function statusText() {   // 状态行文案（原版 updateStatus）
        if (!page.api.busy && "running" !== "running" && page.api.models.length === 0)
            return qsTrId("model_server_not_ready")   // 未就绪且无缓存目录：只报「服务未连接」，不叠加统计

        // 加载中只报进度；已有目录时拼「模型数 + 分组数」，再按需追加只读与逐路由失败原因。
        let text = ""
        if (page.api.busy && page.api.models.length === 0) {
            text = qsTrId("model_loading_catalog")
        }
        else {
            text = qsTrId("model_catalog_summary_fmt")
                .arg(page.api.models.length)
                .arg(page.api.groupCount)
            if (!page.api.writable)
                text += " " + qsTrId("model_add_readonly")
            for (let i = 0; i < page.api.failures.length; ++i) {
                const failure = page.api.failures[i]
                text += " " + qsTrId("model_provider_load_failed_fmt").arg(failure.name).arg(failure.message)   // 单个 provider 失败不阻断其余分组
            }
        }
        if (page.api.status.length > 0)
            text += " " + page.api.status   // 桥接层的瞬时状态放末尾，不挤掉统计信息
        return text
    }

    function credentialText(row) {   // 凭据引用行：基础句 + 状态后缀（原版 refreshCredentialRowText）
        if (row && row.noApiKey === true)   // 账号登录路由：没有 key 可填，别按"未配置"呈现
            return qsTrId("model_credential_account_auth")
        if (!row || !row.keyRef || row.keyRef.length === 0)
            return ""   // 没有引用名（路由未声明 keys）：整行留空，不占高度

        // 基础句给「引用名 + 来源」：profile 显式声明的与按规则派生的措辞不同。
        let text = row.keyDeclaredByProfile
            ? qsTrId("model_credential_from_profile_fmt").arg(row.keyRef)
            : qsTrId("model_credential_derived_fmt").arg(row.keyRef)

        // 状态后缀四选一：未知 > 只读 > 已配置（附来源）> 未配置，不叠加。
        if (!row.keyKnown)
            text += qsTrId("model_credential_unknown_suffix")
        else if (!row.keyWritable)
            text += qsTrId("model_credential_readonly_suffix")
        else if (row.keyConfigured) {
            text += qsTrId("model_credential_configured_fmt").arg(row.keySource && row.keySource.length > 0 ? qsTrId("model_credential_source_fmt").arg(row.keySource) : "")
        }
        else {
            text += qsTrId("model_credential_unconfigured_suffix")
        }
        return text
    }

    function reasoningText(row) {   // 详情行：值为空时不显示（原版 addDetailRow）
        if (row.reasoningDisabled)
            return qsTrId("model_think_not_offered")   // 适配器明确声明该模型不支持思考档位
        if (row.hasReasoning)
            return row.levels.join(" / ")   // 已声明：按声明顺序展示，顺序即强弱
        // 未声明时展示 pi-ai 默认档位，并标明「未声明」，避免被读成已生效配置。
        const fallback = ["off", "low", "high", "max"]
        return qsTrId("model_think_undeclared_fmt").arg(fallback.join("/"))
    }

    // 配置来源三档：用户 settings 覆写 → 内置声明 → 仅适配器提供（后两者界面上不可写）。
    function configSourceText(row) {
        if (row.userDeclared)
            return qsTrId("model_source_settings_user")   // 用户自己加或改过的模型
        if (row.declared)
            return qsTrId("model_source_settings_bundled")   // 随包分发的内置声明
        return qsTrId("model_source_adapter_only")
    }

    // 写回目标：settings 命名空间下的 models 数组；只读时补后缀，免得用户以为保存没生效。
    function writebackText(row) {
        if (!row.settingsNs || row.settingsNs.length === 0)
            return ""
        let text = row.settingsNs + " · models"
        if (!row.settingsWritable)
            text += qsTrId("model_settings_readonly_suffix")
        return text
    }

    // 页面骨架：标题 → 说明 → 状态 → 提示 → 滚动区 → 底部动作条，剩余高度全部由 Flickable 吸收。
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
                font.family: Theme.font.uiFamily
                font.pixelSize: Theme.font.title
                font.weight: Theme.font.weightBold
            }
        }

        Text {
            width: parent.width
            text: qsTrId("model_list_desc")
            wrapMode: Text.WordWrap
            color: Theme.color.textSecondary   // 固定说明：不随目录状态变化
            font.family: Theme.font.uiFamily
            font.pixelSize: Theme.font.caption
        }

        Text {
            width: parent.width
            text: page.statusText()
            wrapMode: Text.WordWrap
            color: Theme.color.textSecondary   // 目录状态：模型数/分组数/只读/失败原因，由 statusText 统一拼装
            font.family: Theme.font.uiFamily
            font.pixelSize: Theme.font.caption
        }

        Text {
            width: parent.width
            visible: page.api.notice.length > 0
            text: page.api.notice
            wrapMode: Text.WordWrap
            color: Theme.color.textSecondary   // 提示行：原版走强调色（蓝），设置页不出蓝 ⇒ 与状态行同档的中性灰
            font.family: Theme.font.uiFamily
            font.pixelSize: Theme.font.caption
        }

        // 滚动区高度 = 页面剩余高度（扣掉底部动作条与 4px 间隔），下限 120 防止窗口过矮时塌陷；
        // 内容高度只由 listColumn 决定，因此表单开合不会改变面板自身高度。
        Flickable {
            id: scroller
            width: parent.width
            height: Math.max(120, root.height - scroller.y - actionFooter.height - 4)
            contentHeight: listColumn.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds   // 禁止拖拽回弹：桌面端越界露底观感不对
            ScrollBar.vertical: AppScrollBar { }

            Column {
                id: listColumn
                width: scroller.width
                spacing: 4

                Repeater {
                    model: page.api.models   // 适配层已汇总好的扁平模型列表，按 provider 分组顺序排列

                    delegate: Column {
                        id: rowHost

                        required property var modelData
                        required property int index   // 仅用于判断上一条的 provider，决定是否插入分组标题

                        readonly property var providerRow: page.providerRowFor(modelData.provider)   // 该模型所属 provider 的那一行（凭据状态都在那边）

                        width: listColumn.width
                        spacing: 2

                        // 分组标题：provider 与上一行不同时插入（原版 modelListGroup）；压到 textTertiary +
                        // 一点字距，读起来是"分区"而不是"正文"。
                        Text {
                            width: parent.width
                            visible: rowHost.index === 0
                                || page.api.models[rowHost.index - 1].provider !== rowHost.modelData.provider
                            text: rowHost.modelData.providerName
                            color: Theme.color.textTertiary
                            font.family: Theme.font.uiFamily
                            font.pixelSize: Theme.font.small
                            font.weight: Theme.font.weightBold
                            font.letterSpacing: 0.4
                            topPadding: 4
                            leftPadding: 2
                        }

                        Rectangle {   // 成员卡（原版 #modelListEntry：panelBg + 1px border + radius 10）
                            id: entry
                            width: parent.width
                            implicitHeight: entryColumn.implicitHeight   // 高度跟随内容：详情/凭据面板收起时卡片自动变矮
                            radius: Theme.radius.card
                            color: Theme.color.pageBg || Theme.color.panelBg
                            // 原描边是 border(4% 黑)，和白底几乎同色 —— 卡片边界只能靠 4px 间距"猜"。
                            // 提到 menuBorder(6%)，展开时 inputBorder(10%)：一屏四张才分得清。
                            border.width: 1
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
                                        color: headerMouse.containsMouse ? Theme.color.hoverBg : "transparent"   // 整块表头可悬停，提示这行可点
                                    }

                                    Row {
                                        id: headerRow
                                        anchors.left: parent.left
                                        anchors.right: parent.right
                                        anchors.leftMargin: 10
                                        anchors.rightMargin: 10
                                        anchors.verticalCenter: parent.verticalCenter
                                        spacing: 8

                                        Column {   // 名称列：吃掉徽章与箭头之外的全部宽度
                                            width: parent.width - badge.width - chevron.width - 2 * parent.spacing
                                            spacing: 0

                                            Text {
                                                width: parent.width
                                                text: rowHost.modelData.name
                                                elide: Text.ElideRight
                                                color: Theme.color.textPrimary
                                                font.family: Theme.font.uiFamily
                                                font.pixelSize: Theme.font.body   // body(13)：与页面标题(14)拉开层级，模型名不抢标题
                                                font.weight: Theme.font.weightMedium
                                            }

                                            Text {   // 副标题：显示名 · 模型 id，id 是写配置文件时的真实键
                                                width: parent.width
                                                text: rowHost.modelData.providerName + " · " + rowHost.modelData.id
                                                elide: Text.ElideRight
                                                color: Theme.color.textTertiary
                                                font.family: Theme.font.uiFamily
                                                font.pixelSize: Theme.font.caption
                                            }

                                            // 这条路由还没配凭据时给个入口提示；前面的 5px 琥珀点是全页唯一的饱和度来源，
                                            // 只出现在需要动手的那一行。
                                            Row {
                                                width: parent.width
                                                visible: rowHost.providerRow !== null
                                                    && rowHost.providerRow.noApiKey !== true
                                                    && rowHost.providerRow.keyKnown
                                                    && !rowHost.providerRow.keyConfigured
                                                topPadding: 1
                                                spacing: 5

                                                Rectangle {
                                                    width: 5
                                                    height: 5
                                                    radius: 2.5
                                                    anchors.verticalCenter: parent.verticalCenter
                                                    color: "#F59E0B"   // 唯一一处硬编码色：琥珀点只在「待配置」行出现
                                                }

                                                Text {
                                                    width: parent.width - 10
                                                    text: qsTrId("model_key_missing")
                                                    elide: Text.ElideRight
                                                    color: Theme.color.textTertiary
                                                    font.family: Theme.font.uiFamily
                                                    font.pixelSize: Theme.font.small
                                                }
                                            }
                                        }

                                        Rectangle {
                                            id: badge
                                            visible: rowHost.modelData.userDeclared || !rowHost.modelData.declared   // 只在「自定义」或「无内置声明」时出现
                                            implicitWidth: badgeText.implicitWidth + 16
                                            implicitHeight: badgeText.implicitHeight + 2
                                            radius: Theme.radius.control   // 徽章走中性面：tagBg 是蓝调底（#EEF2FF / #1E3A5F）
                                            color: Theme.color.buttonBg
                                            anchors.verticalCenter: parent.verticalCenter

                                            Text {
                                                id: badgeText
                                                anchors.centerIn: parent
                                                text: rowHost.modelData.userDeclared
                                                    ? qsTrId("model_source_custom")   // 用户声明
                                                    : qsTrId("model_source_adapter_builtin")   // 适配器内置
                                                color: Theme.color.textSecondary
                                                font.family: Theme.font.uiFamily
                                                font.pixelSize: Theme.font.small
                                            }
                                        }

                                        Text {
                                            id: chevron
                                            anchors.verticalCenter: parent.verticalCenter
                                            text: page.isExpanded(rowHost.modelData) ? "▾" : "▸"   // 展开/收起方向指示，纯文本字符不用图标
                                            color: Theme.color.textCaption
                                            font.pixelSize: Theme.font.caption
                                        }
                                    }

                                    // 表头命中区：左键切换展开，右键弹菜单；两种键都落在这个 MouseArea 上。
                                    MouseArea {
                                        id: headerMouse
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        cursorShape: Qt.PointingHandCursor
                                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                                        onClicked: (mouse) => {
                                            if (mouse.button === Qt.RightButton)
                                                rowMenu.popup()   // 右键只开菜单，不改变展开态
                                            else
                                                page.toggleRow(rowHost.modelData)
                                        }
                                    }

                                    // 右键菜单：不可写时删除项禁用，并补一条只读说明。
                                    Menu {
                                        id: rowMenu

                                        MenuItem {
                                            text: qsTrId("common_delete")
                                            enabled: page.api.writable
                                            onTriggered: page.api.removeModel(rowHost.modelData.provider, rowHost.modelData.id)   // provider + id 才能唯一定位一条模型
                                        }

                                        MenuItem {
                                            visible: !page.api.writable
                                            enabled: false
                                            text: qsTrId("model_delete_readonly")   // 只读时补一条说明，避免菜单看起来「坏了」
                                        }
                                    }
                                }

                                Column {   // 详情（原版边距 (10,0,10,10)、spacing 2；名称列固定 96）
                                    id: detail
                                    width: parent.width
                                    visible: page.isExpanded(rowHost.modelData)   // 收起时 Column 跳过该项，卡片高度随之收缩
                                    topPadding: 0
                                    bottomPadding: 10
                                    leftPadding: 10
                                    rightPadding: 10
                                    spacing: 2

                                    Repeater {
                                        // 详情行：provider / id / 名称必有，其余字段按是否存在追加。
                                        model: {
                                            const row = rowHost.modelData
                                            const rows = [
                                                [qsTrId("model_provider_route"), row.provider],
                                                [qsTrId("model_id_label"), row.id],
                                                [qsTrId("model_display_name"), row.name]
                                            ]
                                            // 可选字段：描述为空、未声明上下文窗口/最大输出时不占一行。
                                            if (row.description && row.description.length > 0)
                                                rows.push([qsTrId("model_description_label"), row.description])
                                            // hasXxx 由适配层给出，区分「未声明」与「声明为 0」。
                                            if (row.hasContextWindow)
                                                rows.push([qsTrId("model_context_window"), String(row.contextWindow)])
                                            if (row.hasMaxTokens)
                                                rows.push([qsTrId("model_max_output"), String(row.maxTokens)])
                                            // 思考档位与配置来源始终显示，是排查「为什么没生效」的主要线索。
                                            rows.push([qsTrId("model_think_level"), page.reasoningText(row)])
                                            rows.push([qsTrId("model_config_source"), page.configSourceText(row)])
                                            const writeback = page.writebackText(row)
                                            if (writeback.length > 0)
                                                rows.push([qsTrId("model_writeback_target"), writeback])
                                            return rows
                                        }

                                        delegate: Row {   // 两列：固定 96 宽标签 + 可换行的值
                                            required property var modelData
                                            width: detail.width - detail.leftPadding - detail.rightPadding
                                            spacing: 8

                                            Text {
                                                width: page.detailLabelWidth
                                                text: modelData[0]
                                                color: Theme.color.textTertiary
                                                font.family: Theme.font.uiFamily
                                                font.pixelSize: Theme.font.caption
                                            }

                                            Text {
                                                width: parent.width - page.detailLabelWidth - parent.spacing
                                                text: modelData[1]
                                                wrapMode: Text.Wrap
                                                color: Theme.color.textSecondary
                                                font.family: Theme.font.uiFamily
                                                font.pixelSize: Theme.font.caption
                                            }
                                        }
                                    }
                                }

                                // 凭据是路由级的（一个 provider 一条引用），所以展开任意一条模型就地写，
                                // 同路由下所有模型共用；只写 credentials/set，不动 models 数组。
                                Column {
                                    id: keyPanel
                                    width: parent.width
                                    visible: page.isExpanded(rowHost.modelData)   // 与详情区同一展开态，收起时不占高度
                                    bottomPadding: 10
                                    leftPadding: 10
                                    rightPadding: 10
                                    spacing: 4

                                    // 账号登录路由没有 key 可填：整行隐藏，只留下面的状态说明。
                                    Row {
                                        width: keyPanel.width - keyPanel.leftPadding - keyPanel.rightPadding
                                        spacing: 8
                                        visible: !(rowHost.providerRow !== null && rowHost.providerRow.noApiKey === true)

                                        Text {
                                            width: page.detailLabelWidth
                                            anchors.verticalCenter: parent.verticalCenter
                                            text: "API Key"   // 字面量标签：与原版一致，不走 qsTrId
                                            color: Theme.color.textTertiary
                                            font.family: Theme.font.uiFamily
                                            font.pixelSize: Theme.font.caption
                                        }

                                        FieldBox {
                                            id: rowKeyField
                                            width: parent.width - page.detailLabelWidth - saveKeyButton.width - 2 * parent.spacing
                                            height: page.fieldHeight
                                            placeholder: page.keyReadonly(rowHost.modelData.provider)
                                                ? qsTrId("model_credential_readonly_hint")
                                                : qsTrId("model_key_hint")   // 只读路由换提示语，而不是让输入框「静默」不可用
                                            password: true
                                            enabled: !page.keyReadonly(rowHost.modelData.provider)
                                        }

                                        IconButton {
                                            id: saveKeyButton
                                            height: page.fieldHeight
                                            label: qsTrId("common_save")
                                            enabled: !page.keyReadonly(rowHost.modelData.provider)
                                                && rowKeyField.text.trim().length > 0
                                                && !page.api.busy
                                            onClicked: {
                                                page.api.saveApiKey(rowHost.modelData.provider, rowKeyField.text)
                                                rowKeyField.text = ""   // 保存后立即清空：凭据不回显明文，只由下一行状态说明体现
                                            }
                                        }
                                    }

                                    Text {   // 凭据状态（与新增表单那一行同一套文案：引用名 + 已/未配置 + 来源）
                                        width: keyPanel.width - keyPanel.leftPadding - keyPanel.rightPadding
                                        text: page.credentialText(rowHost.providerRow)
                                        wrapMode: Text.WordWrap
                                        color: Theme.color.textCaption
                                        font.family: Theme.font.uiFamily
                                        font.pixelSize: Theme.font.small
                                    }
                                }
                            }

                            Rectangle {   // 左侧竖条：展开时标出当前卡；声明在 entryColumn 之后，保证在最上层
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
                    visible: page.api.models.length === 0
                    text: qsTrId("model_catalog_empty")   // 空态只描述「没有模型」，加载/错误另由状态行承担
                    wrapMode: Text.WordWrap
                    color: Theme.color.textSecondary
                    font.family: Theme.font.uiFamily
                    font.pixelSize: Theme.font.caption
                }

                // 新增表单：与成员卡同一套圆角与描边，藏在滚动区末尾，展开时滚到底部才可见。
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
                            font.family: Theme.font.uiFamily
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
                                font.family: Theme.font.uiFamily
                                font.pixelSize: Theme.font.caption
                            }

                            ComboBox {   // 原版无圆角、焦点描边取强调蓝；本页改走 token（radius.control + 中性描边）
                                id: providerCombo
                                width: parent.width - page.labelWidth - parent.spacing
                                height: page.fieldHeight
                                model: page.providerOptions
                                textRole: "label"
                                font.family: Theme.font.uiFamily
                                font.pixelSize: Theme.font.body
                                leftPadding: Theme.spacing.fieldPadX
                                rightPadding: Theme.spacing.fieldPadX + Theme.size.dropdownArrowWidth

                                HoverHandler {
                                    id: providerHover
                                    cursorShape: Qt.PointingHandCursor   // 悬停只影响描边色，不改文字色
                                }

                                contentItem: Text {
                                    leftPadding: 10
                                    rightPadding: 28
                                    text: providerCombo.displayText
                                    elide: Text.ElideRight
                                    verticalAlignment: Text.AlignVCenter
                                    color: Theme.color.textPrimary
                                    font.family: Theme.font.uiFamily
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
                                    rotation: providerCombo.popup.visible ? 180 : 0   // 弹层展开时箭头翻转，收起时复原
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

                                // 下拉行复用 MenuRow：选中即回填 currentIndex 并主动收起弹层。
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
                                    implicitHeight: Math.min(contentItem.implicitHeight + 12, 280)   // 最多 280 高，超出内部滚动
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

                        // 自定义提供商表单：ID / 名称 / API 地址 / 协议 / 模型 / Key，保存后写入 provider 列表。
                        Column {
                            width: parent.width
                            spacing: 6
                            visible: page.isCustomProvider   // 只有下拉选到末尾「自定义…」哨兵项时才展开

                            Text {
                                width: parent.width
                                text: "自定义提供商：保存后会出现在上方下拉框和模型列表里。"
                                wrapMode: Text.WordWrap
                                color: Theme.color.textSecondary
                                font.family: Theme.font.uiFamily
                                font.pixelSize: Theme.font.caption
                            }
                            Row { width: parent.width; height: page.fieldHeight; spacing: 8
                                Text { width: page.labelWidth; text: "提供商 ID"; horizontalAlignment: Text.AlignRight; anchors.verticalCenter: parent.verticalCenter; color: Theme.color.textSecondary; font.family: Theme.font.uiFamily; font.pixelSize: Theme.font.caption }
                                FieldBox { id: customRouteField; width: parent.width - page.labelWidth - parent.spacing; height: page.fieldHeight; placeholder: "acme-gateway" }
                            }
                            Row { width: parent.width; height: page.fieldHeight; spacing: 8
                                Text { width: page.labelWidth; text: "显示名称"; horizontalAlignment: Text.AlignRight; anchors.verticalCenter: parent.verticalCenter; color: Theme.color.textSecondary; font.family: Theme.font.uiFamily; font.pixelSize: Theme.font.caption }
                                FieldBox { id: customNameField; width: parent.width - page.labelWidth - parent.spacing; height: page.fieldHeight; placeholder: "可选" }
                            }
                            Row { width: parent.width; height: page.fieldHeight; spacing: 8
                                Text { width: page.labelWidth; text: "API 地址"; horizontalAlignment: Text.AlignRight; anchors.verticalCenter: parent.verticalCenter; color: Theme.color.textSecondary; font.family: Theme.font.uiFamily; font.pixelSize: Theme.font.caption }
                                FieldBox { id: customBaseUrlField; width: parent.width - page.labelWidth - parent.spacing; height: page.fieldHeight; placeholder: "https://gateway.example/v1" }
                            }
                            Row { width: parent.width; height: page.fieldHeight; spacing: 8
                                Text { width: page.labelWidth; text: "API 协议"; horizontalAlignment: Text.AlignRight; anchors.verticalCenter: parent.verticalCenter; color: Theme.color.textSecondary; font.family: Theme.font.uiFamily; font.pixelSize: Theme.font.caption }
                                ComboBox {
                                    id: customProtocolCombo
                                    width: parent.width - page.labelWidth - parent.spacing
                                    height: page.fieldHeight
                                    model: [
                                        { label: "OpenAI Chat Completions", value: "openai-completions" },
                                        { label: "OpenAI Responses", value: "openai-responses" },
                                        { label: "Anthropic Messages", value: "anthropic-messages" }
                                    ]
                                    textRole: "label"
                                    valueRole: "value"   // 界面显示 label，提交给后端的是 value（provider 配置里的协议枚举）
                                    font.family: Theme.font.uiFamily
                                    font.pixelSize: Theme.font.body
                                    leftPadding: Theme.spacing.fieldPadX
                                    rightPadding: Theme.spacing.fieldPadX + Theme.size.dropdownArrowWidth

                                    HoverHandler { id: customProtocolHover; cursorShape: Qt.PointingHandCursor }

                                    contentItem: Text {
                                        leftPadding: 10
                                        rightPadding: 28
                                        text: customProtocolCombo.displayText
                                        elide: Text.ElideRight
                                        verticalAlignment: Text.AlignVCenter
                                        color: Theme.color.textPrimary
                                        font.family: Theme.font.uiFamily
                                        font.pixelSize: Theme.font.body
                                    }

                                    indicator: LineIcon {
                                        x: customProtocolCombo.width - width - 9
                                        anchors.verticalCenter: parent.verticalCenter
                                        width: 12
                                        height: 12
                                        name: "chevron-down"
                                        size: 12
                                        color: customProtocolCombo.activeFocus
                                            ? Theme.color.textPrimary
                                            : Theme.color.textSecondary
                                        rotation: customProtocolCombo.popup.visible ? 180 : 0
                                    }

                                    background: Rectangle {
                                        color: Theme.color.panelBg
                                        border.width: 1
                                        border.color: customProtocolCombo.activeFocus
                                            ? Theme.color.textSecondary
                                            : (customProtocolHover.hovered ? Theme.color.scrollbarHover : Theme.color.inputBorder)
                                        radius: Theme.radius.control
                                    }

                                    popup: Popup {
                                        y: customProtocolCombo.height + 4
                                        width: customProtocolCombo.width
                                        implicitHeight: Math.min(contentItem.implicitHeight + 12, 240)
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
                                            model: customProtocolCombo.delegateModel
                                            currentIndex: customProtocolCombo.highlightedIndex
                                            ScrollBar.vertical: AppScrollBar { }
                                        }
                                    }

                                    delegate: MenuRow {
                                        required property var modelData
                                        required property int index
                                        width: ListView.view ? ListView.view.width : 0
                                        title: modelData.label
                                        checked: customProtocolCombo.currentIndex === index
                                        onChosen: {
                                            customProtocolCombo.currentIndex = index
                                            customProtocolCombo.popup.close()
                                        }
                                    }
                                }
                            }
                            Row { width: parent.width; height: page.fieldHeight; spacing: 8
                                Text { width: page.labelWidth; text: "模型 ID"; horizontalAlignment: Text.AlignRight; anchors.verticalCenter: parent.verticalCenter; color: Theme.color.textSecondary; font.family: Theme.font.uiFamily; font.pixelSize: Theme.font.caption }
                                FieldBox { id: customModelIdField; width: parent.width - page.labelWidth - parent.spacing; height: page.fieldHeight; placeholder: "gpt-5" }
                            }
                            Row { width: parent.width; height: page.fieldHeight; spacing: 8
                                Text { width: page.labelWidth; text: "模型名称"; horizontalAlignment: Text.AlignRight; anchors.verticalCenter: parent.verticalCenter; color: Theme.color.textSecondary; font.family: Theme.font.uiFamily; font.pixelSize: Theme.font.caption }
                                FieldBox { id: customModelNameField; width: parent.width - page.labelWidth - parent.spacing; height: page.fieldHeight; placeholder: "可选" }
                            }
                            Row { width: parent.width; height: page.fieldHeight; spacing: 8
                                Text { width: page.labelWidth; text: "API Key"; horizontalAlignment: Text.AlignRight; anchors.verticalCenter: parent.verticalCenter; color: Theme.color.textSecondary; font.family: Theme.font.uiFamily; font.pixelSize: Theme.font.caption }
                                FieldBox { id: customKeyField; width: parent.width - page.labelWidth - parent.spacing; height: page.fieldHeight; placeholder: "可留空"; password: true }   // 可留空：不填就不写该路由的凭据
                            }
                            // 保存按钮右对齐；成功/失败就地显示在下方反馈行，不进全局状态行。
                            Row { width: parent.width; height: page.fieldHeight; spacing: 8; layoutDirection: Qt.RightToLeft
                                IconButton { height: page.fieldHeight; label: "保存提供商"; onClicked: page.api.createCustomProvider(customRouteField.text, customNameField.text, customBaseUrlField.text, customProtocolCombo.currentValue, customModelIdField.text, customModelNameField.text, customKeyField.text) }
                            }
                            Text { width: parent.width; text: page.api.formFeedback; visible: text.length > 0; wrapMode: Text.WordWrap; color: Theme.color.textSecondary; font.family: Theme.font.uiFamily; font.pixelSize: Theme.font.caption }
                        }

                        Row {   // 2 模型 ID + 获取模型
                            visible: !page.isCustomProvider
                            width: parent.width
                            spacing: 8

                            Text {
                                width: page.labelWidth
                                text: qsTrId("model_id_label")
                                horizontalAlignment: Text.AlignRight
                                anchors.verticalCenter: parent.verticalCenter
                                color: Theme.color.textSecondary
                                font.family: Theme.font.uiFamily
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
                                    // 未选路由时传空串占位，避免按下标取 provider 越界。
                                    page.api.discover(providerCombo.currentIndex >= 0 ? page.api.providers[providerCombo.currentIndex].provider : "", apiKeyField.text)
                                }
                            }
                        }

                        Column {   // 候选模型（原版是独立弹层；就地铺在 ID 输入框下方，点一条回填）
                            visible: !page.isCustomProvider && page.candidatesVisible && page.api.candidates.length > 0
                            width: parent.width
                            leftPadding: page.labelWidth + 8
                            spacing: 2

                            Repeater {
                                model: page.api.candidates   // discover 拉回的 { id } 列表，只用来回填 idField

                                delegate: Rectangle {   // 单行候选：悬停高亮，整行可点
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
                                        font.family: Theme.font.uiFamily
                                        font.pixelSize: Theme.font.caption
                                    }

                                    MouseArea {
                                        id: optionMouse
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: {
                                            idField.text = modelData.id
                                            page.candidatesVisible = false   // 选中即收起列表，不自动提交表单
                                        }
                                    }
                                }
                            }
                        }

                        Row {   // 3 显示名称
                            visible: !page.isCustomProvider
                            width: parent.width
                            spacing: 8

                            Text {
                                width: page.labelWidth
                                text: qsTrId("model_display_name")
                                horizontalAlignment: Text.AlignRight
                                anchors.verticalCenter: parent.verticalCenter
                                color: Theme.color.textSecondary
                                font.family: Theme.font.uiFamily
                                font.pixelSize: Theme.font.caption
                            }

                            FieldBox {
                                id: nameField
                                width: parent.width - page.labelWidth - parent.spacing
                                placeholder: qsTrId("model_display_name_hint")
                            }
                        }

                        Row {   // 4 上下文窗口
                            visible: !page.isCustomProvider
                            width: parent.width
                            spacing: 8

                            Text {
                                width: page.labelWidth
                                text: qsTrId("model_context_window")
                                horizontalAlignment: Text.AlignRight
                                anchors.verticalCenter: parent.verticalCenter
                                color: Theme.color.textSecondary
                                font.family: Theme.font.uiFamily
                                font.pixelSize: Theme.font.caption
                            }

                            FieldBox {
                                id: contextField
                                width: parent.width - page.labelWidth - parent.spacing
                                placeholder: qsTrId("model_context_window_hint")
                                numeric: true   // 只收数字：留空表示未声明，与填 0 区分
                            }
                        }

                        Row {   // 5 最大输出
                            visible: !page.isCustomProvider
                            width: parent.width
                            spacing: 8

                            Text {
                                width: page.labelWidth
                                text: qsTrId("model_max_output")
                                horizontalAlignment: Text.AlignRight
                                anchors.verticalCenter: parent.verticalCenter
                                color: Theme.color.textSecondary
                                font.family: Theme.font.uiFamily
                                font.pixelSize: Theme.font.caption
                            }

                            FieldBox {
                                id: maxTokensField
                                width: parent.width - page.labelWidth - parent.spacing
                                placeholder: qsTrId("model_max_output_hint")
                                numeric: true   // 同上：空值即不写回 maxTokens
                            }
                        }

                        Row {   // 6 思考档位（仅 pi-ai 路由显示，与原版一致）
                            visible: !page.isCustomProvider && page.currentProvider !== null
                                && page.api.supportsThinkingLevels()
                            width: parent.width
                            spacing: 8

                            Text {
                                width: page.labelWidth
                                text: qsTrId("model_think_level")
                                horizontalAlignment: Text.AlignRight
                                anchors.verticalCenter: parent.verticalCenter
                                color: Theme.color.textSecondary
                                font.family: Theme.font.uiFamily
                                font.pixelSize: Theme.font.caption
                            }

                            FieldBox {
                                id: effortsField
                                width: parent.width - page.labelWidth - parent.spacing
                                placeholder: qsTrId("model_think_level_hint")   // 自由文本档位，留空即不声明
                            }
                        }

                        Row {   // 7 输入模态（仅非 pi-ai 路由显示）
                            visible: !page.isCustomProvider && page.currentProvider !== null
                                && !page.api.supportsThinkingLevels()
                            width: parent.width
                            spacing: 8

                            Text {
                                width: page.labelWidth
                                text: qsTrId("model_input_modality")
                                horizontalAlignment: Text.AlignRight
                                anchors.verticalCenter: parent.verticalCenter
                                color: Theme.color.textSecondary
                                font.family: Theme.font.uiFamily
                                font.pixelSize: Theme.font.caption
                            }

                            // 勾选控件：Basic 默认指示器 28×28 方角 + 1px 平台灰描边，键盘焦点描边取
                            // palette.highlight（ThemeManager 装的是强调蓝）。悬停浅灰底、选中 = textPrimary
                            // 近黑面 + 反相勾（panelBg），禁用退回 border 描边。整体高 = 18 + 上下 padding 12 = 30px。
                            CheckBox {
                                id: imageInputBox
                                text: qsTrId("model_supports_image")
                                font.family: Theme.font.uiFamily
                                font.pixelSize: Theme.font.body
                                spacing: Theme.spacing.labelGap
                                palette.windowText: Theme.color.textPrimary   // 标签色/高亮色也钉在 token 上：不再受全局 QPalette 影响（深色主题下尤其重要）
                                palette.text: Theme.color.textPrimary
                                palette.highlight: Theme.color.textSecondary
                                palette.highlightedText: Theme.color.pageBg || Theme.color.panelBg

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

                                    CheckMark {
                                        anchors.centerIn: parent
                                        visible: imageInputBox.checkState === Qt.Checked
                                        size: 11
                                        color: Theme.color.pageBg || Theme.color.panelBg
                                    }
                                }
                            }
                        }

                        Row {   // 8 API Key（原版是字面量标签，无 id）
                            visible: !page.isCustomProvider && !(page.currentProvider !== null && page.currentProvider.noApiKey === true)
                            width: parent.width
                            spacing: 8

                            Text {
                                width: page.labelWidth
                                text: "API Key"
                                horizontalAlignment: Text.AlignRight
                                anchors.verticalCenter: parent.verticalCenter
                                color: Theme.color.textSecondary
                                font.family: Theme.font.uiFamily
                                font.pixelSize: Theme.font.caption
                            }

                            FieldBox {
                                id: apiKeyField
                                width: parent.width - page.labelWidth - parent.spacing
                                placeholder: page.credentialReadonly()
                                    ? qsTrId("model_credential_readonly_hint")   // 只读路由：直接说明写不进去
                                    : qsTrId("model_credential_ref_hint")
                                password: true
                            }
                        }

                        Text {   // 当前路由的凭据状态：与展开卡片里的那一行同一套文案
                            width: parent.width
                            text: page.credentialText(page.currentProvider)
                            wrapMode: Text.WordWrap
                            color: Theme.color.textCaption
                            font.family: Theme.font.uiFamily
                            font.pixelSize: Theme.font.small
                        }

                        Text {   // 路由说明：key 从哪来、是否走账号登录；无可用路由时给提示
                            width: parent.width
                            text: page.currentProvider
                                ? page.api.routeHint(page.currentProvider.provider)
                                : qsTrId("model_no_configurable_provider")
                            wrapMode: Text.WordWrap
                            color: Theme.color.textTertiary
                            font.family: Theme.font.uiFamily
                            font.pixelSize: Theme.font.caption
                        }

                        Text {
                            width: parent.width
                            text: page.api.formFeedback
                            wrapMode: Text.WordWrap
                            color: Theme.color.textSecondary   // 表单反馈：原版走强调色（蓝）⇒ 中性灰
                            font.family: Theme.font.uiFamily
                            font.pixelSize: Theme.font.caption
                        }

                        // 右对齐：layoutDirection 反转后「取消」在左、「添加」在右。
                        Row {
                            width: parent.width
                            spacing: 6
                            layoutDirection: Qt.RightToLeft

                            IconButton {
                                visible: !page.isCustomProvider
                                id: submitButton
                                height: page.fieldHeight
                                label: qsTrId("common_add")
                                onClicked: {
                                    const provider = page.currentProvider
                                    if (!provider)
                                        return   // 选到「自定义…」时提交按钮不可见，这次返回只是兜底
                                    page.api.submitModelForm(provider.provider, idField.text, nameField.text, contextField.text, maxTokensField.text, effortsField.text, apiKeyField.text)
                                }
                            }

                            IconButton {
                                id: cancelButton
                                height: page.fieldHeight
                                label: qsTrId("common_cancel")
                                onClicked: page.formOpen = false   // 只收起表单，保留已填内容
                            }
                        }
                    }
                }
            }
        }

        // 底部动作条固定在滚动区之外：高度参与滚动区高度计算，不随内容滚动。
        Item {
            id: actionFooter
            width: parent.width
            height: 34

            ActionButton {
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                width: 116
                height: 30
                // 新增入口与表单都在底部，不进标题栏。
                text: page.formOpen ? qsTrId("common_cancel") : qsTrId("model_add")
                variant: page.formOpen ? "ghost" : "elevated"   // 表单打开时按钮退为次要样式
                enabled: page.formOpen || page.api.writable
                tooltip: enabled ? "" : qsTrId("model_add_requires_api")
                onClicked: {
                    if (page.formOpen) {
                        page.formOpen = false
                        page.candidatesVisible = false   // 关表单顺手收起候选列表
                        return
                    }
                    page.formOpen = true
                    page.candidatesVisible = false
                    Qt.callLater(function() {
                        // 等布局完成再滚：此时 contentHeight 已含新展开的表单高度。
                        scroller.contentY = Math.max(0, scroller.contentHeight - scroller.height)
                    })
                }
            }
        }
    }

    // 新增表单所选路由的凭据是否只读（由 profile / 环境提供时写不进去）。
    function credentialReadonly() {
        const row = page.currentProvider
        return row !== null && row.keyKnown && !row.keyWritable
    }
}
