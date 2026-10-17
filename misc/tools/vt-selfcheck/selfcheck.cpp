// VT 核心自检：把一段**真实的引擎抓包**喂给 TerminalVt，dump 出网格并做结构检查。
//
// 与 misc/tools/cpp-selfcheck 分开的原因：那边测的是"纯逻辑单元"（断言输入→输出），这边测的是
// "一整条真实字节流能不能被正确解释"——依赖内嵌的 C 库与一份抓包夹具，形态不一样，所以单独一个。
//
// 夹具：fixture/stream.bin（由 extract.py 从 capture.json 转出；capture.json 是引擎
// terminal/follow 流的原样抓包：1 份序列化重放串 + 28 帧实时输出，含 CJK、绝对定位、\e[K、\e[72X）。
// 期望内容写在 fixture/expected.txt：每行一个"视口里必须出现"的子串（# 开头是注释）。
//
// 检查分三类：① 结构性不变量（不出现 U+FFFD、宽字符续格严格配对）；② 期望内容；③ 光标与统计。

#include "common/terminal/TerminalVt.h"

#include <cstdio>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace
{
int g_failures = 0;

void check(bool ok, const std::string& what)
{
	std::printf("%s  %s\n", ok ? "PASS" : "FAIL", what.c_str());
	if (!ok)
		++g_failures;
}

// 码点 → UTF-8
std::string toUtf8(uint32_t code)
{
	std::string out;
	if (code < 0x80)
		out.push_back(static_cast<char>(code));
	else if (code < 0x800)
	{
		out.push_back(static_cast<char>(0xc0 | (code >> 6)));
		out.push_back(static_cast<char>(0x80 | (code & 0x3f)));
	}
	else if (code < 0x10000)
	{
		out.push_back(static_cast<char>(0xe0 | (code >> 12)));
		out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3f)));
		out.push_back(static_cast<char>(0x80 | (code & 0x3f)));
	}
	else
	{
		out.push_back(static_cast<char>(0xf0 | (code >> 18)));
		out.push_back(static_cast<char>(0x80 | ((code >> 12) & 0x3f)));
		out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3f)));
		out.push_back(static_cast<char>(0x80 | (code & 0x3f)));
	}
	return out;
}

// 一行格 → 文本。宽字符的续格（width==0）必须跳过，否则 CJK 行会多出空格。
std::string rowText(const TermCell* cells, int cols)
{
	std::string out;
	for (int col = 0; col < cols; ++col)
	{
		if (cells[col].width == 0)
			continue;
		for (int i = 0; i < 6 && cells[col].chars[i]; ++i)
			out += toUtf8(cells[col].chars[i]);
	}
	while (!out.empty() && (out.back() == ' ' || out.back() == '\t'))
		out.pop_back();
	return out;
}

// 定长帧文件：uint8 kind + uint32 length + payload（见 extract.py）
bool readFixture(const std::string& path, std::vector<std::pair<int, std::string>>& frames)
{
	// 用 *_s 变体：工程开了 SDLCheck(/sdl)，裸 fopen 会变成 C4996 **错误**。
	// 这里是自检自己的文件 IO，不掩盖 —— 直接换成安全版本，让自检与工程的严格度一致。
	FILE* file = nullptr;
	if (fopen_s(&file, path.c_str(), "rb") != 0 || !file)
		return false;

	for (;;)
	{
		unsigned char kind = 0;
		unsigned int length = 0;
		if (std::fread(&kind, 1, 1, file) != 1)
			break;
		if (std::fread(&length, 4, 1, file) != 1)
			break;
		std::string payload(length, '\0');
		if (length != 0 && std::fread(&payload[0], 1, length, file) != length)
			break;
		frames.emplace_back(static_cast<int>(kind), payload);
	}
	std::fclose(file);
	return true;
}

std::vector<std::string> readLines(const std::string& path)
{
	std::vector<std::string> lines;
	FILE* file = nullptr;
	if (fopen_s(&file, path.c_str(), "r") != 0 || !file)
		return lines;

	char buffer[4096];
	while (std::fgets(buffer, sizeof(buffer), file))
	{
		std::string line(buffer);
		while (!line.empty() && (line.back() == '\n' || line.back() == '\r'))
			line.pop_back();
		if (line.empty() || line[0] == '#')
			continue;
		lines.push_back(line);
	}
	std::fclose(file);
	return lines;
}
}

