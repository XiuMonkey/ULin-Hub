// 设置 · 外观设置（按原版 Settings.cpp 的 appearancePanel 复刻）：
//   说明 settings_appearance_desc → 语言标签 settings_language_label + 下拉（第 0 项"跟随系统"）
//   → 语言反馈 → 重置样式按钮 settings_reset_styles → 样式反馈。
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

        Text {
            width: parent.width
            text: qsTrId("settings_appearance_desc")
            wrapMode: Text.WordWrap
            color: Theme.color.textSecondary
            font.family: "Microsoft YaHei UI"
            font.pixelSize: Theme.font.caption
        }

        Text {
            // 原版 settings.qss 把 #settingsAppearanceLabel 与 Hint/Feedback 放在同一组规则里：
            // transparent 底、{{textSecondary}}、font-size 12px。
            // 也就是说这个标签不是 14px 的正文标题色，之前按正文样式做是不一致的。
            text: qsTrId("settings_language_label")
            color: Theme.color.textSecondary
            font.family: "Microsoft YaHei UI"
            font.pixelSize: Theme.font.caption
        }

        ComboBox {
            id: languageCombo
            // 只复刻原版对"关着的那个框"的定制（settings.qss#settingsLanguageCombo）：
            //   background {{inputBg}} / border 1px {{scrollbar}} / border-radius 8px
            //   padding 6px 10px / font-size 13px / min-width 200px
            //   :hover → border-color {{accent}} / ::drop-down 无边框宽 18px
            //
            // ⚠️ 与旧壳的两处有意偏离（都是"去蓝/现代化"要求，纯观感）：
            //   * :hover 描边：原版取 {{accent}}（强调蓝），这里取 scrollbarHover（中性灰）；
            //   * 弹出列表：原版没有 QComboBox QAbstractItemView 规则（等于走 QPalette：Highlight=强调蓝、
            //     HighlightedText=textOnAccent 白 ⇒ Basic 风格拿 palette.light 当高亮底、palette.highlightedText
            //     当高亮字色，白底白字），这里只把这两个调色板角色换成中性 token + 给弹层一个圆角中性面。
            // 仍然**不覆盖 contentItem**：第一次把 contentItem 换成自定义 Text 后，框里文字与选项全没了，
            // 日志里却一条错误都没有 —— 那个坑与 delegate/popup 无关，但记着别再踩。
            width: Math.max(200, parent.width)       // 原版 min-width: 200px
            height: 32
            leftPadding: 10                          // 原版 padding-left: 10px（Control 的 padding 属性）
            rightPadding: 30                         // 给右侧下拉箭头留位
            model: Hub.settings.languages
            textRole: "name"
            font.family: "Microsoft YaHei UI"
            font.pixelSize: Theme.font.body           // 原版 font-size: 13px

            // 选中项跟随已保存的语言（空串 = 跟随系统）
            Component.onCompleted: syncFromSettings()
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

            // 原版 :hover 只把描边换成 {{accent}}（蓝）。按"设置页不出蓝"改成中性灰 scrollbarHover
            // （比静止态的 scrollbar 略深一档，克制）。用 HoverHandler 取悬停态并顺手设手型光标：
            // 它是纯悬停处理器（不接按键），所以不会像铺一层 MouseArea 那样有拦点击的风险
            // （Control/ComboBox 自身没有 cursorShape 属性，那个属性只在 MouseArea/Handler 上）。
            HoverHandler {
                id: comboHover
                cursorShape: Qt.PointingHandCursor
            }

            // 原版 ::drop-down { border: none; width: 18px }：右侧一个小三角、无边框。
            // Qt Basic 默认画的是"上下双箭头"（⌃⌄），与原版不同，所以这里只覆盖 indicator；
            // contentItem / delegate / popup 一概不碰（那三处属于"超出原版"的改动）。
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
                border.width: 1
                // 静息 2026-10-17 改：scrollbar(#D1D5DB ≈17% 灰) → inputBorder(10% 黑)，
                // 对齐官方 DSH `--dsw-alias-border-l2` 的取法；悬停仍深一档（克制的中性灰）。
                border.color: comboHover.hovered ? Theme.color.scrollbarHover : Theme.color.inputBorder
                radius: Theme.radius.control
            }

            // 弹出列表的行：ItemDelegate 在 Basic 风格里高亮底取 palette.light、高亮字取
            // palette.highlightedText（ThemeManager 全局装的是 Highlight=强调蓝 / HighlightedText=
            // textOnAccent 白 ⇒ 高亮行是"浅底白字"）。这里只改这两个调色板角色为中性 token，
            // 不动 ItemDelegate 自己的 contentItem/background 结构（结构一改就静默失效，见上面的坑）。
            delegate: ItemDelegate {
                id: languageOption

                required property var modelData
                required property int index

                width: ListView.view ? ListView.view.width : 0
                text: languageOption.modelData.name
                font.family: "Microsoft YaHei UI"
                font.pixelSize: Theme.font.body

                palette.text: Theme.color.textPrimary
                palette.light: Theme.color.activeBg          // 高亮行底色（中性灰）
                palette.highlightedText: Theme.color.textPrimary
            }

            // 弹层：旧壳走 QPalette（方角 + 平台描边），这里给一个与设置页其它面成套的圆角中性面。
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
            font.family: "Microsoft YaHei UI"
            font.pixelSize: Theme.font.caption
        }

        // 原版是 QPushButton#settingsStylesResetButton（settings.qss 与 settingsServerSaveButton 同组规则）：
        //   background {{primaryButtonBg}} / color {{primaryButtonText}} / border none
        //   border-radius 8px / padding 8px 14px；:hover→primaryButtonHover、:pressed→primaryButtonPressed、
        //   :disabled→primaryButtonDisabledBg/Text；setCursor(Qt::PointingHandCursor)。
        // ActionButton(variant=primary) 就是这套规格，IconButton 是工具栏小按钮，不该用在这里。
        // 原版在 QVBoxLayout 里横向撑满（QPushButton 默认 Minimum/Fixed 策略），所以这里也撑满。
        ActionButton {
            width: parent.width
            text: qsTrId("settings_reset_styles")
            variant: "primary"
            onClicked: Hub.settings.resetStyles()
        }
    }
}
