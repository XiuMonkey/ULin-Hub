// 终端网格视图：把 TerminalVtModel（→ TerminalVt → libvterm）的网格逐格画出来。本文件只画，不解析、不联网：
// model.rowSegments(first,last) 取一段行（同样式合并后的段 [文本, fg, bg, flags]；空时退回 rowsJson）、
// model.dirtyRowsChanged(rows) 只重画脏行、model.cursorRow/Col/Visible 画光标（闪烁在 QML 侧）、
// model.state/statusText 是顶部状态行（连接中/只读/错误）。
// 为什么每行一个 Canvas（而不是整屏一个 Canvas / Repeater+Text）：整屏 Canvas 时"只脏一行"也得整屏重画
// （一屏 2400 格字形布局每秒几十次，稳不住）；Repeater + 每格一个 Text 是一屏 2400 个 item，创建/布局比画还贵；
// 每行一个 Canvas 正好对上 TerminalVt 的脏行粒度，只有脏行的 canvas 会重绘，非脏行连 item 树都不动。
// ⚠️ 宽字符：模型把宽字符的**续格**给成空文本段，这里必须跳过（首格已占两格宽），否则中日韩整行错位。

import QtQuick
import ULinHub 1.0

// 逐行 Canvas 的 delegate 里要引用外层 id（root / flick / Terminal）——Bound 让这些引用是
// "编译期绑定"而不是"运行时查作用域链"，qmllint 的 unqualified 警告也随之消失（268 → 3）。
pragma ComponentBehavior: Bound

