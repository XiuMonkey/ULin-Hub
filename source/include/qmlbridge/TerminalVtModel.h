#pragma once

// TerminalVtModel —— 终端网格的 QML 可见桥：VT 状态（TerminalVt）与渲染（TerminalView.qml）之间那一层。
//
// 分层：TerminalSession（协议状态）→ TerminalVt（VT 状态）→ **本类（QML 面）** → TerminalView.qml（逐格画）。
// 本类只做三件事：① 把会话身份/状态露给 QML（rows/cols/state/statusText/readOnly/title）；
// ② 网格**按行**暴露 + 脏行通知（渲染只重画脏行）；③ 把 QML 的键盘事件翻译成回写 PTY 的字节。
// 解析不在这里（TerminalVt），画也不在这里（QML）。
//
// 为什么"按行 + 按样式合并的段"而不是"每格一个 QVariantMap"：一屏 80×30 就是 2400 格，每帧重发
// 2400 个 map 光构造就吃满主线程；合并成"同样式的一段"之后，一整行命令行的文本通常只剩个位数段，
// QML 侧拿到的是小字符串。渲染侧缓存住整屏，脏行来了只换那几行。
//
// 为什么键盘编码放在 C++：libvterm 的键盘表（vterm_keyboard_key/unichar）是权威 —— 方向键在
// "普通/DECCKM 应用模式"下序列不同、Ctrl/Alt 的组合规则也在库里，QML 只该做"KeyEvent → 码点/特殊键"。

#include "common/terminal/TerminalVt.h"

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>

class TerminalVtModel : public QObject
{
	Q_OBJECT
		Q_PROPERTY(int rows READ rows NOTIFY sizeChanged)                     // 网格行数
		Q_PROPERTY(int cols READ cols NOTIFY sizeChanged)                     // 网格列数
		Q_PROPERTY(int scrollback READ scrollback NOTIFY scrollbackChanged)   // 可回滚历史行数
		Q_PROPERTY(int viewOffset READ viewOffset NOTIFY viewOffsetChanged)   // 0 = 贴实时画面
		Q_PROPERTY(int cursorRow READ cursorRow NOTIFY cursorChanged)         // 视口行；-1 = 不在视口内
		Q_PROPERTY(int cursorCol READ cursorCol NOTIFY cursorChanged)
		Q_PROPERTY(bool cursorVisible READ cursorVisible NOTIFY cursorChanged)
		Q_PROPERTY(QString title READ title NOTIFY titleChanged)
		Q_PROPERTY(QString state READ state NOTIFY stateChanged)
		Q_PROPERTY(QString statusText READ statusText NOTIFY statusTextChanged)
		Q_PROPERTY(bool readOnly READ readOnly NOTIFY readOnlyChanged)

public:
	explicit TerminalVtModel(QObject* parent = nullptr);

	// ---- 会话侧驱动（TerminalSession 调）----
	// 灌一帧原始 PTY 字节。**逐 chunk 直接喂**：跨帧被切开的 UTF-8/转义序列由 libvterm 自己续。
	void feed(const QByteArray& bytes);
	// 快照帧：xterm 序列化重放串语义上就是"整屏重画"，先硬复位再喂（调色板会被重放）
	void feedSnapshot(const QByteArray& bytes);
	// 键盘等要回写给 PTY 的字节（模型只攒着，由会话取走）
	QByteArray takePendingInput();
	bool hasPendingInput() const { return !m_pendingInput.isEmpty(); }
	// 会话状态的镜像（QML 据此画状态行 / 只读提示）
	void setSessionState(const QString& state, const QString& statusText, const QString& title, bool readOnly);
	// 网格尺寸跟着面板走：会话把引擎侧尺寸推给引擎，这里只同步本地 VT
	void setGridSize(int rows, int cols);

