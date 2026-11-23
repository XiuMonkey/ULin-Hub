// 整壳覆盖页外壳：遮罩 + "从点击处长出来"的卡片 + 收回播完才卸载。与 AppPopup 同一套机制：
// 一个 0→1 的 t 同时驱动卡片 x/y/宽/高/圆角、遮罩透明度与内容透明度。
// 展开 t 0→1 OutBack（越过终点再回弹 ⇒ 弹出感）；收回 t 1→0 InBack（半途打断也不跳）。
// 起点 = 最近一次鼠标点击位置（32×32 小方块），拿不到就退化成从窗口中心长出来。
// 根是 Item，implicitWidth/implicitHeight = 卡片目标尺寸；关闭仍由 Hub.xxx.closePage() 负责。
// 可选：页面声明 property bool overlayAnimating: false，并把常驻动画（尤其 BusySpinner）门控上它 ——
// 展开/收回那两百多毫秒里转圈先停，否则每帧多花 ~10ms 整窗重绘。

import QtQuick
import ULinHub 1.0

Item {
    id: root

    property bool shown: false   // 由壳层绑到 Hub.xxx.open：true = 展开；false = 原路收回，收完（t 回 0）才卸载页面
    property url source   // 页面文件（Style.url("basic/...")）
    property real cardRadius: Theme.radius.popup
    // 功能性页面（设置/扩展）不需要从点击处长出来的展开/收回动画。
    property bool animate: true
    // 仅本次打开不播展开动画（切换 Agent 后自动打开新设置用）；关闭仍走正常收回动画。
    property bool openInstant: false
    property bool pendingOpenInstant: false
    // 切换 Agent 时旧设置直接收掉，不播收回动画；普通关闭仍动画。
    property bool closeInstant: false

    // 阴影与转圈叠加是相乘关系，展开期间先关掉；静止时仍要阴影，所以用 overlayAnimating 门控而不是改默认值。
    property bool cardShadow: !overlayAnimating
    signal pageLoaded(var page)   // 页面装载完成时抛出去（有的页要接它的信号，例如 closeRequested）

    readonly property var pageItem: content.item
    readonly property real targetW: pageItem ? pageItem.implicitWidth : 0   // 卡片目标尺寸：由页面自己声明，尺寸只留一处
    readonly property real targetH: pageItem ? pageItem.implicitHeight : 0

    property real t: 0
    property rect startRect: Qt.rect(0, 0, 0, 0)
    property real dragOffsetX: 0   // 抓手拖动卡片用的偏移：卡片的 x/y 是展开动画算出来的，拖动只能叠在它上面
    property real dragOffsetY: 0
    readonly property int originSide: 32   // 与演示扩展那颗 32×32 入口按钮同值
    readonly property real targetX: Math.round((width - targetW) / 2)
    readonly property real targetY: Math.round((height - targetH) / 2)

    // 卡片几何正在动（展开或收回）：装载的页面若声明同名属性，外壳会把它绑上去，
    // 页面据此按停自己一直在跑的东西（首要是 BusySpinner），避免展开动画被整窗重绘拖慢。
    readonly property bool overlayAnimating: t > 0.001 && t < 0.999

    visible: shown || t > 0.001

    NumberAnimation {   // 展开 / 收回都由 JS 起停（理由同 AppPopup：Transition 里写死 from 会在打断时跳）
        id: expandAnim
        target: root
        property: "t"
        from: 0
        to: 1
        duration: Theme.motion.slow
        easing.type: Easing.OutBack
        easing.overshoot: 1.08
    }

    NumberAnimation {
        id: retractAnim
        target: root
        property: "t"
        to: 0
        duration: Theme.motion.normal
        easing.type: Easing.InBack
        easing.overshoot: 1.10
    }

    function resolveStartRect() {
        const p = WindowBridge.recentPointerPos(600)
        const scene = Qt.point(p ? p.x : width / 2, p ? p.y : height / 2)
        const local = mapFromItem(null, scene.x, scene.y)   // null = 场景根，父链交给 Qt 算
        const own = mapFromItem(null, 0, 0)   // 起点异常（如卡片从左上层长出来）时用这几个数定位
        console.log("[overlay] raw=" + (p ? (Math.round(p.x) + "," + Math.round(p.y)) : "null")
                    + " scene=" + Math.round(scene.x) + "," + Math.round(scene.y)
                    + " local=" + Math.round(local.x) + "," + Math.round(local.y)
                    + " ownScene=" + Math.round(own.x) + "," + Math.round(own.y)
                    + " size=" + Math.round(width) + "x" + Math.round(height))
        return Qt.rect(Math.round(local.x - originSide / 2), Math.round(local.y - originSide / 2), originSide, originSide)
    }

    function beginExpand() {
        if (!animate || pendingOpenInstant) {
            t = 1;
            pendingOpenInstant = false;
            return;
        }
        if (targetW <= 0 || targetH <= 0)
            return;   // 页面还没装载：等 onLoaded 再起（起点已在 onShownChanged 定好）
        t = 0;

        // 这里不要再 resolveStartRect()：第一次打开要等页面装载，而 recentPointerPos 只认 600ms 内的点击，
        // 取不到就退回"窗口中心"。起点必须在刚点完那一刻（onShownChanged）取。
        expandAnim.start();
    }

    function beginRetract() {
        if (!visible)
            return;
        if (!animate || closeInstant || Hub.settings.switchingBackend) {
            t = 0;
            closeInstant = false;
            return;
        }
        expandAnim.stop();
        retractAnim.start();
    }

    onShownChanged: {
        if (shown && openInstant)
            pendingOpenInstant = true
        if (shown) {
            startRect = resolveStartRect();   // 刚点完，指针位置最新鲜：先把起点定下来，再等装载完起动画
            dragOffsetX = 0;   // 每次打开都回到居中位置（抓手拖动留下的偏移不跨次残留）
            dragOffsetY = 0;
            console.log("[overlay] shown start=" + startRect.x + "," + startRect.y   // 起点异常的诊断日志
                        + " size=" + startRect.width + "x" + startRect.height
                        + " center=" + Math.round(width / 2) + "," + Math.round(height / 2))
            if (pageItem)
                beginExpand();      // 已经装载过（第二次打开）：直接起
        } else {   // shown 转 false：原路收回
            beginRetract();
        }
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.color.overlayBg
        opacity: Math.max(0, Math.min(1, root.t))

        MouseArea {
            anchors.fill: parent
            hoverEnabled: true
            acceptedButtons: Qt.AllButtons
            preventStealing: true

            // 遮罩要拦住滚轮，否则滚轮会继续送给下面的消息列表/页面。
            WheelHandler {
                acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                onWheel: (event) => { event.accepted = true }
            }
        }
    }

    Item {
        id: card

        x: root.startRect.x + (root.targetX - root.startRect.x) * root.t + root.dragOffsetX
        y: root.startRect.y + (root.targetY - root.startRect.y) * root.t + root.dragOffsetY
        width: Math.max(1, root.startRect.width + (root.targetW - root.startRect.width) * root.t)
        height: Math.max(1, root.startRect.height + (root.targetH - root.startRect.height) * root.t)
        opacity: Math.min(1, root.t * 6)

        readonly property real cardRadiusNow: 6   // 圆角只在前半程从起点小方块的 6 长到 token 的弹窗圆角，之后钉住
            + (root.cardRadius - 6) * Math.max(0, Math.min(1, root.t))

        Card {   // 不 clip 这一层：阴影要能溢到卡片外面
            anchors.fill: parent
            radius: card.cardRadiusNow
            shadowEnabled: root.cardShadow
            padding: 0
        }

        // 内容钉在目标尺寸上排版（动画中不重排，否则文字会跟着卡片抖），超出部分裁掉：
        // "长出来"的过程就是内容一点点露出来；t 到 1 后放开裁剪，提示气泡才能探出卡片。
        Rectangle {
            anchors.fill: parent
            radius: card.cardRadiusNow
            color: "transparent"
            clip: root.t < 0.999

            Item {
                id: frame
                width: root.targetW
                height: root.targetH
                opacity: Math.max(0, Math.min(1, (root.t - 0.5) / 0.5))

                Loader {
                    id: content
                    anchors.fill: parent
                    // 打开设置/扩展等重页面时不要在 GUI 线程同步构建整棵 QML 树：
                    // 否则遮罩动画第一帧就被几十毫秒的控件创建卡住。
                    asynchronous: true
                    active: root.shown || root.t > 0.001
                    source: root.source

                    onLoaded: {
                        root.pageLoaded(item)

                        // 页面声明了 overlayAnimating 就绑到本外壳的"卡片正在动"：
                        // 展开/收回期间页面自己的转圈先停，避免整窗重绘叠加拖慢动画。
                        if (item.overlayAnimating !== undefined)
                            item.overlayAnimating = Qt.binding(function () { return root.overlayAnimating })
                        if (root.shown)
                            root.beginExpand();
                    }
                }
            }
        }

        // 三点抓手：只有三个浅灰点（无背景、无悬停效果），拖它 = 拖动这张卡片。
        // 命中区（MouseArea）向外各放 10px，视觉上仍是三个点。三个坑：
        // ① 不是拖窗口，那是标题栏的事（WindowBridge.startSystemMove）；
        // ② 位移必须用场景坐标差：MouseArea 跟着卡片动，用局部坐标 mouse.x 算增量会错；
        // ③ MouseArea 不能放进 Row：positioner 会把它当"不参与排版"，三个点挤到同一 x，所以行里只放点。
        Item {
            id: grip

            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: parent.top
            anchors.topMargin: 7
            width: dotRow.implicitWidth
            height: 4
            z: 10

            Row {
                id: dotRow

                anchors.fill: parent
                spacing: 5

                Repeater {
                    model: 3

                    delegate: Rectangle {
                        width: 4   // 3px 点 + 1.5 圆角在 DPR2 抗锯齿下会糊成一片，给到 4px（间距 5）
                        height: 4
                        radius: 2
                        color: Theme.color.textTertiary
                    }
                }
            }

            MouseArea {
                id: gripMouse

                anchors.fill: parent
                anchors.margins: -10
                property real pressSceneX: 0   // 按下点的场景坐标：位移用场景坐标差计算
                property real pressSceneY: 0
                property real baseX: 0   // 按下时的拖拽偏移基准，位移累加在它上面
                property real baseY: 0

                onPressed: (mouse) => {
                    const p = gripMouse.mapToItem(null, mouse.x, mouse.y);
                    pressSceneX = p.x;
                    pressSceneY = p.y;
                    baseX = root.dragOffsetX;
                    baseY = root.dragOffsetY;
                }
                onPositionChanged: (mouse) => {
                    if (!pressed)
                        return;
                    const p = gripMouse.mapToItem(null, mouse.x, mouse.y);
                    root.dragOffsetX = baseX + (p.x - pressSceneX);
                    root.dragOffsetY = baseY + (p.y - pressSceneY);
                }
            }

            // 把抓手登记进原生命中测试：标题栏在 WM_NCHITTEST 里默认判 HTCAPTION，
            // 但系统在 caption 矩形内会先问 windowControlAt()，命中已登记控件就返回 HTCLIENT，
            // 于是三个点的优先级高于标题栏，QML 才收得到按下事件 —— 拖空白处拖窗口，拖三点拖面板。
            Component.onCompleted: WindowBridge.addWindowControl(gripMouse, "other")
            Component.onDestruction: WindowBridge.removeWindowControl(gripMouse)
        }
    }
}
