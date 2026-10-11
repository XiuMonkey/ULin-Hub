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
            // 原版**没有**定制弹出列表（旧壳 QSS 里没有 QComboBox QAbstractItemView 规则），
            // 所以这里也不覆盖 contentItem / delegate / popup / indicator —— 那是"超出原版"的改动，
            // 而且会静默失效：我第一次把 contentItem 换成自定义 Text 后，框里文字与选项全没了，
            // 日志里却一条错误都没有。
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

            // 原版 :hover 只把描边换成 {{accent}}。用 HoverHandler 取悬停态并顺手设手型光标：
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
                border.color: comboHover.hovered ? Theme.color.accent : Theme.color.scrollbar
                radius: Theme.radius.control
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