	// ---- QML 面：网格读取 ----
	// 返回 [first, last] 闭区间内每行的**按样式合并段**，JSON 数组的数组：
	//   [[[文本, fg, bg, flags], ...], ...]
	// 文本为空串 = 宽字符的续格，渲染必须跳过这一格（宽字符首格占两格）。
	// 越界行给空数组（渲染画成空行）。一次调用返回多行是刻意为之：整屏重画只走一次 QML↔C++ 边界。
	Q_INVOKABLE QString rowsJson(int first, int last) const;

	// ---- QML 面：键盘 ----
	// 可打印字符（含中文，QKeyEvent::text 给什么就发什么；Ctrl 组合传空串，由 sendKey 处理）
	Q_INVOKABLE void typeText(const QString& text);
	// 特殊键：name 见 TerminalVt::SpecialKey 的 kebab 名（enter/tab/backspace/escape/up/down/left/right/
	// insert/delete/home/end/pageup/pagedown），modifiers = Shift 1 / Alt 2 / Ctrl 4 的或值
	Q_INVOKABLE void sendKey(const QString& name, int modifiers);
	// F1..F24（index 从 1 起）
	Q_INVOKABLE void sendFunction(int index, int modifiers);
	// Qt::Key_* 里映射不到名字键的那批（Backspace/Tab/Return/Enter/Escape + 方向/编辑/翻页/功能键）
	Q_INVOKABLE void sendQtKey(int qtKey, int modifiers);

	// ---- QML 面：视口 / 尺寸 / 主题 ----
	Q_INVOKABLE void scrollBy(int lines);        // 滚轮：正数 = 往历史里滚
	Q_INVOKABLE void scrollToBottom();
	Q_INVOKABLE void reset();
	// 面板量出来的网格尺寸。**真变了才动 VT**（改尺寸会整屏重排 + 整屏重画，别每帧调）。
	Q_INVOKABLE void resizeGrid(int rows, int cols);
	// 16 个 0xRRGGBB（QML 传 "RRGGBB" 或 "#RRGGBB"；空串 = 该索引用库内置默认）
	Q_INVOKABLE void setAnsiPalette(const QStringList& colors);

	int rows() const { return m_vt.rows(); }
	int cols() const { return m_vt.cols(); }
	int scrollback() const { return m_vt.scrollbackLines(); }
	int viewOffset() const { return m_vt.viewOffset(); }
	int cursorRow() const { return m_vt.cursorViewRow(); }
	int cursorCol() const { return m_vt.cursorCol(); }
	bool cursorVisible() const { return m_vt.cursorVisible(); }
	QString title() const { return m_title; }
	QString state() const { return m_state; }
	QString statusText() const { return m_statusText; }
	bool readOnly() const { return m_readOnly; }

signals:
	// 自上次通知以来变化的视口行（升序、不重复；size() == rows() 表示整屏）
	void dirtyRowsChanged(const QVariantList& rows);
	void sizeChanged();
	void scrollbackChanged();
	void viewOffsetChanged();
	void cursorChanged();
	void titleChanged();
	void stateChanged();
	void statusTextChanged();
	void readOnlyChanged();
	// 攒下了要回写 PTY 的字节（会话据此取走并写 terminal/write）
	void inputReady();

private:
	// VT 状态变过之后的统一收尾：光标/脏行/回滚/标题的通知都从这里出，保证"喂完必通知"
	void afterFeed();
	void emitDirty();
	// 库键盘表攒下的序列 → m_pendingInput（发 inputReady）
	void collectKeyOutput();

	TerminalVt m_vt;
	QByteArray m_pendingInput;

	QString m_title;
	QString m_state = QStringLiteral("idle");
	QString m_statusText;
	bool m_readOnly = false;

	// 上次通知过的回滚行数（只在真变了才发 scrollbackChanged）
	int m_lastScrollback = 0;

	// 宿主给的 ANSI 前 16 色（0xRRGGBB）；m_paletteCount==0 = 还没设过，用库内置默认
	uint32_t m_paletteRgb[16] = {};
	int m_paletteCount = 0;
};
