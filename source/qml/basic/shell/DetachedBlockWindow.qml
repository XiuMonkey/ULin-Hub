// 单个右栏块脱出的独立窗口：内容仍走 PanelBarDock 的 kind→view 路由。
// 无边框窗口自己画标题栏、圆角边框、窗口按钮和拖动区。
import QtQuick
import QtQuick.Window
import ULinHub 1.0
import "../../widgets"

Window {
    id: win

    // 不挂到主窗口的 transient 链：否则 Windows 把副窗口当 tool/transient，
    // 任务栏没有入口，最小化也只会缩成贴在主窗口上的条。
    transientParent: null
    flags: Qt.Window | Qt.FramelessWindowHint
    color: "transparent"

    required property string blockId
    property var info: ({})

    readonly property bool maximized: win.visibility === Window.Maximized
    readonly property color frameBorder: Theme.isDark ? "#2E3034" : "#E3E5E9"

    Component.onCompleted: {
        info = PanelBarDock.blockInfo(blockId)
        WindowBridge.markAppWindow(win)
    }
    onBlockIdChanged: info = PanelBarDock.blockInfo(blockId)

    width: 640
    height: 480
    minimumWidth: 360
    minimumHeight: 240
    visible: true
    title: info && info.title ? info.title : qsTrId("panelBar_title")

    onClosing: {
        // 独立窗口的关闭 = 关闭这个块本身，不再把它塞回右栏。
        if (info && info.kind === "terminal")
            Terminal.closeBlock(blockId)
        PanelBarDock.discardDetachedBlock(blockId)
    }

    Rectangle {
        id: frame
        anchors.fill: parent
        radius: win.maximized ? 0 : 12
        color: Theme.color.panelBg
        border.width: win.maximized ? 0 : 1
        border.color: win.frameBorder
        clip: true

        // ---------------- 自绘标题栏 ----------------
        Item {
            id: titleBar
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: 34
            z: 20

            Rectangle {
                anchors.fill: parent
                radius: win.maximized ? 0 : 11
                color: Theme.color.panelBg
            }

            Row {
                anchors.left: parent.left
                anchors.leftMargin: 10
                anchors.verticalCenter: parent.verticalCenter
                spacing: 6

                LineIcon {
                    anchors.verticalCenter: parent.verticalCenter
                    name: win.info && win.info.kind === "terminal" ? "terminal"
                        : win.info && win.info.kind === "browser" ? "browser" : "file"
                    size: 13
                    color: Theme.color.textTertiary
                }

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: win.info && win.info.title ? win.info.title : qsTrId("panelBar_title")
                    color: Theme.color.textPrimary
                    font.family: "Microsoft YaHei UI"
                    font.pixelSize: Theme.font.caption
                    font.weight: Theme.font.weightMedium
                    elide: Text.ElideRight
                }
            }

            Row {
                anchors.right: parent.right
                anchors.rightMargin: 6
                anchors.verticalCenter: parent.verticalCenter
                spacing: 2
                z: 30

                IconButton {
                    boxSize: 24
                    iconSize: 11
                    iconSource: "qrc:/ULinHub/icons/ui/window-minimize.svg"
                    iconColor: Theme.color.textSecondary
                    tooltip: qsTrId("common_minimize")
                    onClicked: win.showMinimized()
                }

                IconButton {
                    boxSize: 24
                    iconSize: 11
                    iconSource: win.maximized
                        ? "qrc:/ULinHub/icons/ui/window-restore.svg"
                        : "qrc:/ULinHub/icons/ui/window-maximize.svg"
                    iconColor: Theme.color.textSecondary
                    tooltip: qsTrId("common_maximize")
                    onClicked: win.maximized ? win.showNormal() : win.showMaximized()
                }

                IconButton {
                    boxSize: 24
                    iconSize: 11
                    iconSource: "qrc:/ULinHub/icons/ui/window-close.svg"
                    iconColor: Theme.color.textSecondary
                    tooltip: qsTrId("common_close")
                    onClicked: win.close()
                }
            }

            // 标题栏拖动：交给系统 move loop，QML 不逐帧写 x/y，跟手才顺。
            MouseArea {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                anchors.rightMargin: 82
                acceptedButtons: Qt.LeftButton
                onPressed: (mouse) => {
                    if (!win.maximized)
                        WindowBridge.startWindowMove(win)
                }
                onDoubleClicked: {
                    if (win.maximized)
                        win.showNormal()
                    else
                        win.showMaximized()
                }
            }
        }

        // ---------------- 内容 ----------------
        Loader {
            id: blockLoader
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: titleBar.bottom
            anchors.bottom: parent.bottom
            anchors.margins: 6
            source: info && info.viewPath ? Style.url(info.viewPath) : ""

            onLoaded: {
                if (!item || !win.info)
                    return
                if (win.info.viewBlockScoped)
                    item.blockId = win.blockId
                if (typeof item.openExternal === "function")
                    item.startUrl = win.info.url
            }
        }

        PanelPlaceholder {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: titleBar.bottom
            anchors.bottom: parent.bottom
            anchors.margins: 6
            visible: !win.info || !win.info.viewPath
            title: win.info && win.info.title ? win.info.title : qsTrId("panelBar_title")
            detail: win.info ? win.info.viewDetail : ""
        }

        // 无边框窗口的四边/四角 resize：同样交系统 sizing loop，避免逐帧改 QML 尺寸抖动。
        MouseArea { anchors.left: parent.left; anchors.top: parent.top; anchors.bottom: parent.bottom; width: 5; enabled: !win.maximized; cursorShape: Qt.SizeHorCursor; onPressed: WindowBridge.startWindowResize(win, Qt.LeftEdge) }
        MouseArea { anchors.right: parent.right; anchors.top: parent.top; anchors.bottom: parent.bottom; width: 5; enabled: !win.maximized; cursorShape: Qt.SizeHorCursor; onPressed: WindowBridge.startWindowResize(win, Qt.RightEdge) }
        MouseArea { anchors.top: parent.top; anchors.left: parent.left; anchors.right: parent.right; height: 5; enabled: !win.maximized; cursorShape: Qt.SizeVerCursor; onPressed: WindowBridge.startWindowResize(win, Qt.TopEdge) }
        MouseArea { anchors.bottom: parent.bottom; anchors.left: parent.left; anchors.right: parent.right; height: 5; enabled: !win.maximized; cursorShape: Qt.SizeVerCursor; onPressed: WindowBridge.startWindowResize(win, Qt.BottomEdge) }
        MouseArea { anchors.left: parent.left; anchors.top: parent.top; width: 10; height: 10; enabled: !win.maximized; cursorShape: Qt.SizeFDiagCursor; onPressed: WindowBridge.startWindowResize(win, Qt.LeftEdge | Qt.TopEdge) }
        MouseArea { anchors.right: parent.right; anchors.top: parent.top; width: 10; height: 10; enabled: !win.maximized; cursorShape: Qt.SizeBDiagCursor; onPressed: WindowBridge.startWindowResize(win, Qt.RightEdge | Qt.TopEdge) }
        MouseArea { anchors.left: parent.left; anchors.bottom: parent.bottom; width: 10; height: 10; enabled: !win.maximized; cursorShape: Qt.SizeBDiagCursor; onPressed: WindowBridge.startWindowResize(win, Qt.LeftEdge | Qt.BottomEdge) }
        MouseArea { anchors.right: parent.right; anchors.bottom: parent.bottom; width: 10; height: 10; enabled: !win.maximized; cursorShape: Qt.SizeFDiagCursor; onPressed: WindowBridge.startWindowResize(win, Qt.RightEdge | Qt.BottomEdge) }
    }
}
