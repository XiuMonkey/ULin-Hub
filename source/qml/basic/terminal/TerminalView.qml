// 终端网格视图：把 TerminalVtModel（→ TerminalVt → libvterm）的网格逐格画出来。
//
// 分层：本文件**只画**，不解析、不联网。要画什么全从 model 拿：
//   · model.rowsJson(first, last) —— 一段行，行内是"同样式合并后的段" [文本, fg, bg, flags]；
//   · model.dirtyRowsChanged(rows) —— 哪几行变了（只重画那几行，别整屏重建）；
//   · model.cursorRow/cursorCol/cursorVisible —— 光标（闪烁在 QML 侧）；
//   · model.state/statusText —— 顶部状态行（连接中/只读/错误都靠它）。
//
// 为什么每个可见行一个 Canvas（而不是整屏一个 Canvas / Repeater + Text）：
//   · 整屏一个 Canvas 时，"只脏一行"也得整屏重画 —— 一屏 2400 格的字形布局每秒几十次，稳不住；
//   · Repeater + 每格一个 Text 是一屏 2400 个 item，创建/布局的代价比画还大；
//   · 每行一个 Canvas 正好对上 TerminalVt 的脏行粒度（damage 按行合并），只有脏行的 canvas 会重绘，
//     非脏行连 item 树都不动。行内再按"段"画，一段一次 drawText（不是一格一次）。
//
// 宽字符：模型把宽字符的**续格**给成空文本段，这里必须跳过（首格已占两格宽）—— 否则中日韩整行错位。

import QtQuick
import ULinHub 1.0

// 逐行 Canvas 的 delegate 里要引用外层 id（root / flick / Terminal）——Bound 让这些引用是
// "编译期绑定"而不是"运行时查作用域链"，qmllint 的 unqualified 警告也随之消失（268 → 3）。
pragma ComponentBehavior: Bound

