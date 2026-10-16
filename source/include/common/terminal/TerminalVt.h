#pragma once

// TerminalVt —— 终端 VT 核心：把原始 PTY 字节（含 ANSI/VT 转义）解析成屏幕网格。
//
// 分层：TerminalBridge（协议状态）→ **本类（VT 状态）** → 网格 → QML 渲染。输入是字节、输出是格子
// 与脏行；不碰网络、不碰 QObject，可脱离窗口单测。
//
// 运行时消费者：`TerminalVtModel`（qmlbridge/**，QML 可见的桥）+ `TerminalView.qml`（逐格渲染）。
// 接线在 `TerminalSession` 的帧路径上 —— terminal/follow 的 snapshot/output 帧原样喂进来。
// 另有一条只跑字节的离线自检 `misc/tools/vt-selfcheck/`（不链 Qt）。
// 早先终端面板走的是页面里的 xterm.js（同一条字节流的另一条路）；两条路的取舍与理由见
// source/3rdparty/libvterm/README.md 的「为什么内嵌」。
//
// 解析器用内嵌的 libvterm（source/3rdparty/libvterm，只解析不渲染）：活的屏幕归它持有，本类只补
// 两件它不管的事 —— 回滚历史、以及"哪几行变了"。
//
// 本头文件**不出现任何 libvterm 类型**：一是别把内嵌库的类型带进工程契约，二是日后换解析器只需改 .cpp。

#include <cstdint>
#include <deque>
#include <set>
#include <string>
#include <vector>

struct VTerm;
struct VTermScreen;
struct VTermState;
struct TerminalVtCallbacks;

// 一个格的属性。颜色是 0xRRGGBB；"是否终端默认色"必须单独记 —— SGR 没写前景时不能拿 0 当黑，
// 得留给主题的默认色。位定义见 Flag。
struct TermStyle
{
	uint32_t fg = 0x000000;
	uint32_t bg = 0x000000;
	uint16_t flags = 0;

	enum Flag : uint16_t
	{
		FgDefault = 1u << 0,   // fg 无效，取主题默认前景
		BgDefault = 1u << 1,   // bg 无效，取主题默认背景
		Bold      = 1u << 2,
		Italic    = 1u << 3,
		Underline = 1u << 4,
		Blink     = 1u << 5,
		Reverse   = 1u << 6,
		Conceal   = 1u << 7,
		Strike    = 1u << 8,
	};

	bool test(Flag flag) const { return (flags & flag) != 0; }
	bool sameAs(const TermStyle& other) const
	{
		return fg == other.fg && bg == other.bg && flags == other.flags;
	}
};

// 一个格。chars = 基字符 + 组合字符（最多 6 个码点，与 libvterm 的上限一致），0 结尾。
// 宽字符占两格：首格 width=2 且带码点，紧随的续格 width=0 且 chars 全 0 —— 渲染时跳过续格，
// 别按"占 1 列的空格"处理，否则中日韩文本整行都会错位。
struct TermCell
{
	uint32_t chars[6] = { 0, 0, 0, 0, 0, 0 };
	uint8_t width = 1;
	TermStyle style;
};

class TerminalVt
{
public:
	TerminalVt(int rows, int cols, int scrollbackLines = 1000);
	~TerminalVt();

	TerminalVt(const TerminalVt&) = delete;
	TerminalVt& operator=(const TerminalVt&) = delete;

	// ---- 输入 ----
	// 灌入原始 PTY 字节。跨调用被拆开的 UTF-8 序列与转义序列由 libvterm 自己续上（编码器与
	// 解析器的状态都挂在 VTerm 上），所以**不需要**外部拼帧 —— 引擎逐 chunk 给的帧直接喂。
	void feed(const char* bytes, int len);
	// 硬复位（重连取新快照前调）：清屏、清回滚、清脏行。调色板会被重放一次（硬复位会把它冲掉）。
	void reset();
	// 改网格尺寸：libvterm 按 reflow 重排折行内容（回滚历史不重排）。
	void setSize(int rows, int cols);

	// ---- 键盘：生成要回写给 PTY 的字节 ----
	// 修饰键位。刻意不用 libvterm 的 VTermModifier 当契约，改库时不至于把调用点都带上。
	enum Modifier
	{
		ModNone  = 0x00,
		ModShift = 0x01,
		ModAlt   = 0x02,
		ModCtrl  = 0x04,
	};