int main(int argc, char** argv)
{
	// 第三个参数给了就把报告直接写进那个文件：Windows 控制台的默认编码是 GBK，
	// 走管道抓 stdout 会把中文弄成乱码，落盘拿到的才是原样的 UTF-8 字节。
	FILE* report = nullptr;
	if (argc > 3 && freopen_s(&report, argv[3], "wb", stdout) != 0)
		std::fprintf(stderr, "警告：打不开报告文件 %s\n", argv[3]);

	const std::string fixturePath = argc > 1 ? argv[1] : "fixture/stream.bin";
	const std::string expectedPath = argc > 2 ? argv[2] : "fixture/expected.txt";

	std::vector<std::pair<int, std::string>> frames;
	if (!readFixture(fixturePath, frames))
	{
		std::printf("FAIL  读不到夹具 %s\n", fixturePath.c_str());
		return 1;
	}
	std::printf("夹具 %s：%zu 帧\n\n", fixturePath.c_str(), frames.size());

	TerminalVt terminal(26, 100);
	for (const auto& frame : frames)
	{
		if (frame.first == 0)
			terminal.reset();   // 快照 = 序列化重放串，喂之前先硬复位
		terminal.feed(frame.second.data(), static_cast<int>(frame.second.size()));
	}

	const int rows = terminal.rows();
	const int cols = terminal.cols();

	std::vector<std::string> lines;
	lines.reserve(static_cast<size_t>(rows));
	for (int row = 0; row < rows; ++row)
		lines.push_back(rowText(terminal.viewRow(row), cols));

	std::printf("网格 %dx%d，回滚历史 %d 行，光标 (row=%d, col=%d) 可见=%d，标题「%s」\n\n",
		cols, rows, terminal.scrollbackLines(), terminal.cursorViewRow(), terminal.cursorCol(),
		terminal.cursorVisible() ? 1 : 0, terminal.title().c_str());

	std::printf("---- 视口内容 ----\n");
	for (int row = 0; row < rows; ++row)
		std::printf("%2d|%s\n", row, lines[row].c_str());
	std::printf("---- 视口内容结束 ----\n\n");

	// ① 结构性不变量
	bool replacement = false;
	int widthBreakRow = -1;
	for (int row = 0; row < rows; ++row)
	{
		const TermCell* cells = terminal.viewRow(row);
		for (int col = 0; col < cols; ++col)
		{
			if (cells[col].chars[0] == 0xFFFD)
				replacement = true;
			if (cells[col].width == 2 && (col + 1 >= cols || cells[col + 1].width != 0))
				widthBreakRow = row;
			if (cells[col].width == 0 && (col == 0 || cells[col - 1].width != 2))
				widthBreakRow = row;
		}
	}
	check(!replacement, "视口里没有 U+FFFD（UTF-8 没被切断）");
	check(widthBreakRow < 0, "宽字符续格严格配对（width=2 后面必跟 width=0）");

	// ③ 改尺寸（面板拉伸时每帧都会走这条路）：libvterm 的 screen_resize 在**退化尺寸**上会
	//    abort（实测 2 列 1 行 → "screen_resize failed to update cursor position" + 0xC0000409），
	//    所以这里把"喂过真实字节流之后再改尺寸"固化成用例：能跑完就说明这条路活着。
	{
		TerminalVt resized(26, 100);
		for (const auto& frame : frames)
		{
			if (frame.first == 0)
				resized.reset();
			resized.feed(frame.second.data(), static_cast<int>(frame.second.size()));
		}
		resized.takeDirtyRows();

		resized.setSize(30, 120);
		const bool grew = resized.rows() == 30 && resized.cols() == 120;
		check(grew, "改尺寸：26x100 → 30x120 生效");

		resized.setSize(26, 100);
		check(resized.rows() == 26 && resized.cols() == 100, "改尺寸：再改回 26x100 生效");

		// 改尺寸之后还要能继续吃字节（内容不烂）
		const char after[] = "\r\nafter-resize-ok";
		resized.feed(after, static_cast<int>(sizeof(after) - 1));
		resized.takeDirtyRows();
		bool foundAfter = false;
		for (int row = 0; row < resized.rows() && !foundAfter; ++row)
			foundAfter = rowText(resized.viewRow(row), resized.cols()).find("after-resize-ok") != std::string::npos;
		check(foundAfter, "改尺寸后继续喂字节：新内容出现在网格里");
	}

	// ③ 键盘 → 字节（"按键到底有没有变成要回写 PTY 的字节"）。
	//    这一段是自绘终端唯一的输入出口：QML 的 Keys.onPressed → TerminalVtModel → 本类的键盘表。
	//    期望值逐条对着 3rdparty/libvterm/src/keyboard.c 的 keycodes 表核过（含 Ctrl/Alt/Shift 与
	//    DECCKM 应用光标键），不是"跑出来什么就写什么"。
	{
		std::printf("\n键盘 → 字节（对照 libvterm keyboard.c 的 keycodes 表）\n");
		TerminalVt keyboard(24, 80);
		keyboard.reset();
		keyboard.takeKeyOutput();

		const auto utf8 = [](uint32_t codepoint) {
			return toUtf8(codepoint);
		};
		const auto hexdump = [](const std::string& bytes) {
			std::string out;
			char piece[8];
			for (unsigned char byte : bytes)
			{
				std::snprintf(piece, sizeof(piece), "%02x ", byte);
				out += piece;
			}
			if (!out.empty())
				out.pop_back();
			return out;
		};
		// 把"上一步攒下的字节"拼成一个串（一个按键可能产出多段）
		const auto drain = [&keyboard]() {
			std::string joined;
			for (const std::string& chunk : keyboard.takeKeyOutput())
				joined += chunk;
			return joined;
		};

		// 逐条核对：动作 → 期望字节。给的是"按一下键"，比较的是整套字节。
		const auto expectKey = [&](const char* what, const std::function<void()>& act, const std::string& expected) {
			act();
			const std::string got = drain();
			check(got == expected, std::string(what) + " → " + hexdump(expected)
				+ (got == expected ? "" : ("（实际 " + hexdump(got) + "）")));
		};

		// ① 普通字符（含 Shift：unichar 会剥掉 Shift）
		expectKey("'a'", [&] { keyboard.keyUnichar('a', TerminalVt::ModNone); }, "a");
		expectKey("Shift+'a'", [&] { keyboard.keyUnichar('A', TerminalVt::ModShift); }, "A");
		expectKey("'1'", [&] { keyboard.keyUnichar('1', TerminalVt::ModNone); }, "1");
		expectKey("空格", [&] { keyboard.keyUnichar(' ', TerminalVt::ModNone); }, " ");

		// ② Ctrl 组合（可打印键 → C0 控制码；非 a-z 走 CSI u）
		expectKey("Ctrl+'c'", [&] { keyboard.keyUnichar('c', TerminalVt::ModCtrl); }, std::string(1, '\x03'));
		expectKey("Ctrl+'d'", [&] { keyboard.keyUnichar('d', TerminalVt::ModCtrl); }, std::string(1, '\x04'));
		expectKey("Ctrl+'1'", [&] { keyboard.keyUnichar('1', TerminalVt::ModCtrl); }, "\x1b[49;5u");

		// ③ Alt 组合（前缀 ESC）。⚠️ 写成两段字面量：`"\x1ba"` 会被当成十六进制转义 \x1ba（超范围）
		expectKey("Alt+'a'", [&] { keyboard.keyUnichar('a', TerminalVt::ModAlt); }, "\x1b" "a");

		// ④ 特殊键
		expectKey("Enter", [&] { keyboard.keySpecial(TerminalVt::KeyEnter, TerminalVt::ModNone); }, "\r");
		expectKey("Tab", [&] { keyboard.keySpecial(TerminalVt::KeyTab, TerminalVt::ModNone); }, "\t");
		expectKey("Shift+Tab", [&] { keyboard.keySpecial(TerminalVt::KeyTab, TerminalVt::ModShift); }, "\x1b[Z");
		expectKey("Backspace", [&] { keyboard.keySpecial(TerminalVt::KeyBackspace, TerminalVt::ModNone); }, "\x7f");
		expectKey("Escape", [&] { keyboard.keySpecial(TerminalVt::KeyEscape, TerminalVt::ModNone); }, "\x1b");
		expectKey("Up", [&] { keyboard.keySpecial(TerminalVt::KeyUp, TerminalVt::ModNone); }, "\x1b[A");
		expectKey("Down", [&] { keyboard.keySpecial(TerminalVt::KeyDown, TerminalVt::ModNone); }, "\x1b[B");
		expectKey("Left", [&] { keyboard.keySpecial(TerminalVt::KeyLeft, TerminalVt::ModNone); }, "\x1b[D");
		expectKey("Right", [&] { keyboard.keySpecial(TerminalVt::KeyRight, TerminalVt::ModNone); }, "\x1b[C");
		expectKey("Home", [&] { keyboard.keySpecial(TerminalVt::KeyHome, TerminalVt::ModNone); }, "\x1b[H");
		expectKey("End", [&] { keyboard.keySpecial(TerminalVt::KeyEnd, TerminalVt::ModNone); }, "\x1b[F");
		expectKey("Insert", [&] { keyboard.keySpecial(TerminalVt::KeyInsert, TerminalVt::ModNone); }, "\x1b[2~");
		expectKey("Delete", [&] { keyboard.keySpecial(TerminalVt::KeyDelete, TerminalVt::ModNone); }, "\x1b[3~");
		expectKey("PageUp", [&] { keyboard.keySpecial(TerminalVt::KeyPageUp, TerminalVt::ModNone); }, "\x1b[5~");
		expectKey("PageDown", [&] { keyboard.keySpecial(TerminalVt::KeyPageDown, TerminalVt::ModNone); }, "\x1b[6~");
		expectKey("Shift+Up", [&] { keyboard.keySpecial(TerminalVt::KeyUp, TerminalVt::ModShift); }, "\x1b[1;2A");

		// ⑤ 功能键（F1-F4 走 SS3，F5 起走 CSI 数字）
		expectKey("F1", [&] { keyboard.keyFunction(1, TerminalVt::ModNone); }, "\x1bOP");
		expectKey("F4", [&] { keyboard.keyFunction(4, TerminalVt::ModNone); }, "\x1bOS");
		expectKey("F5", [&] { keyboard.keyFunction(5, TerminalVt::ModNone); }, "\x1b[15~");
		expectKey("F12", [&] { keyboard.keyFunction(12, TerminalVt::ModNone); }, "\x1b[24~");

		// ⑥ DECCKM（应用光标键模式，shell 里 readline/vim 会开）：方向键变成 SS3
		keyboard.feed("\x1b[?1h", 5);   // DECCKM set
		keyboard.takeDirtyRows();
		keyboard.takeKeyOutput();
		expectKey("Up（DECCKM 开）", [&] { keyboard.keySpecial(TerminalVt::KeyUp, TerminalVt::ModNone); }, "\x1bOA");
		keyboard.feed("\x1b[?1l", 5);   // DECCKM reset
		keyboard.takeDirtyRows();
		keyboard.takeKeyOutput();
		expectKey("Up（DECCKM 关）", [&] { keyboard.keySpecial(TerminalVt::KeyUp, TerminalVt::ModNone); }, "\x1b[A");

		// ⑦ 一串连打（真实输入的样子）：逐键攒、最后一次性取走
		keyboard.keyUnichar('e', TerminalVt::ModNone);
		keyboard.keyUnichar('c', TerminalVt::ModNone);
		keyboard.keyUnichar('h', TerminalVt::ModNone);
		keyboard.keyUnichar('o', TerminalVt::ModNone);
		keyboard.keySpecial(TerminalVt::KeyEnter, TerminalVt::ModNone);
		const std::string typed = drain();
		check(typed == "echo\r", "连打 \"echo\" + Enter → " + hexdump(typed) + "（实际 " + hexdump(typed) + "）");
	}

	// ⑤ 光标位置的写入隔离（用户报的"用箭头移动后再输入会跳到前面/盖住已打印输出"）。
	//    真机上出问题的根因是"PTY 的 cols 与本地 VT 的 cols 不是一个数"，但这条用例守的是**核心不变量**：
	//    在 VT 里，一个可打印字符只能写到**光标所在的那一格**，别的格子一格都不能动。
	//    只要这条不变量在，剩下的就只是"两边尺寸对齐"的接线问题。
	{
		std::printf("\n光标定位写入（箭头移动后继续输入，只动光标那一格）\n");
		TerminalVt vt(6, 20);
		vt.reset();
		vt.takeDirtyRows();

		// 一屏"提示符 + 两行已打印输出 + 提示符"，模拟 shell 的画面
		const char screen[] = "PS> abcdef\r\noutput-line-1\r\noutput-line-2\r\nPS> 0123";
		vt.feed(screen, static_cast<int>(sizeof(screen) - 1));
		vt.takeDirtyRows();

		// 先给整屏拍快照（每格的文本 + 宽度），后面逐格比较
		const auto snapshot = [&vt]() {
			std::vector<std::pair<std::string, int>> cells;
			for (int row = 0; row < vt.rows(); ++row)
			{
				const TermCell* line = vt.viewRow(row);
				for (int col = 0; col < vt.cols(); ++col)
					cells.emplace_back(rowText(line + col, 1), static_cast<int>(line[col].width));
			}
			return cells;
		};
		const std::vector<std::pair<std::string, int>> before = snapshot();

		// 光标此刻在最后一行 "PS> 0123" 之后：row=3, col=11
		const int rowBefore = vt.cursorViewRow();
		const int colBefore = vt.cursorCol();

		// 模拟用户按 4 次 ←（shell 回显 ESC[D），再敲 "XY"
		const char lefts[] = "\x1b[D\x1b[D\x1b[D\x1b[D";
		vt.feed(lefts, static_cast<int>(sizeof(lefts) - 1));
		vt.takeDirtyRows();
		check(vt.cursorViewRow() == rowBefore && vt.cursorCol() == colBefore - 4,
			"4 次 ESC[D：光标仍在本行、左移 4 格");

		vt.feed("XY", 2);
		vt.takeDirtyRows();
		const std::vector<std::pair<std::string, int>> after = snapshot();

		// 只有 (rowBefore, colBefore-4) 与 (rowBefore, colBefore-3) 这两格变成 X / Y
		int changed = 0;
		bool onlyCursorCellsChanged = true;
		bool xAtCursor = false;
		bool yAtCursor = false;
		for (int row = 0; row < vt.rows(); ++row)
		{
			for (int col = 0; col < vt.cols(); ++col)
			{
				const size_t index = static_cast<size_t>(row) * static_cast<size_t>(vt.cols())
					+ static_cast<size_t>(col);
				if (after[index] == before[index])
					continue;
				++changed;
				if (row == rowBefore && col == colBefore - 4 && after[index].first == "X")
					xAtCursor = true;
				else if (row == rowBefore && col == colBefore - 3 && after[index].first == "Y")
					yAtCursor = true;
				else
					onlyCursorCellsChanged = false;
			}
		}
		check(xAtCursor, "第一个字符 X 落在光标那一格");
		check(yAtCursor, "第二个字符 Y 落在光标右边那一格");
		check(changed == 2, "整屏只有 2 格变化（实际 " + std::to_string(changed) + "）");
		check(onlyCursorCellsChanged, "已打印的输出行一格都没动");

		// 覆盖"不可修改的输出"这件事：把输出行逐字比对一遍（只读区不该被写）
		bool outputIntact = true;
		for (int row = 1; row <= 2; ++row)
		{
			std::string line;
			const TermCell* cells = vt.viewRow(row);
			for (int col = 0; col < vt.cols(); ++col)
				if (cells[col].width != 0)
					line += rowText(cells + col, 1);
			while (!line.empty() && line.back() == ' ')
				line.pop_back();
			if (line != (row == 1 ? "output-line-1" : "output-line-2"))
				outputIntact = false;
		}
		check(outputIntact, "两行已打印输出原样（没被输入覆盖）");

		// 光标跑到行首再输入：只能动第 0 格，绝不"跳到上一行"
		vt.feed("\r", 1);
		vt.takeDirtyRows();
		check(vt.cursorViewRow() == rowBefore && vt.cursorCol() == 0, "回车后光标回到本行行首（不跨行）");
		vt.feed("Z", 1);
		vt.takeDirtyRows();
		check(rowText(vt.viewRow(rowBefore) + 0, 1) == "Z", "行首输入落在第 0 格");
	}

	// ⑥ 列边界 / 自动折行 / 宽字符（宽字符占两格，续格必须 width=0）
	{
		std::printf("\n列边界与宽字符\n");
		TerminalVt vt(4, 10);
		vt.reset();
		vt.takeDirtyRows();

		// 写满最后一行前先清干净：直接从第 0 行写 10 个字符
		vt.feed("0123456789", 10);
		vt.takeDirtyRows();
		check(vt.cursorCol() == 9, "写满 10 列后光标停在第 10 格（末列挂起）");
		vt.feed("A", 1);
		vt.takeDirtyRows();
		check(rowText(vt.viewRow(1) + 0, 10).substr(0, 1) == "A" && vt.cursorCol() == 1,
			"第 11 个字符折到下一行第 0 格，光标跟进到第 2 格");
		check(rowText(vt.viewRow(0) + 0, 10) == "0123456789", "第一行 10 格内容没被写坏");

		// 宽字符：在末列放不下时必须整体折行，且首格 width=2、续格 width=0
		TerminalVt wide(4, 10);
		wide.reset();
		wide.takeDirtyRows();
		wide.feed("abcdefgh", 8);            // 占 0..7，剩下 8、9 两格
		wide.feed("\xe4\xb8\xad", 3);        // "中"（宽 2）正好占 8、9
		wide.takeDirtyRows();
		const TermCell* wideRow = wide.viewRow(0);
		check(wideRow[8].width == 2, "宽字符首格 width=2");
		check(wideRow[9].width == 0, "宽字符续格 width=0");
		wide.feed("\xe6\x96\x87", 3);        // "文"（宽 2）在末列放不下 ⇒ 折到下一行
		wide.takeDirtyRows();
		const TermCell* nextRow = wide.viewRow(1);
		check(nextRow[0].width == 2 && nextRow[1].width == 0, "放不下的宽字符整体折到下一行");
		check(wide.cursorViewRow() == 1 && wide.cursorCol() == 2, "折行后光标在下一行第 3 格");

		// 改尺寸后折行要重排（面板拉伸时每帧都可能发生）
		TerminalVt reflow(4, 20);
		reflow.reset();
		reflow.takeDirtyRows();
		reflow.feed("ABCDEFGHIJ", 10);
		reflow.takeDirtyRows();
		reflow.setSize(4, 5);                // 20 列缩到 5 列：10 个字符要折成两行
		reflow.takeDirtyRows();
		check(rowText(reflow.viewRow(0) + 0, 5) == "ABCDE", "缩窄后第 1 行 = ABCDE");
		check(rowText(reflow.viewRow(1) + 0, 5) == "FGHIJ", "缩窄后第 2 行 = FGHIJ");
	}

	// ④ 期望内容
	std::string joined;
	for (const std::string& line : lines)
	{
		joined += line;
		joined += '\n';
	}
	const std::vector<std::string> expected = readLines(expectedPath);
	std::printf("\n期望内容（%s）：%zu 条\n", expectedPath.c_str(), expected.size());
	for (const std::string& needle : expected)
		check(joined.find(needle) != std::string::npos, "视口里出现「" + needle + "」");

	std::printf("\n%s（%d 项失败）\n", g_failures == 0 ? "全部通过" : "有失败项", g_failures);
	return g_failures == 0 ? 0 : 1;
}