Item {
    id: root

    // ---- 外部契约（TerminalPanel 给）----
    // 块的稳定 id：键盘路径要用它把输入路由回 Terminal.flushInput（与 model 是同一个块）
    property string blockId: ""
    // 网格模型（TerminalVtModel*）；null = 还没绑定上（画空屏）
    property var model: null
    // 等宽字体族：Cascadia Mono 是 Windows 终端默认，Consolas 兜底
    property string fontFamily: "Cascadia Mono"
    property var fontFallbacks: ["Consolas", "Courier New", "Microsoft YaHei UI"]
    property int fontSize: 13
    property int lineHeight: Math.round(fontSize * 1.35)
    // 主题：终端默认前景/背景 + ANSI 16 色（"dark"/"light" 各一套）
    property bool darkTheme: true
    // 光标闪烁
    property bool cursorBlink: true

    // 面板尺寸算出来的网格。
    // ⚠️ 首帧/尺寸未定时 flick 的宽高还是 0，算式会算出负数 —— 必须夹掉"没有布局"的情形：
    //    否则会把 2×1 这种退化尺寸推给 VT，而 libvterm 的 screen_resize 遇到这种尺寸会直接 abort
    //    （实测："screen_resize failed to update cursor position" + 0xC0000409）。所以宽高不够就是 0，
    //    pushGridSize() 见 0 直接跳过，等布局真出来再推一次。
    readonly property int gridCols: (flick.width > padding * 2 && cellWidth > 1)
                                    ? Math.max(2, Math.floor((flick.width - padding * 2) / cellWidth)) : 0
    readonly property int gridRows: (flick.height > padding * 2 + lineHeight)
                                    ? Math.max(1, Math.floor((flick.height - padding * 2) / lineHeight)) : 0
    readonly property real cellWidth: metrics.advanceWidth

    // 网格量宽用：等宽字体里 "M" 的advance 就是格宽；宽字符按两格算（CJK 在等宽字体里正好两倍）
    TextMetrics {
        id: metrics
        font.family: root.fontFamily
        font.pixelSize: root.fontSize
        text: "M"
    }

    // ---- 配色 ----
    // 暗色用 xterm 的默认 16 色（与旧页面 xterm.js 的观感一致），亮色用 xterm 的 light 一套。
    // 索引语义固定：0 黑 1 红 2 绿 3 黄 4 蓝 5 品 6 青 7 白 / 8-15 亮色。
    // ⚠️ 写成**单行**数组：QML 的 JS 在 `[` 换行后按语句结束处理（换行即 ASI），多行字面量是语法错。
    readonly property var ansiDark: ["#000000", "#CD3131", "#0DBC79", "#E5E510", "#2472C8", "#BC3FBC", "#11A8CD", "#E5E5E5", "#666666", "#F14C4C", "#23D18B", "#F5F543", "#3B8EEA", "#D670D6", "#29B8DB", "#FFFFFF"]
    readonly property var ansiLight: ["#000000", "#CD3131", "#00BC00", "#949800", "#0451A5", "#BC05BC", "#0598BC", "#555555", "#666666", "#CD3131", "#14CE14", "#B5BA00", "#0451A5", "#BC05BC", "#0598BC", "#A5A5A5"]
    readonly property color defaultFg: darkTheme ? "#E5E5E5" : "#333333"
    readonly property color defaultBg: darkTheme ? "#000000" : "#F7F7F7"
    readonly property color selectionBg: darkTheme ? "#264F78" : "#ADD6FF"

    // 位与 TermStyle::Flag 一致（见 include/common/terminal/TerminalVt.h）
    readonly property int fFgDefault: 1
    readonly property int fBgDefault: 2
    readonly property int fBold: 4
    readonly property int fItalic: 8
    readonly property int fUnderline: 16
    readonly property int fBlink: 32
    readonly property int fReverse: 64
    readonly property int fConceal: 128
    readonly property int fStrike: 256

    // ---- 每一行的内容缓存 ----
    // rows: JS 数组，每项 = 该行的段数组。只在收到脏行通知时替换那几项（不重建数组本身）。
    property var rows: []
    property int padding: 6
    // 光标闪烁相位（只在需要重画光标时才动）
    property bool cursorOn: true

    clip: true
    // 键盘入口：**本视图必须拿到 active focus**，Keys.onPressed 才会来（这是"输入不进去"的第一现场）。
    // focus:true 只是"在本作用域里声明想要焦点"，真正生效要靠 forceActiveFocus()：
    // 壳里 Composer / 输入框 / Popup 都会抢焦点，光靠 focus:true 拿不到（实测 Window.activeFocusItem
    // 一直是 null / QQuickRootItem）。所以下面还有三处主动要焦点：建好模型、绑上块、以及点一下视图。
    focus: true
    // Tab 必须留给 PTY（补全/切换焦点在 shell 里是 Tab 键的活），所以不参与 Tab 焦点导航
    activeFocusOnTab: false

    // 主动要焦点：建好模型/换块之后，用户一打开终端就能直接打字
    function grabFocus() {
        root.forceActiveFocus()
        cursorOn = true
    }

    function rgbOf(value) {
        // 0xRRGGBB → "#rrggbb"
        return "#" + ("000000" + (value >>> 0).toString(16)).slice(-6)
    }

    function paletteColor(index) {
        var table = darkTheme ? ansiDark : ansiLight
        return (index >= 0 && index < table.length) ? table[index] : root.defaultFg
    }

    // 重新取一段行（含越界钳制）。整屏重画时 first=0/last=rows-1。
    function reloadRows(first, last) {
        if (!model)
            return
        var json = model.rowsJson(first, last)
        if (!json || json.length === 0)
            return
        var parsed = JSON.parse(json)
        var next = rows.slice()   // 浅拷贝：只换脏的那几项，别让所有行都重新绑定
        for (var i = 0; i < parsed.length; ++i) {
            var rowIndex = first + i
            if (rowIndex >= 0 && rowIndex < maxRows)
                next[rowIndex] = parsed[i]
        }
        rows = next
    }

    function reloadAll() {
        if (!model)
            return
        var count = model.rows
        maxRows = count
        reloadRows(0, count - 1)
        rowRepeater.model = count
    }

    // 可见行数上限：模型尺寸变了就跟着变
    property int maxRows: 0

    Component.onCompleted: {
        if (model)
            reloadAll()
        // 建好就要焦点（打开终端块后直接能打字）。callLater：让同一帧里别处的 focus 请求先落定，
        // 我们再"抢"回来 —— 否则会被 Composer 之类后到的请求盖掉。
        Qt.callLater(root.grabFocus)
    }

    onModelChanged: {
        rows = []
        if (model) {
            reloadAll()
            Qt.callLater(root.grabFocus)
        }
    }

    // 脏行通知 → 只重取那几行（rows 赋值会带着 Repeater 里对应 delegate 的 rowCells 一起重绑）
    Connections {
        target: root.model
        ignoreUnknownSignals: true

        function onDirtyRowsChanged(dirty) {
            if (!root.model)
                return
            var full = (dirty.length === root.model.rows)
            if (full) {
                root.reloadAll()
                return
            }
            // 脏行一定升序：按"连续区间"归并成几段，少过几次 QML↔C++ 边界
            var start = dirty[0]
            var previous = dirty[0]
            for (var i = 1; i < dirty.length; ++i) {
                if (dirty[i] !== previous + 1) {
                    root.reloadRows(start, previous)
                    start = dirty[i]
                }
                previous = dirty[i]
            }
            root.reloadRows(start, previous)
        }

        function onSizeChanged() { root.reloadAll() }
        function onTitleChanged() { /* 标题由状态行显示，见下 */ }
    }

    // 网格尺寸变化（面板拉伸/字体变化）：先告诉模型，再整屏重取
    onGridColsChanged: pushGridSize()
    onGridRowsChanged: pushGridSize()
    onFontSizeChanged: pushGridSize()
    // 首帧常见路径：属性没"变化"（一直是 0）、尺寸是后来才出来的 —— 宽度一变就再算一次
    onCellWidthChanged: pushGridSize()

    function pushGridSize() {
        // gridCols/gridRows 为 0 = 还没有真实布局（见上面的夹取理由）
        if (!model || gridCols < 2 || gridRows < 1)
            return
        model.resizeGrid(gridRows, gridCols)
    }

    // ---- 光标闪烁 ----
    Timer {
        id: blinkTimer
        interval: 530
        repeat: true
        running: root.cursorBlink && root.activeFocus && root.model !== null
                 && root.model.cursorVisible && root.model.cursorRow >= 0
        onTriggered: root.cursorOn = !root.cursorOn
    }

    // ---- 键盘 ----
    // 把"一个按键"翻成终端字节并交给桥。抽成函数（而不是全写在 Keys.onPressed 里）有两个好处：
    //   ① 按键翻译只有一份，测试/自检可以直接喂 (key, text, modifiers) 调它，不必伪造 QKeyEvent；
    //   ② Keys.onPressed 只剩"接住事件 + accepted"，焦点相关的行为一眼看清。
    // 参数就是 QKeyEvent 的三个字段：key = Qt::Key_*，text = 可打印文本（可能为空），modifiers = Qt 的修饰位。
    function handleKey(key, text, modifiers) {
        if (!root.model)
            return

        // 打字时光标回到实时画面（在历史里打字容易"看不到自己打的字"）
        root.model.scrollToBottom()

        var mods = 0
        if (modifiers & Qt.ShiftModifier) mods |= 1
        if (modifiers & Qt.AltModifier) mods |= 2
        if (modifiers & Qt.ControlModifier) mods |= 4

        var isPlainText = (modifiers & (Qt.ControlModifier | Qt.AltModifier | Qt.MetaModifier)) === 0
        var first = text.length > 0 ? text.charCodeAt(0) : -1
        var isAsciiPrintable = isPlainText && text.length === 1 && first >= 0x20 && first <= 0x7e

        if (isAsciiPrintable) {
            // ⚠️ 可打印 ASCII **必须按 text 发**，不能按 key：Qt 的 QKeyEvent::key 对字母永远是
            // 大写码点（Qt::Key_A = 'A'），小写/大小写与符号全靠 text。按 key 发的话敲 a 会得到 A。
            root.model.typeText(text)
        } else {
            // 特殊键（方向/编辑/翻页/功能键）、Ctrl 组合、以及非 ASCII 之外的兜底都走键盘表
            root.model.sendQtKey(key, mods)
            // 非 ASCII 文本（中文/IME 提交的串）：sendQtKey 刻意只认 ASCII，这里补发一次
            if (isPlainText && text.length > 0 && first > 0x7f)
                root.model.typeText(text)
        }

        // 把模型攒好的字节交给桥（→ TerminalSession::writeBytes → terminal/write 或扩展后端）
        Terminal.flushInput(root.blockId)
    }

    // 键盘入口：本视图独占焦点，事件才会到这里；**必须 accepted = true**，
    // 否则未处理的按键会继续往父链走（宿主快捷键/页面导航会把它吞掉，表现为"敲了没反应"）。
    Keys.onPressed: (event) => {
        if (!root.model) {
            event.accepted = false
            return
        }
        event.accepted = true
        root.handleKey(event.key, event.text, event.modifiers)
    }

    // 没有 activeFocus 时也把按键接住（比如焦点在别处但鼠标刚点进来这一瞬）：
    // QML 的 Keys 只在有焦点时投递，这里用 Window 级兜底没有意义 —— 真正的手段是 grabFocus()，
    // 所以这里只把"焦点丢了"这件事记进日志，别静默（用户报障时第一眼就能看出来）。
    onActiveFocusChanged: {
        cursorOn = true
        if (!activeFocus && model !== null)
            Terminal.log(blockId, "[视角] 终端视图失去键盘焦点（点击视图或用 Tab 切回来即可继续输入）")
    }

    // ---- 状态行：连接中/只读/错误 ----
    // state == "attached" 时收起来（画面本身够说明问题）；其余状态都给一行提示
    Rectangle {
        id: statusBar
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: 22
        visible: root.model !== null && String(root.model.state) !== "attached"
        color: root.darkTheme ? "#1F2937" : "#ECEFF3"
        z: 2

        Text {
            anchors.fill: parent
            anchors.leftMargin: 8
            anchors.rightMargin: 8
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
            color: root.darkTheme ? "#9CA3AF" : "#5A626C"
            font.family: "Microsoft YaHei UI"
            font.pixelSize: 11
            text: root.model === null ? ""
                  : (String(root.model.statusText).length > 0 ? String(root.model.statusText)
                                                              : String(root.model.state))
        }
    }

    // 只读角标（输入被另一个窗口接管时）
    Rectangle {
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 6
        width: readOnlyLabel.implicitWidth + 12
        height: 18
        radius: 4
        visible: root.model !== null && root.model.readOnly
        color: root.darkTheme ? "#4A1F1F" : "#F3D6D6"
        z: 2

        Text {
            id: readOnlyLabel
            anchors.centerIn: parent
            text: "只读"
            color: root.darkTheme ? "#CE7A7A" : "#8A3B3B"
            font.family: "Microsoft YaHei UI"
            font.pixelSize: 10
        }
    }

    // ---- 网格本体 ----
    Flickable {
        id: flick
        anchors.fill: parent
        anchors.topMargin: statusBar.visible ? statusBar.height : 0
        contentWidth: width
        contentHeight: height
        interactive: false   // 滚动由滚轮 → viewOffset 承担（历史在 C++ 侧，不是 QML 的滚动条）
        clip: true

        Column {
            x: root.padding
            y: root.padding
            spacing: 0

            Repeater {
                id: rowRepeater
                model: 0

                delegate: Canvas {
                    id: rowCanvas
                    required property int index
                    width: flick.width - root.padding * 2
                    height: root.lineHeight

                    // 本行的段（来自 root.rows[index]）；整屏重画/脏行更新都会换掉这个数组引用
                    property var rowCells: (index >= 0 && index < root.rows.length)
                                           ? root.rows[index] : null

                    // 光标画在哪一格（只有光标所在那一行需要它）
                    property bool cursorHere: root.model !== null
                                              && root.model.cursorVisible
                                              && root.model.cursorRow === index
                                              && root.model.cursorCol >= 0
                                              && root.cursorOn && root.activeFocus

                    onRowCellsChanged: requestPaint()
                    onCursorHereChanged: requestPaint()
                    onWidthChanged: requestPaint()

                    onPaint: {
                        var ctx = getContext("2d")
                        ctx.reset()
                        ctx.clearRect(0, 0, width, height)
                        // 底色：整行画一次默认背景（段里的 bg 再覆盖）
                        ctx.fillStyle = root.defaultBg
                        ctx.fillRect(0, 0, width, height)

                        if (!rowCells)
                            return

                        // 字体
                        ctx.font = "normal " + root.fontSize + "px \"" + root.fontFamily + "\""
                        ctx.textBaseline = "middle"
                        var y = height / 2 + 1

                        // ① 背景：先把整行的段底色铺完，再画文字（避免后画的段底色压掉前一段的字）
                        var penX = 0
                        var cellX = []
                        for (var i = 0; i < rowCells.length; ++i) {
                            var seg = rowCells[i]
                            var text = seg[0]
                            var flags = seg[3]

                            // 宽字符续格：占位但不画（首格已经占了两格）—— 用空格占宽度即可
                            if (text.length === 0) {
                                cellX.push(penX)
                                penX += root.cellWidth
                                continue
                            }

                            var cells = text.length   // 段内格数（每格一个码点；组合字符在段里也按一格算）
                            cellX.push(penX)

                            var bgDefault = (flags & root.fBgDefault) !== 0
                            var reverse = (flags & root.fReverse) !== 0
                            var bg = bgDefault ? root.defaultBg : root.rgbOf(seg[2])
                            var fg = (flags & root.fFgDefault) !== 0 ? root.defaultFg : root.rgbOf(seg[1])
                            if (reverse) {
                                var swap = bg
                                bg = fg
                                fg = swap
                            }

                            var runWidth = cells * root.cellWidth
                            // 默认底色：第 ① 遍已经把整行铺成 defaultBg，这里只画"非默认"的段
                            if (!bgDefault || reverse) {
                                ctx.fillStyle = bg
                                ctx.fillRect(penX, 0, runWidth, height)
                            }
                            penX += runWidth
                        }

                        // ② 文字：一段一次 drawText
                        ctx.textBaseline = "middle"
                        for (var j = 0; j < rowCells.length; ++j) {
                            var segment = rowCells[j]
                            var runText = segment[0]
                            if (runText.length === 0)
                                continue

                            var segFlags = segment[3]
                            var isFgDefault = (segFlags & root.fFgDefault) !== 0
                            var isBgDefault = (segFlags & root.fBgDefault) !== 0
                            var color = isFgDefault ? root.defaultFg : root.rgbOf(segment[1])
                            var back = isBgDefault ? root.defaultBg : root.rgbOf(segment[2])
                            if ((segFlags & root.fReverse) !== 0) {
                                var tmp = color
                                color = back
                                back = tmp
                            }
                            if ((segFlags & root.fConceal) !== 0)
                                continue   // 隐藏：什么都不画

                            var weight = (segFlags & root.fBold) !== 0 ? "bold " : "normal "
                            var style = (segFlags & root.fItalic) !== 0 ? "italic " : ""
                            ctx.font = style + weight + root.fontSize + "px \"" + root.fontFamily + "\""
                            ctx.fillStyle = color
                            var x = cellX[j]
                            ctx.fillText(runText, x, y)

                            if ((segFlags & root.fUnderline) !== 0) {
                                ctx.fillRect(x, height - 2, runText.length * root.cellWidth, 1)
                            }
                            if ((segFlags & root.fStrike) !== 0) {
                                ctx.fillRect(x, Math.round(height / 2), runText.length * root.cellWidth, 1)
                            }
                        }

                        // ③ 光标：实心块 + 反色字（只在该行、且只有相位在"亮"时）
                        if (cursorHere) {
                            var cx = root.model.cursorCol * root.cellWidth
                            ctx.fillStyle = root.darkTheme ? "#E5E5E5" : "#333333"
                            ctx.fillRect(cx, 0, root.cellWidth, height)
                        }
                    }
                }
            }
        }
    }

    // ---- 鼠标：滚轮 → 视口偏移（历史）；**左键点一下 → 抢焦点** ----
    // ⚠️ 这个 MouseArea 必须声明在**网格之后**（同 z 时后声明者在上层）：网格里没有别的可交互项
    //    （Canvas 不吃鼠标事件），由它独占点击/滚轮。放在网格之前会先被 Flickable 那片区域压住。
    MouseArea {
        id: inputArea
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton
        // 只"要焦点"，不拦别的事：以后要加选区的拖动也走这里，不至于把焦点弄丢
        onPressed: root.grabFocus()
        onClicked: root.grabFocus()
        onWheel: (wheel) => {
            if (!root.model)
                return
            var lines = wheel.angleDelta.y / 40   // 一格滚轮 ≈ 3 行（120/40）
            root.model.scrollBy(lines > 0 ? Math.max(1, Math.round(lines))
                                          : Math.min(-1, Math.round(lines)))
        }
    }

    // 视图自己的 blockId 已在上面的"外部契约"里声明（键盘路径用它路由 Terminal.flushInput）
}