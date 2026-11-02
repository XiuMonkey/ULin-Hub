import QtQuick
import ULinHub 1.0
import "../../widgets"

// 账户管理：DeepSeek 官方账号。独立于设置页，由左下角头像打开。
Item {
    id: page
    implicitWidth: 632
    implicitHeight: 432

    Component.onCompleted: Hub.settings.refreshAccount()

    Connections {
        target: Hub.settings
        function onAccountAuthorizeRequested(url) {
            if (url.length > 0) {
                PanelBarDock.openUrl(url, "DeepSeek 登录")
                Hub.openPanelBar()
                Hub.settings.closeAccountPage()
            }
        }
    }

    Item {
        anchors.fill: parent

        Item {
            id: titleRow
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.leftMargin: 20
            anchors.rightMargin: 20
            anchors.topMargin: 16
            implicitHeight: 32

            Text {
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                text: "账户管理"
                color: Theme.color.textPrimary
                font.family: "Microsoft YaHei UI"
                font.pixelSize: 15
                font.weight: Theme.font.weightBold
            }

            IconButton {
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                boxSize: 28
                glyph: "✕"
                tooltip: qsTrId("common_close")
                onClicked: Hub.settings.closeAccountPage()
            }
        }

        Column {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: titleRow.bottom
            anchors.bottom: parent.bottom
            anchors.leftMargin: 20
            anchors.rightMargin: 20
            anchors.topMargin: 12
            anchors.bottomMargin: 20
            spacing: 12

            Row {
                width: parent.width
                spacing: 12

                Rectangle {
                    width: 52
                    height: 52
                    radius: 26
                    color: Theme.color.accent
                    Text {
                        anchors.centerIn: parent
                        text: (Hub.settings.account.name || "D").substring(0, 1)
                        color: Theme.color.textOnAccent
                        font.family: "Microsoft YaHei UI"
                        font.pixelSize: Theme.font.title
                        font.weight: Theme.font.weightBold
                    }
                }

                Column {
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 3
                    Text {
                        text: Hub.settings.account.signedIn
                            ? (Hub.settings.account.name || "DeepSeek 官方账号")
                            : "未登录 DeepSeek 官方账号"
                        color: Theme.color.textPrimary
                        font.family: "Microsoft YaHei UI"
                        font.pixelSize: Theme.font.body
                    }
                    Text {
                        text: Hub.settings.account.signedIn
                            ? (Hub.settings.account.contact || "已连接")
                            : "登录后可查看官方余额和用量"
                        color: Theme.color.textSecondary
                        font.family: "Microsoft YaHei UI"
                        font.pixelSize: Theme.font.caption
                    }
                }
            }

            Rectangle { width: parent.width; height: 1; color: Theme.color.border }

            Text {
                width: parent.width
                text: "官方 DeepSeek 账户"
                color: Theme.color.textPrimary
                font.family: "Microsoft YaHei UI"
                font.pixelSize: Theme.font.body
                font.weight: Theme.font.weightBold
            }

            Text {
                width: parent.width
                visible: Hub.settings.account.balanceText.length > 0
                text: "充值余额  " + Hub.settings.account.balanceText
                color: Theme.color.textPrimary
                font.family: "Microsoft YaHei UI"
                font.pixelSize: Theme.font.body
                wrapMode: Text.WordWrap
            }
            Text {
                width: parent.width
                visible: Hub.settings.account.bonusText.length > 0
                text: "赠送余额  " + Hub.settings.account.bonusText
                color: Theme.color.textSecondary
                font.family: "Microsoft YaHei UI"
                font.pixelSize: Theme.font.caption
                wrapMode: Text.WordWrap
            }
            Text {
                width: parent.width
                visible: Hub.settings.account.signedIn && Hub.settings.account.balanceText.length === 0
                    && !Hub.settings.account.busy
                text: "暂时拿不到余额，请稍后刷新"
                color: Theme.color.textTertiary
                font.family: "Microsoft YaHei UI"
                font.pixelSize: Theme.font.caption
            }
            Text {
                width: parent.width
                visible: !Hub.settings.account.signedIn
                text: "当前未登录。点击下方按钮会打开 DeepSeek 官方登录页。"
                color: Theme.color.textSecondary
                font.family: "Microsoft YaHei UI"
                font.pixelSize: Theme.font.caption
                wrapMode: Text.WordWrap
            }

            Row {
                spacing: 8
                ActionButton {
                    width: 116
                    height: 32
                    text: Hub.settings.account.signedIn ? "退出登录" : "登录 DeepSeek"
                    visible: !Hub.settings.account.busy
                    onClicked: {
                        if (Hub.settings.account.signedIn)
                            Hub.settings.signOutAccount()
                        else
                            Hub.settings.startAccountSignIn()
                    }
                }
                ActionButton {
                    width: 72
                    height: 32
                    text: "刷新"
                    variant: "ghost"
                    visible: !Hub.settings.account.busy
                    onClicked: Hub.settings.refreshAccount()
                }
                ActionButton {
                    width: 96
                    height: 32
                    text: "取消登录"
                    variant: "ghost"
                    visible: Hub.settings.account.attemptPhase.length > 0
                        && Hub.settings.account.attemptPhase !== "succeeded"
                        && Hub.settings.account.attemptPhase !== "failed"
                    onClicked: Hub.settings.cancelAccountSignIn()
                }
            }

            Row {
                spacing: 8
                visible: Hub.settings.account.signedIn
                ActionButton {
                    width: 104
                    height: 30
                    text: "查看用量"
                    variant: "ghost"
                    onClicked: Hub.settings.openAccountUsage()
                }
                ActionButton {
                    width: 88
                    height: 30
                    text: "充值"
                    variant: "ghost"
                    onClicked: Hub.settings.openAccountTopUp()
                }
            }

            Text {
                width: parent.width
                visible: Hub.settings.account.error.length > 0
                text: Hub.settings.account.error
                color: Theme.color.danger
                font.family: "Microsoft YaHei UI"
                font.pixelSize: Theme.font.caption
                wrapMode: Text.WordWrap
            }
        }
    }
}
