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

	// ② 期望内容
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
