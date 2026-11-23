// 设置 · 外观设置（按原版 Settings.cpp 的 appearancePanel 复刻）：
// 说明 settings_appearance_desc → 语言标签 settings_language_label + 下拉（第 0 项"跟随系统"）
// → 语言反馈 → 重置样式按钮 settings_reset_styles → 样式反馈。
// 语言切换立即生效（原版：Translation::apply 后 Qt 给各控件发 LanguageChange）。

import QtQuick
import QtQuick.Controls.Basic
import ULinHub 1.0
import "../../widgets"

Item {
    id: page

    Column {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        spacing: 6

        // 页面标题：这页原本一进来就是 12px 灰说明，从"设置"(15) 直接掉到 12，中间是空的。
        Text {
            text: qsTrId("settings_appearance_tab")
            color: Theme.color.textPrimary
            font.family: Theme.font.uiFamily
            font.pixelSize: Theme.font.title
            font.weight: Theme.font.weightBold
            bottomPadding: 2
        }

        Text {
            width: parent.width
            text: qsTrId("settings_appearance_desc")
            wrapMode: Text.WordWrap
            color: Theme.color.textSecondary
            font.family: Theme.font.uiFamily
            font.pixelSize: Theme.font.caption
        }

        Text {
            // 原版 settings.qss 把 #settingsAppearanceLabel 与 Hint/Feedback 放在同一组规则里：
            // transparent 底、{{textSecondary}}、font-size 12px。
            text: qsTrId("settings_language_label")
            color: Theme.color.textSecondary
            font.family: Theme.font.uiFamily
            font.pixelSize: Theme.font.caption
            topPadding: 4   // 与上面那段说明拉开：同号同色，不加间距会读成同一段
        }

        ComboBox {
            id: languageCombo

            // 原版 settings.qss#settingsLanguageCombo：inputBg 底 / 1px scrollbar 描边 / radius 8px /
            // padding 6px 10px / 13px / min-width 200px；:hover → accent 描边 / ::drop-down 无边框宽 18px。
            // 两处有意偏离（去蓝）：:hover 描边改用 scrollbarHover；弹出列表的高亮角色改中性 token
            // （原版没有 QComboBox QAbstractItemView 规则，走 QPalette 的 Highlight 蓝，会蓝底白字）。
            width: Math.max(200, parent.width)       // 原版 min-width: 200px
            height: 32
            leftPadding: 10                          // 原版 padding-left: 10px（Control 的 padding 属性）
            rightPadding: 30                         // 给右侧下拉箭头留位
            model: Hub.settings.languages
            textRole: "name"
            font.family: Theme.font.uiFamily
            font.pixelSize: Theme.font.body           // 原版 font-size: 13px

            // Basic 风格的 ComboBox 内容项默认吃 QPalette；深色主题下会变成黑字。
            // 内容项与 palette 都钉在主题 token 上。
            palette.windowText: Theme.color.textPrimary
            palette.text: Theme.color.textPrimary
            palette.buttonText: Theme.color.textPrimary

            contentItem: Text {
                leftPadding: 10
                rightPadding: 30
                text: languageCombo.displayText
                elide: Text.ElideRight
                verticalAlignment: Text.AlignVCenter
                color: Theme.color.textPrimary
                font.family: Theme.font.uiFamily
                font.pixelSize: Theme.font.body
            }

            Component.onCompleted: syncFromSettings()   // 选中项跟随已保存的语言（空串 = 跟随系统）
            Connections {   // 页内同步，不登记（登记等于把 id 暴露成可被接管的目标）
                target: Hub.settings
                function onLanguageChanged() {
                    languageCombo.syncFromSettings()
                }
            }

            function syncFromSettings() {
                const rows = Hub.settings.languages
                for (let i = 0; i < rows.length; ++i) {
                    if (rows[i].code === Hub.settings.languageCode) {
                        currentIndex = i
                        return
                    }
                }
                currentIndex = 0
            }

            onActivated: (index) => {
                const rows = Hub.settings.languages
                if (index >= 0 && index < rows.length)
                    Hub.settings.setLanguage(rows[index].code)
            }

            // 悬停描边深一档（scrollbarHover）。用 HoverHandler 取悬停态并设手型光标：它不接按键，
            // 不像铺一层 MouseArea 那样有拦点击的风险（ComboBox 自身没有 cursorShape）。
            HoverHandler {
                id: comboHover
                cursorShape: Qt.PointingHandCursor
            }

            // 原版 ::drop-down { border: none; width: 18px }：右侧一个小三角、无边框；Qt Basic 默认画
            // 上下双箭头（⌃⌄），所以只覆盖 indicator 的形状，其余保持 Basic 结构。
            indicator: Text {
                x: languageCombo.width - 26
                y: Math.round((languageCombo.height - height) / 2)
                width: 18
                horizontalAlignment: Text.AlignHCenter
                text: "\u25BE"
                color: Theme.color.textSecondary
                font.pixelSize: Theme.font.small
            }

            background: Rectangle {
                color: Theme.color.inputBg
                // 静息对齐官方 DSH --dsw-alias-border-l2，悬停深一档
                border.width: 1
                border.color: comboHover.hovered ? Theme.color.scrollbarHover : Theme.color.inputBorder
                radius: Theme.radius.control
            }

            // 弹出列表的行：ItemDelegate 在 Basic 风格里高亮底取 palette.light、高亮字取 palette.highlightedText
            // （全局装的是 Highlight=强调蓝 / HighlightedText=白，高亮行会变成浅底白字）。本页只改这两个角色为
            // 中性 token，不动 ItemDelegate 自己的 contentItem/background 结构（结构一改就静默失效）。
            delegate: ItemDelegate {
                id: languageOption

                required property var modelData
                required property int index

                width: ListView.view ? ListView.view.width : 0
                text: languageOption.modelData.name
                font.family: Theme.font.uiFamily
                font.pixelSize: Theme.font.body

                palette.text: Theme.color.textPrimary
                palette.light: Theme.color.activeBg          // 高亮行底色（中性灰）
                palette.highlightedText: Theme.color.textPrimary
            }

            // 弹层：走 QPalette（方角 + 平台描边），给一个与设置页其它面成套的圆角中性面。
            // contentItem 仍照 Basic 风格那份 ListView 写（delegateModel + highlightedIndex）。
            popup: Popup {
                y: languageCombo.height + 2
                width: languageCombo.width
                implicitHeight: Math.min(contentItem.implicitHeight + 8, 260)
                padding: 4

                background: Rectangle {
                    radius: Theme.radius.control
                    color: Theme.color.menuBg
                    border.width: 1
                    border.color: Theme.color.menuBorder
                }

                contentItem: ListView {
                    clip: true
                    implicitHeight: contentHeight
                    model: languageCombo.delegateModel
                    currentIndex: languageCombo.highlightedIndex
                    ScrollBar.vertical: AppScrollBar { }
                }
            }
        }

        Text {
            width: parent.width
            visible: Hub.settings.appearanceFeedback.length > 0
            text: Hub.settings.appearanceFeedback
            wrapMode: Text.WordWrap

            // 原版 #settingsAppearanceFeedback：{{textSecondary}} + 12px（与上面标签同组规则），
            // 不是强调色。
            color: Theme.color.textSecondary
            font.family: Theme.font.uiFamily
            font.pixelSize: Theme.font.caption
        }

        // 原版是 QPushButton#settingsStylesResetButton（与 settingsServerSaveButton 同组规则）：
        // primaryButtonBg/Text、无边框、radius 8px、padding 8px 14px，hover/pressed/disabled 各一档，手型光标。
        Item { height: 2 }   // Column 的 spacing 是全列统一的，单独给主按钮让一点位

        ActionButton {
            width: parent.width
            text: qsTrId("settings_reset_styles")
            // 设置页的重置不是全局主操作：用 elevated 中性面，避免 dark 主题下
            // primary（近白面）在深色设置页里像一块游离的白色按钮。
            variant: "elevated"
            onClicked: Hub.settings.resetStyles()
        }
    }
}
