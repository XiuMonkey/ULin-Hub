import QtQuick
import ULinHub 1.0
import "../../widgets"

// 设置 · DeepSeek 官方账号：资料、余额、登录/退出。
Item {
    id: page

    Component.onCompleted: Hub.settings.refreshAccount()

    Column {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        spacing: 10

        Text {
            text: "账户"
            color: Theme.color.textPrimary
            font.family: "Microsoft YaHei UI"
            font.pixelSize: Theme.font.title
            font.weight: Theme.font.weightBold
        }

        Row {
            width: parent.width
            spacing: 10

            Rectangle {
                width: 46
                height: 46
                radius: 23
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
                spacing: 2
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
                        : "使用浏览器登录后可查看余额和用量"
                    color: Theme.color.textSecondary
                    font.family: "Microsoft YaHei UI"
                    font.pixelSize: Theme.font.caption
                }
            }
        }

        Rectangle { width: parent.width; height: 1; color: Theme.color.border }

        Text {
            width: parent.width
            text: "余额"
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
            visible: !Hub.settings.account.balanceText && Hub.settings.account.signedIn
                && !Hub.settings.account.busy
            text: "暂时拿不到余额，请稍后刷新"
            color: Theme.color.textTertiary
            font.family: "Microsoft YaHei UI"
            font.pixelSize: Theme.font.caption
        }

        Row {
            spacing: 8

            ActionButton {
                width: 92
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
                width: 92
                height: 32
                text: "刷新"
                variant: "ghost"
                visible: !Hub.settings.account.busy
                onClicked: Hub.settings.refreshAccount()
            }

            ActionButton {
                width: 92
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
                width: 112
                height: 30
                text: "查看用量"
                variant: "ghost"
                onClicked: Hub.settings.openAccountUsage()
            }
            ActionButton {
                width: 112
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
