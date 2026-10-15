// 面板坞的「三点抓手」：抽成通用控件 —— 视觉与交互规则**逐条照 widgets/PageOverlay.qml 那颗**，
// 只有"拖出去之后做什么"由调用方决定（那边是拖动整壳覆盖页的卡片，这边是脱离/回附）。
//
// 需求原话的规矩（两条都别自作主张加东西）：
//   · **只有三个浅灰点**：不要背景、不要悬浮反馈、不要光标反馈（所以这里没有 hoverEnabled，
//     也**不设 cursorShape**——鼠标经过时不该有任何变化）；
//   · 命中区往外放 ~10px：MouseArea 用 `anchors.margins: -10`（视觉上仍是三个点那么大）。
//
// 三个坑（PageOverlay 里踩过并写下来的，这里照抄结论）：
//   ① 不是拖窗口：那是标题栏该干的事（WindowBridge.startSystemMove），这里是拖面板；
//   ② 位移必须用**场景坐标差**：MouseArea 跟着面板一起动，用它的局部坐标(mouse.x)算增量会自己追
//      自己 ⇒ 越拖越慢跟不上手。所以按下/移动都先 mapToItem(null, …) 换成场景坐标；
//      ⚠️ 但"要不要脱离/回附"这种跨窗口判断必须用**屏幕坐标**（mapToGlobal）——场景坐标只在各自
//      窗口内有意义，两个窗口的场景原点不是一个。
//   ③ MouseArea **不能放进 Row 里**：positioner 会把锚定子项当成"不参与排版"，三个点会被排到同一个
//      x 上（看着只剩一个点）。所以行里只放点，MouseArea 做外层 Item 的兄弟。

import QtQuick
import ULinHub 1.0

Item {
    id: root

    // 拖动过程中，位移已经越过阈值（调用方据此决定"现在就脱离"还是"松手才算"）
    property bool dragging: false
    // 手感阈值：判定应该由调用方给（脱离用「离开右栏 48px 或水平位移 60px」，回附用「落进原区域」）
    property real dragThreshold: 6

    // 拖动中的实时反馈，四个量都给调用方：
    //   globalPos  —— 屏幕坐标（跨窗口判断只能用这个）
    //   scenePos   —— 本窗口场景坐标（画本窗口内的落点提示用它）
    //   deltaX/deltaY —— 相对按下点的位移（屏幕坐标系：跨窗口拖动用它算新位置，1:1 跟手）
    signal dragMoved(real deltaX, real deltaY, point globalPos, point scenePos)
    // 松手（调用方在拖动过程中自己记着"还在有效落点里吗"；这里不再回传那个布尔量 ——
    // 信号声明由调用方定义，多一个参数就多一处声明的负担）
    signal dragFinished(real deltaX, real deltaY, point globalPos)
    // 按下：调用方据此开始记"按下点"（QML 的 Window 没有 pressed 信号可接）
    signal pressStarted()

    // 三个点：单个点 4×4、间距 5、半径 2（= 正圆）—— 与 widgets/PageOverlay.qml 那颗**完全一致**。
    //
    // 尺寸史（别来回改）：这里一度放大到 6/6，理由是"4px 在 200% 缩放下只有 8 物理像素、
    // 颜色又是 textTertiary，画在白色卡片上存在但看不见"。后来查明那次的真正原因是**位置**：
    // 抓手被页签条 Flickable 整个盖住（见 basic/shell/PanelDock.qml 的说明），不是尺寸。
    // 抓手搬到右栏标题行后用户直接反馈"这三个点为什么这么大"⇒ 回到通用控件的 4/5。
    // ⚠️ 这两个值刻意**不是 readonly**：调用方要更大/更小的点可以直接赋值覆盖
    //    （曾经从 RightSidebar 赋 readonly 属性 ⇒ 整张右栏实例化失败、壳层全空，见那次教训）。
    property int dotSize: 4
    property int dotGap: 5

    implicitWidth: dotRow.implicitWidth
    implicitHeight: dotSize

    Row {
        id: dotRow

        anchors.fill: parent
        spacing: root.dotGap

        Repeater {
            model: 3

            delegate: Rectangle {
                width: root.dotSize
                height: root.dotSize
                radius: root.dotSize / 2
                color: Theme.color.textTertiary
            }
        }
    }

    MouseArea {
        id: gripMouse

        anchors.fill: parent
        anchors.margins: -10          // 命中区往外放 10px（见文件头）
        preventStealing: true         // 拖拽不被祖先（Flickable / 窗口）抢走

        property real pressSceneX: 0
        property real pressSceneY: 0
        property real pressGlobalX: 0
        property real pressGlobalY: 0

        onPressed: (mouse) => {
            const p = gripMouse.mapToItem(null, mouse.x, mouse.y);
            pressSceneX = p.x;
            pressSceneY = p.y;
            const g = gripMouse.mapToGlobal(mouse.x, mouse.y);
            pressGlobalX = g.x;
            pressGlobalY = g.y;
            root.dragging = false;
            root.pressStarted();
        }

        onPositionChanged: (mouse) => {
            if (!pressed)
                return;

            const scene = gripMouse.mapToItem(null, mouse.x, mouse.y);
            if (!root.dragging
                    && Math.abs(scene.x - pressSceneX) + Math.abs(scene.y - pressSceneY) > root.dragThreshold)
                root.dragging = true;

            // 屏幕坐标：跨窗口判定用（本窗口的场景原点不是别的窗口的原点）。
            // 未过阈值也照报：调用方要拿它算"有没有离开原区域"（那是它的阈值，不是这里的）。
            // 位移一律用**屏幕坐标差**：它不受"MouseArea 跟着面板一起动"影响（局部坐标会自己追自己）。
            const g = gripMouse.mapToGlobal(mouse.x, mouse.y);
            root.dragMoved(g.x - pressGlobalX, g.y - pressGlobalY, g, scene);
        }

        onReleased: (mouse) => {
            const g = gripMouse.mapToGlobal(mouse.x, mouse.y);
            root.dragging = false;
            root.dragFinished(g.x - pressGlobalX, g.y - pressGlobalY, g);
        }

        onCanceled: {
            const g = gripMouse.mapToGlobal(mouse.x, mouse.y);
            root.dragging = false;
            root.dragFinished(g.x - pressGlobalX, g.y - pressGlobalY, g);
        }
    }

    // 把抓手登记进**原生**命中测试（与 PageOverlay 同一套）：
    // 主窗口无边框，标题栏那块在 WM_NCHITTEST 里默认判 HTCAPTION（拖窗口），但原生在 caption 矩形内
    // 会**先问** windowControlAt() 有没有登记过的控件，命中就返回 HTCLIENT ⇒ 三个点的优先级高于标题栏。
    // ⚠️ 只在"宿主窗口是无边框窗口"时登记才有效/才有必要；独立窗口是系统装饰窗口，标题栏归系统，
    //    这里不登记也照常收得到按下事件（nativeHost 由调用方给，缺省 false）。
    property bool nativeHost: false

    Component.onCompleted: {
        if (nativeHost)
            WindowBridge.addWindowControl(gripMouse, "other")
    }
    Component.onDestruction: {
        if (nativeHost)
            WindowBridge.removeWindowControl(gripMouse)
    }
}
