// 整壳覆盖页的**外壳**：遮罩 + "从点击处长出来"的卡片 + 收回播完才卸载。
//
// 为什么要有它：设置 / 插件市场 / 扩展管理原来是"Shell 里一个整壳 Loader + 页内自己画遮罩与
// 居中卡片"，于是只有"瞬间出现、瞬间消失"。这里把外壳抽出来，用与 widgets/AppPopup.qml
// **同一套机制**：一个 0 → 1 的 t 同时驱动卡片的 x / y / 宽 / 高 / 圆角、遮罩透明度、内容
// 透明度 —— 弹出与收回天生是同一条路的两端（演示扩展 PanelAnimDemo 的那个观感）。
//
//   展开：t 0 → 1，OutBack（越过终点再回弹 ⇒ 弹出感）
//   收回：t 1 → 0，InBack（收回从**当前** t 起步，展开到一半被打断也不跳）
//
// 起点 = **最近一次鼠标点击的位置**（32×32 小方块），拿不到（纯键盘 / 程序触发）就退化成
// 从居中长出来 —— 所以壳层调用方不用接线：点侧栏哪颗图标，卡片就从那颗图标那儿长出来。
//
// 页面这边的要求只有三条（各页改动都只是把原来的 Card 换成 Item）：
//   · 根是 Item，且给出 implicitWidth / implicitHeight = 卡片目标尺寸；
//   · 内容填满自己（原来挂在 Card 里的那层，现在直接挂在自己身上）；
//   · 自己怎么关还怎么关（Hub.xxx.closePage()）—— 壳层把 shown 置 false，这里负责收回。
//
// 可选第四条（建议要）：声明 `property bool overlayAnimating: false`，并把页面里那些
// "一直在跑"的东西（尤其 BusySpinner）门控上它 —— 展开/收回那两百多毫秒里转圈先停，
// 否则每帧多花 ~10ms 整窗重绘，动画会明显掉帧（由来与实测见下面 overlayAnimating 的注释）。

import QtQuick
import ULinHub 1.0