Item {
    id: root

    property string blockId: ""
    property var model: null   // 网格模型（TerminalVtModel*）；null = 还没绑定上（画空屏）
    property string fontFamily: "Cascadia Mono"   // 等宽字体族：Cascadia Mono 是 Windows 终端默认，Consolas 兜底
    property var fontFallbacks: ["Consolas", "Courier New", "Microsoft YaHei UI"]
    property int fontSize: 13
    property int lineHeight: Math.round(fontSize * 1.35)
    property bool darkTheme: true   // 主题：终端默认前景/背景 + ANSI 16 色（"dark"/"light" 各一套）
    property bool cursorBlink: true   // 光标闪烁

    // 面板尺寸算出来的网格。
    // ⚠️ 首帧/尺寸未定时 flick 的宽高还是 0，算式会算出负数 —— 必须夹掉"没有布局"的情形：
    // 否则会把 2×1 这种退化尺寸推给 VT，而 libvterm 的 screen_resize 遇到这种尺寸会直接 abort
    // pushGridSize() 见 0 直接跳过，等布局真出来再推一次。
    readonly property int gridCols: (flick.width > padding * 2 && cellWidth > 1)
                                    ? Math.max(2, Math.floor((flick.width - padding * 2) / cellWidth)) : 0
    readonly property int gridRows: (flick.height > padding * 2 + lineHeight)
                                    ? Math.max(1, Math.floor((flick.height - padding * 2) / lineHeight)) : 0
    readonly property real cellWidth: metrics.advanceWidth

    TextMetrics {   // 网格量宽用：等宽字体里 "M" 的advance 就是格宽；宽字符按两格算（CJK 在等宽字体里正好两倍）
        id: metrics
        font.family: root.fontFamily
        font.pixelSize: root.fontSize
        text: "M"
    }

    readonly property var ansiDark: ["#000000", "#CD3131", "#0DBC79", "#E5E510", "#2472C8", "#BC3FBC", "#11A8CD", "#E5E5E5", "#666666", "#F14C4C", "#23D18B", "#F5F543", "#3B8EEA", "#D670D6", "#29B8DB", "#FFFFFF"]
    readonly property var ansiLight: ["#000000", "#CD3131", "#00BC00", "#949800", "#0451A5", "#BC05BC", "#0598BC", "#555555", "#666666", "#CD3131", "#14CE14", "#B5BA00", "#0451A5", "#BC05BC", "#0598BC", "#A5A5A5"]
    readonly property color defaultFg: darkTheme ? "#E5E5E5" : "#333333"
    readonly property color defaultBg: darkTheme ? "#000000" : "#F7F7F7"
    readonly property color selectionBg: darkTheme ? "#264F78" : "#ADD6FF"

    readonly property int fFgDefault: 1   // 位与 TermStyle::Flag 一致（见 include/common/terminal/TerminalVt.h）
    readonly property int fBgDefault: 2
    readonly property int fBold: 4
    readonly property int fItalic: 8
    readonly property int fUnderline: 16
    readonly property int fBlink: 32
    readonly property int fReverse: 64
    readonly property int fConceal: 128
    readonly property int fStrike: 256

    property var rows: []
    property int padding: 6
    property bool cursorOn: true   // 光标闪烁相位（只在需要重画光标时才动）

    clip: true

    // 键盘入口：**本视图必须拿到 active focus**，Keys.onPressed 才会来（这是"输入不进去"的第一现场）。
    // focus:true 只是"在本作用域里声明想要焦点"，真正生效要靠 forceActiveFocus()：
    // 一直是 null / QQuickRootItem）。所以下面还有三处主动要焦点：建好模型、绑上块、以及点一下视图。
    focus: true
    activeFocusOnTab: false   // Tab 必须留给 PTY（补全/切换焦点在 shell 里是 Tab 键的活），所以不参与 Tab 焦点导航

    function grabFocus() {   // 主动要焦点：建好模型/换块之后，用户一打开终端就能直接打字
        root.forceActiveFocus()
        cursorOn = true
    }

    Rectangle {
        anchors.fill: parent
        color: root.defaultBg
        z: -1
    }

    function rgbOf(value) {
        return "#" + ("000000" + (value >>> 0).toString(16)).slice(-6)   // 0xRRGGBB → "#rrggbb"
    }

    function paletteColor(index) {
        var table = darkTheme ? ansiDark : ansiLight
        return (index >= 0 && index < table.length) ? table[index] : root.defaultFg
    }

    function reloadRows(first, last) {   // 重新取一段行（含越界钳制）。整屏重画时 first=0/last=rows-1
        if (!model)
            return
        var parsed = model.rowSegments(first, last)   // 优先拿 C++ 的结构化数据（QVariantList），解析不再放 QML（分层：逻辑在 C++）
        if (!parsed || parsed.length === 0) {
            var json = model.rowsJson(first, last)   // 兜底：结构化返回为空就退回旧路 —— 保证"分层优化"永远不会把终端变成空白屏
            if (!json || json.length === 0)
                return
            parsed = JSON.parse(json)
        }
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

    property int maxRows: 0   // 可见行数上限：模型尺寸变了就跟着变

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

    Connections {   // 脏行通知 → 只重取那几行（rows 赋值会带着 Repeater 里对应 delegate 的 rowCells 一起重绑）
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
            var start = dirty[0]   // 脏行一定升序：按"连续区间"归并成几段，少过几次 QML↔C++ 边界
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

    onGridColsChanged: { gridPushTimer.restart(); reloadAll() }   // 网格尺寸变化（面板拉伸/字体变化）：先告诉模型，再整屏重取
    onGridRowsChanged: { gridPushTimer.restart(); reloadAll() }
    onFontSizeChanged: pushGridSize()
    onCellWidthChanged: pushGridSize()

    Timer {   // 每跳一次就 pushGridSize() 等于连续 resize PTY 五次 => shell 输出被打断，画面只剩前几行
        id: gridPushTimer
        interval: 150
        running: false
        repeat: false
        onTriggered: pushGridSize()
    }

    function pushGridSize() {
        if (!model || gridCols < 2 || gridRows < 1)   // gridCols/gridRows 为 0 = 还没有真实布局（见上面的夹取理由）
            return
        model.resizeGrid(gridRows, gridCols)

        // 把"视图量出来的网格"也推给桥：**引擎/PTY 的 cols 必须与本地 VT 是同一个数**。
        // 不一致时 shell 按它的宽度折行、VT 按另一个宽度折行，光标行列随后错位 ——
        // ⚠️ 参数顺序是 (blockId, cols, rows)，与 TerminalSession::resize(cols, rows) /
        // terminal/resize 的 args{cols, rows} 一致；而 resizeGrid 是 (rows, cols)。别弄反。
        Terminal.applyGridSize(root.blockId, gridCols, gridRows)
    }

    Timer {
        id: blinkTimer
        interval: 530
        repeat: true
        running: root.cursorBlink && root.activeFocus && root.model !== null
                 && root.model.cursorVisible && root.model.cursorRow >= 0
        onTriggered: root.cursorOn = !root.cursorOn
    }

    function handleKey(key, text, modifiers) {
        if (!root.model)
            return

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
            root.model.sendQtKey(key, mods)   // 特殊键（方向/编辑/翻页/功能键）、Ctrl 组合、以及非 ASCII 之外的兜底都走键盘表
            if (isPlainText && text.length > 0 && first > 0x7f)   // 非 ASCII 文本（中文/IME 提交的串）：sendQtKey 刻意只认 ASCII，这里补发一次
                root.model.typeText(text)
        }

        Terminal.flushInput(root.blockId)   // 把模型攒好的字节交给桥（→ TerminalSession::writeBytes → terminal/write 或扩展后端）
    }

    // 键盘入口：本视图独占焦点，事件才会到这里；**必须 accepted = true**，
    // 否则未处理的按键会继续往父链走（宿主快捷键/页面导航会把它吞掉，表现为"敲了没反应"）。
    Keys.onPressed: (event) => {
        if (!root.model) {
            event.accepted = false
            return
        }

        // 粘贴：Ctrl+V / Ctrl+Shift+V（Ctrl+C 仍留给 shell 当 中断，复制以后走 Ctrl+Shift+C）。
        if (event.key === Qt.Key_V && (event.modifiers & Qt.ControlModifier)) {
            Terminal.pasteFromClipboard(root.blockId)
            event.accepted = true
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

    Rectangle {   // 只读角标（输入被另一个窗口接管时）
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

                    property var rowCells: (index >= 0 && index < root.rows.length)   // 本行的段（来自 root.rows[index]）；整屏重画/脏行更新都会换掉这个数组引用
                                           ? root.rows[index] : null

                    property bool cursorHere: root.model !== null   // 光标画在哪一格（只有光标所在那一行需要它）
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
                        ctx.fillStyle = root.defaultBg   // 底色：整行画一次默认背景（段里的 bg 再覆盖）
                        ctx.fillRect(0, 0, width, height)

                        if (!rowCells)
                            return

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

                            if (text.length === 0) {   // 宽字符续格：占位但不画（首格已经占了两格）—— 用空格占宽度即可
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
                            if (!bgDefault || reverse) {   // 默认底色：第 ① 遍已经把整行铺成 defaultBg，这里只画"非默认"的段
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

    MouseArea {
        id: inputArea
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton
        onPressed: root.grabFocus()   // 只"要焦点"，不拦别的事：以后要加选区的拖动也走这里，不至于把焦点弄丢
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