	// 特殊键。功能键走 keyFunction（1 = F1）。
	enum SpecialKey
	{
		KeyEnter,
		KeyTab,
		KeyBackspace,
		KeyEscape,
		KeyUp,
		KeyDown,
		KeyLeft,
		KeyRight,
		KeyInsert,
		KeyDelete,
		KeyHome,
		KeyEnd,
		KeyPageUp,
		KeyPageDown,
	};

	// 可打印字符（含中文等）。modifiers 是 Modifier 的或值。
	void keyUnichar(uint32_t codepoint, int modifiers);
	void keySpecial(SpecialKey key, int modifiers);
	void keyFunction(int index, int modifiers);
	// 取走并清空积累的待写字节（回车/退格/方向键等生成的序列）
	std::vector<std::string> takeKeyOutput();

	// ---- 视口 ----
	int rows() const { return m_rows; }
	int cols() const { return m_cols; }
	// 可回滚的历史行数（不含屏幕本身）
	int scrollbackLines() const { return static_cast<int>(m_scrollback.size()); }
	// 0 = 贴着实时画面；n>0 = 往回收 n 行。滚在历史里时，屏幕最下面 n 行不在视口内。
	int viewOffset() const { return m_viewOffset; }
	void setViewOffset(int offset);

	// ---- 读取 ----
	// 视口第 viewRow 行（0 = 视口最顶）。返回**内部缓冲**，长度恒为 cols()，下次调用本函数前有效。
	// viewRow 越界返回空格行。
	const TermCell* viewRow(int viewRow) const;
	bool cursorVisible() const { return m_cursorVisible; }
	// 光标在视口里的行；<0 或 >=rows() 表示不在视口内（滚在历史里时会发生）
	int cursorViewRow() const { return m_cursorRow + m_viewOffset; }
	int cursorCol() const { return m_cursorCol; }
	// OSC 0/2 设的窗口标题（空 = 没设过）
	const std::string& title() const { return m_title; }

	// ---- 变化通知 ----
	// 自上次调用以来变化的视口行（升序、不重复）。视口整体换过（建/复位/改尺寸/滚屏/回滚/换回滚
	// 偏移）时返回全部 rows() 行 —— 调用方据此"补几行"或"整屏重画"，判据是 size() == rows()。
	std::vector<int> takeDirtyRows();

	// ---- 主题 ----
	// 覆盖 ANSI 前 16 色（索引 0-15）；索引 16-255 是标准色立方，不覆盖。不调则用 libvterm 内置默认。
	void setAnsiPalette(const uint32_t rgb[16]);

private:
	friend struct TerminalVtCallbacks;   // 回调签名要用 libvterm 的结构体，只能在 .cpp 里声明

	// 回滚里的一行。libvterm 的格结构不往外露，所以在这里就转成 TermCell。
	struct ScrollLine
	{
		std::vector<TermCell> cells;
		bool continuation = false;   // 上一行的折行续行（渲染时该行不另起一段）
	};

	// 视口行号 → 绝对行号（0 = 回滚历史最旧一行）；越界返回 -1
	int absoluteLine(int viewRow) const;
	// 把绝对行号对应的内容填进 m_rowBuffer（m_rowBuffer 是 mutable，所以这里能是 const）
	void readAbsoluteLine(int line) const;
	void applyAnsiPalette();

	VTerm* m_vt = nullptr;
	VTermScreen* m_screen = nullptr;
	VTermState* m_state = nullptr;

	int m_rows = 0;
	int m_cols = 0;
	int m_scrollbackLimit = 1000;
	std::deque<ScrollLine> m_scrollback;

	// 脏行，屏幕坐标（0 = 屏幕最顶）。m_fullRepaint 时不看它。
	std::set<int> m_dirtyRows;
	bool m_fullRepaint = true;

	int m_viewOffset = 0;

	bool m_cursorVisible = true;
	int m_cursorRow = 0;
	int m_cursorCol = 0;

	std::string m_title;

	// 键盘生成、待回写给 PTY 的字节
	std::vector<std::string> m_keyOutput;

	// viewRow() 的返回缓冲：按需从 libvterm 屏幕或回滚历史里拷一格
	mutable std::vector<TermCell> m_rowBuffer;

	uint32_t m_ansiPalette[16] = {};
	bool m_paletteSet = false;
};