Item {
    id: root

    // 由壳层绑到 Hub.xxx.open：true = 展开；false = 原路收回，收完（t 回 0）才卸载页面
    property bool shown: false
    // 页面文件（Style.url("basic/...")）
    property url source
    property real cardRadius: Theme.radius.popup
    property bool cardShadow: true

    // 页面装载完成时抛出去（有的页要接它的信号，例如 closeRequested）
    signal pageLoaded(var page)

    readonly property var pageItem: content.item
    // 卡片目标尺寸：由页面自己声明，尺寸只留一处
    readonly property real targetW: pageItem ? pageItem.implicitWidth : 0
    readonly property real targetH: pageItem ? pageItem.implicitHeight : 0

    // ---------------- 动画状态 ----------------
    property real t: 0
    property rect startRect: Qt.rect(0, 0, 0, 0)
    // 与演示扩展那颗 32×32 入口按钮同值
    readonly property int originSide: 32
    readonly property real targetX: Math.round((width - targetW) / 2)
    readonly property real targetY: Math.round((height - targetH) / 2)

    // 卡片几何正在动（展开或收回）。装载进来的页面只要声明了同名属性，外壳就会把它绑上去 ——
    // 页面拿它按停自己那些"一直在跑"的东西（首当其冲是 BusySpinner）：
    // 转圈一跑，窗口每帧都要整窗重绘（实测 +10ms/帧），叠在展开动画上会让动画窗从
    // ~26ms/帧 涨到 ~50ms/帧 —— 也就是"加载中的打开动画明显更卡"的直接原因。
    readonly property bool overlayAnimating: t > 0.001 && t < 0.999

    visible: shown || t > 0.001

    // 展开 / 收回都由 JS 起停（理由同 AppPopup：Transition 里写死 from 会在打断时跳）
    NumberAnimation {
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
        // 技术细节走日志：起点不对（例如卡片从左上层长出来）时，这四个数就能定位到底错在哪一步
        const own = mapFromItem(null, 0, 0)
        console.log("[overlay] raw=" + (p ? (Math.round(p.x) + "," + Math.round(p.y)) : "null")
                    + " scene=" + Math.round(scene.x) + "," + Math.round(scene.y)
                    + " local=" + Math.round(local.x) + "," + Math.round(local.y)
                    + " ownScene=" + Math.round(own.x) + "," + Math.round(own.y)
                    + " size=" + Math.round(width) + "x" + Math.round(height))
        return Qt.rect(Math.round(local.x - originSide / 2),
                       Math.round(local.y - originSide / 2),
                       originSide, originSide)
    }

    function beginExpand() {
        if (targetW <= 0 || targetH <= 0)
            return;   // 页面还没装载：等 onLoaded 再起（起点已在 onShownChanged 定好）
        t = 0;
        // ⚠️ 这里**不要**再 resolveStartRect()：第一次打开要等页面装载，重的页（设置/扩展/插件）
        //    远超 recentPointerPos 的 600ms 时限 ⇒ 取不到就退回"窗口中心"，
        //    表现就是"卡片不从鼠标点击处长出来"。起点必须在刚点完那一刻取。
        expandAnim.start();
    }

    function beginRetract() {
        if (!visible)
            return;
        expandAnim.stop();
        retractAnim.start();
    }

    onShownChanged: {
        if (shown) {
            // 刚点完，指针位置最新鲜：先把起点定下来，再等装载完起动画
            startRect = resolveStartRect();
            // 起点错了就会"卡片不从点击处长出来"，这条日志专门用来对照（技术细节走日志，界面不提示）
            console.log("[overlay] shown start=" + startRect.x + "," + startRect.y
                        + " size=" + startRect.width + "x" + startRect.height
                        + " center=" + Math.round(width / 2) + "," + Math.round(height / 2))
            if (pageItem)
                beginExpand();      // 已经装载过（第二次打开）：直接起
            // 否则交给 Loader 的 onLoaded
        } else {
            beginRetract();
        }
    }

    // ---------------- 遮罩 ----------------
    // 吃掉点击，别让底下的壳还能点到（与改造前各页那层遮罩同职责）；透明度跟着 t 走
    Rectangle {
        anchors.fill: parent
        color: Theme.color.overlayBg
        opacity: Math.max(0, Math.min(1, root.t))

        MouseArea {
            anchors.fill: parent
        }
    }

    // ---------------- 卡片 ----------------
    Item {
        id: card

        x: root.startRect.x + (root.targetX - root.startRect.x) * root.t
        y: root.startRect.y + (root.targetY - root.startRect.y) * root.t
        width: Math.max(1, root.startRect.width + (root.targetW - root.startRect.width) * root.t)
        height: Math.max(1, root.startRect.height + (root.targetH - root.startRect.height) * root.t)
        opacity: Math.min(1, root.t * 6)

        // 圆角只在前半程从起点小方块的 6 长到 token 的弹窗圆角，之后钉住
        readonly property real cardRadiusNow: 6
            + (root.cardRadius - 6) * Math.max(0, Math.min(1, root.t))

        // 面 + 阴影：与各页原来那个 Card 同一套画法。
        // 刻意**不 clip 这一层** —— 阴影要能溢到卡片外面去。
        Card {
            anchors.fill: parent
            radius: card.cardRadiusNow
            shadowEnabled: root.cardShadow
            padding: 0
        }

        // 内容：钉在**目标**尺寸上排版（动画中不重排，否则文字会跟着卡片抖），
        // 超出卡片的部分被裁掉 —— 于是"长出来"的过程就是内容一点点露出来。
        // 稳定后放开裁剪：提示气泡这一类溢出卡片的东西要能照常探出去。
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
                    active: root.shown || root.t > 0.001
                    source: root.source

                    onLoaded: {
                        root.pageLoaded(item)
                        // 页面声明了 overlayAnimating 就把它绑到本外壳的"卡片正在动"：
                        // 展开 / 收回那两百多毫秒里，页面自己的转圈先停（见上面的说明）。
                        // 「声明了才绑」是为了不强迫所有页面加这个属性。
                        if (item.overlayAnimating !== undefined)
                            item.overlayAnimating = Qt.binding(function () { return root.overlayAnimating })
                        if (root.shown)
                            root.beginExpand();
                    }
                }
            }
        }
    }
}
