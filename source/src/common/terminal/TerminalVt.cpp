#include "common/terminal/TerminalVt.h"

#include <vterm.h>

#include <cstring>
#include <utility>

namespace
{
// 宽字符续格的哨兵：libvterm 在宽字符右边那格里把 chars[0] 写成 (uint32_t)-1
// （见 3rdparty/libvterm/src/screen.c 的 vterm_screen_get_cell）。
constexpr uint32_t kWideContinuation = 0xFFFFFFFFu;

// 与 VTERM_MAX_CHARS_PER_CELL 同值；TermCell::chars 也是这个长度。
constexpr int kCharsPerCell = 6;

uint32_t rgb24(const VTermColor& color)
{
	return (static_cast<uint32_t>(color.rgb.red) << 16)
		| (static_cast<uint32_t>(color.rgb.green) << 8)
		| static_cast<uint32_t>(color.rgb.blue);
}

// libvterm 的色 → 0xRRGGBB。默认前景/背景用出参标志带出去，具体色值留给主题决定；
// 索引色交给 libvterm 自己解 —— 它自带 256 色默认表，我们不必再抄一份调色板数学。
uint32_t resolveColor(const VTermColor& color, VTermScreen* screen, bool isForeground, bool* isDefault)
{
	*isDefault = isForeground ? VTERM_COLOR_IS_DEFAULT_FG(&color) : VTERM_COLOR_IS_DEFAULT_BG(&color);
	if (*isDefault)
		return 0;

	VTermColor resolved = color;
	vterm_screen_convert_color_to_rgb(screen, &resolved);
	return rgb24(resolved);
}

// 反向：0xRRGGBB + 默认标志 → VTermColor。只给"把回滚行还回屏幕"用（sb_popline）。
// 索引色在这里一律还原成 RGB —— 与本类"读出来时已解析"的口径一致。
VTermColor makeVTermColor(uint32_t rgb, bool isDefault, bool isForeground)
{
	VTermColor color = {};
	if (isDefault)
	{
		color.type = static_cast<uint8_t>(isForeground ? VTERM_COLOR_DEFAULT_FG : VTERM_COLOR_DEFAULT_BG);
		return color;
	}
	vterm_color_rgb(&color, static_cast<uint8_t>((rgb >> 16) & 0xff),
		static_cast<uint8_t>((rgb >> 8) & 0xff), static_cast<uint8_t>(rgb & 0xff));
	return color;
}

// 屏幕格 → 本类的格。宽字符的续格归一化成 width=0 / chars 全 0，渲染侧照此跳过。
void toTermCell(const VTermScreenCell& src, VTermScreen* screen, TermCell& out)
{
	out = TermCell();
	if (src.chars[0] == kWideContinuation)
	{
		out.width = 0;
		return;
	}

	for (int i = 0; i < kCharsPerCell; ++i)
		out.chars[i] = src.chars[i];

	out.width = static_cast<uint8_t>(src.width < 1 ? 1 : src.width);

	bool fgDefault = false;
	bool bgDefault = false;
	out.style.fg = resolveColor(src.fg, screen, true, &fgDefault);
	out.style.bg = resolveColor(src.bg, screen, false, &bgDefault);
	if (fgDefault)
		out.style.flags |= TermStyle::FgDefault;
	if (bgDefault)
		out.style.flags |= TermStyle::BgDefault;
	if (src.attrs.bold)
		out.style.flags |= TermStyle::Bold;
	if (src.attrs.italic)
		out.style.flags |= TermStyle::Italic;
	if (src.attrs.underline)
		out.style.flags |= TermStyle::Underline;
	if (src.attrs.blink)
		out.style.flags |= TermStyle::Blink;
	if (src.attrs.reverse)
		out.style.flags |= TermStyle::Reverse;
	if (src.attrs.conceal)
		out.style.flags |= TermStyle::Conceal;
	if (src.attrs.strike)
		out.style.flags |= TermStyle::Strike;
}

// 空白格：字符全 0、宽 1、前景/背景都取主题默认。
// 用来把"回滚行不满一行"的尾巴补满（见 onPopLine）—— 不能留默认 fg/bg（那样补出来是黑底黑字，
// 亮色主题下就是一道黑边）。
TermCell blankCell()
{
	TermCell cell;
	cell.style.flags = TermStyle::FgDefault | TermStyle::BgDefault;
	return cell;
}

// 本类的格 → 屏幕格（sb_popline 用）
void fromTermCell(const TermCell& src, VTermScreenCell& out)
{
	std::memset(&out, 0, sizeof(out));
	if (src.width == 0)
	{
		out.chars[0] = kWideContinuation;
		out.width = 1;
	}
	else
	{
		for (int i = 0; i < kCharsPerCell; ++i)
			out.chars[i] = src.chars[i];
		out.width = static_cast<char>(src.width);
	}

	out.fg = makeVTermColor(src.style.fg, src.style.test(TermStyle::FgDefault), true);
	out.bg = makeVTermColor(src.style.bg, src.style.test(TermStyle::BgDefault), false);
	out.attrs.underline = src.style.test(TermStyle::Underline) ? VTERM_UNDERLINE_SINGLE : VTERM_UNDERLINE_OFF;
	out.attrs.bold = src.style.test(TermStyle::Bold);
	out.attrs.italic = src.style.test(TermStyle::Italic);
	out.attrs.blink = src.style.test(TermStyle::Blink);
	out.attrs.reverse = src.style.test(TermStyle::Reverse);
	out.attrs.conceal = src.style.test(TermStyle::Conceal);
	out.attrs.strike = src.style.test(TermStyle::Strike);
}

// 本类的 SpecialKey → libvterm 的 VTermKey
VTermKey toVTermKey(TerminalVt::SpecialKey key)
{
	switch (key)
	{
	case TerminalVt::KeyEnter:     return VTERM_KEY_ENTER;
	case TerminalVt::KeyTab:       return VTERM_KEY_TAB;
	case TerminalVt::KeyBackspace: return VTERM_KEY_BACKSPACE;
	case TerminalVt::KeyEscape:    return VTERM_KEY_ESCAPE;
	case TerminalVt::KeyUp:        return VTERM_KEY_UP;
	case TerminalVt::KeyDown:      return VTERM_KEY_DOWN;
	case TerminalVt::KeyLeft:      return VTERM_KEY_LEFT;
	case TerminalVt::KeyRight:     return VTERM_KEY_RIGHT;
	case TerminalVt::KeyInsert:    return VTERM_KEY_INS;
	case TerminalVt::KeyDelete:    return VTERM_KEY_DEL;
	case TerminalVt::KeyHome:      return VTERM_KEY_HOME;
	case TerminalVt::KeyEnd:       return VTERM_KEY_END;
	case TerminalVt::KeyPageUp:    return VTERM_KEY_PAGEUP;
	case TerminalVt::KeyPageDown:  return VTERM_KEY_PAGEDOWN;
	}
	return VTERM_KEY_NONE;
}

// 本类的 Modifier 位 → libvterm 的 VTermModifier。两者当前同值，但仍逐位翻译 ——
// 契约分开写，改库时不会静默错位。
VTermModifier toVTermModifier(int modifiers)
{
	int value = VTERM_MOD_NONE;
	if (modifiers & TerminalVt::ModShift)
		value |= VTERM_MOD_SHIFT;
	if (modifiers & TerminalVt::ModAlt)
		value |= VTERM_MOD_ALT;
	if (modifiers & TerminalVt::ModCtrl)
		value |= VTERM_MOD_CTRL;
	return static_cast<VTermModifier>(value);
}
}

// libvterm 的回调签名要用它的结构体（VTermRect / VTermScreenCell），没法定在前向声明的类里，
// 只能集中在本 .cpp；靠 TerminalVt 的 friend 声明拿私有成员。
struct TerminalVtCallbacks
{
	// 有格子变了。damage 已按行合并，所以 rect 的行范围就是要重画的行。
	static int onDamage(VTermRect rect, void* user)
	{
		TerminalVt* self = static_cast<TerminalVt*>(user);
		for (int row = rect.start_row; row < rect.end_row; ++row)
			self->m_dirtyRows.insert(row);
		return 1;
	}

	// 整块搬移（滚屏等）：屏幕行号整体错位，脏行记录作废，直接判"整屏重画"。
	static int onMoveRect(VTermRect dest, VTermRect src, void* user)
	{
		(void)dest;
		(void)src;
		static_cast<TerminalVt*>(user)->m_fullRepaint = true;
		return 1;
	}

	// 光标移动：新旧两行都要重画（旧位置擦光标、新位置画光标）
	static int onMoveCursor(VTermPos pos, VTermPos oldpos, int visible, void* user)
	{
		TerminalVt* self = static_cast<TerminalVt*>(user);
		self->m_cursorVisible = visible != 0;
		self->m_cursorRow = pos.row;
		self->m_cursorCol = pos.col;
		self->m_dirtyRows.insert(oldpos.row);
		self->m_dirtyRows.insert(pos.row);
		return 1;
	}

	// OSC 0/2 的窗口标题、光标可见性
	static int onSetProp(VTermProp prop, VTermValue* value, void* user)
	{
		TerminalVt* self = static_cast<TerminalVt*>(user);
		if (!value)
			return 1;

		if (prop == VTERM_PROP_TITLE)
			self->m_title.assign(value->string.str ? value->string.str : "", value->string.len);
		else if (prop == VTERM_PROP_CURSORVISIBLE)
			self->m_cursorVisible = value->boolean != 0;
		return 1;
	}

	// 一行被顶出屏幕 → 进回滚历史
	static int onPushLine(int cols, const VTermScreenCell* cells, bool continuation, void* user)
	{
		TerminalVt* self = static_cast<TerminalVt*>(user);
		TerminalVt::ScrollLine line;
		line.continuation = continuation;
		line.cells.resize(static_cast<size_t>(cols));
		for (int col = 0; col < cols; ++col)
			toTermCell(cells[col], self->m_screen, line.cells[static_cast<size_t>(col)]);
		self->m_scrollback.push_back(std::move(line));
		while (static_cast<int>(self->m_scrollback.size()) > self->m_scrollbackLimit)
			self->m_scrollback.pop_front();

		// 滚在历史里时把偏移一起 +1：视口锚在内容上，不会因为新输出而往下溜
		if (self->m_viewOffset > 0)
			++self->m_viewOffset;

		// 回滚一动，视口到屏幕的映射整体位移 ⇒ 脏行记录作废
		self->m_fullRepaint = true;
		return 1;
	}

	// 屏幕想把一行要回去（上滚回退，如顶部的 \e[M；或**放大尺寸时回填顶部**，见 screen.c 的
	// resize_buffer 结尾那段 "backfill rows by popping scrollback"）
	static int onPopLine(int cols, VTermScreenCell* cells, void* user)
	{
		TerminalVt* self = static_cast<TerminalVt*>(user);
		if (self->m_scrollback.empty())
			return 0;

		const TerminalVt::ScrollLine line = std::move(self->m_scrollback.back());
		self->m_scrollback.pop_back();
		const int count = cols < static_cast<int>(line.cells.size())
			? cols : static_cast<int>(line.cells.size());
		for (int col = 0; col < count; ++col)
			fromTermCell(line.cells[static_cast<size_t>(col)], cells[col]);

		// ⚠️ 尾巴必须补满到 cols —— 这里是"放大尺寸后顶部吞字"的真正现场：
		//   ① libvterm 拿这行是**按 old_cols 整行**搬进屏幕顶部的（resize_buffer 的回填循环读
		//      `sb_buffer[pos.col]`，不看我们写了多少格），没写的格子就原样搬进屏幕，被当成正文画出来；
		//   ② 没写的那段是 sb_buffer 里上一次塞进去的东西 —— 换列时 libvterm 会重新 malloc 这个缓冲
		//      （screen.c 的 resize 里 new_cols != old_cols 两条分支），内容是未初始化内存或回收来的旧行；
		//   ③ 更糟的是它按 `pos.col += sb_buffer[pos.col].width` 步进，碰上 width==0 就原地不动，
		//      **整个 vterm_set_size 不再返回**（实测：60 列下打字 → 放大到 100 列 → 行数 20 改成 40
		//      直接挂住；这正是脱离/回附时"变宽 + 变高"那一步）。
		//    所以只要回滚行比 cols 窄，剩下的一律铺空白格。
		for (int col = count; col < cols; ++col)
			fromTermCell(blankCell(), cells[col]);

		self->m_fullRepaint = true;
		return 1;
	}

	// \e[3J：清回滚
	static int onClearScrollback(void* user)
	{
		TerminalVt* self = static_cast<TerminalVt*>(user);
		self->m_scrollback.clear();
		self->m_viewOffset = 0;
		self->m_fullRepaint = true;
		return 1;
	}

	// 键盘编好的序列（vterm_output_set_callback 攒进来）
	static void onKeyOutput(const char* bytes, size_t len, void* user)
	{
		if (!bytes || len == 0)
			return;
		static_cast<TerminalVt*>(user)->m_keyOutput.emplace_back(bytes, len);
	}
};

namespace
{
// ⚠️ libvterm 的 vterm_screen_set_callbacks 只**存指针**（screen->callbacks = callbacks），
// 所以这张表必须是静态存储期 —— 传个栈上的局部结构体会立刻悬垂。
const VTermScreenCallbacks* screenCallbacks()
{
	static const VTermScreenCallbacks table = [] {
		VTermScreenCallbacks value = {};
		value.damage = &TerminalVtCallbacks::onDamage;
		value.moverect = &TerminalVtCallbacks::onMoveRect;
		value.movecursor = &TerminalVtCallbacks::onMoveCursor;
		value.settermprop = &TerminalVtCallbacks::onSetProp;
		value.sb_pushline4 = &TerminalVtCallbacks::onPushLine;
		value.sb_popline = &TerminalVtCallbacks::onPopLine;
		value.sb_clear = &TerminalVtCallbacks::onClearScrollback;
		return value;
	}();
	return &table;
}
}

TerminalVt::TerminalVt(int rows, int cols, int scrollbackLines)
	: m_rows(rows > 0 ? rows : 24)
	, m_cols(cols > 0 ? cols : 80)
	, m_scrollbackLimit(scrollbackLines > 0 ? scrollbackLines : 1000)
{
	m_vt = vterm_new(m_rows, m_cols);
	vterm_set_utf8(m_vt, 1);
	vterm_output_set_callback(m_vt, &TerminalVtCallbacks::onKeyOutput, this);

	m_screen = vterm_obtain_screen(m_vt);
	m_state = vterm_obtain_state(m_vt);

	// 改尺寸时按内容重排折行（而不是硬裁），面板拉伸时画面才不错乱
	vterm_screen_enable_reflow(m_screen, true);
	vterm_screen_enable_altscreen(m_screen, 1);
	// damage 按行合并：够定位到行，又不像按格那样碎
	vterm_screen_set_damage_merge(m_screen, VTERM_DAMAGE_ROW);

	vterm_screen_set_callbacks(m_screen, screenCallbacks(), this);
	// 必须在 set_callbacks 之后调：它看的是刚装上的 sb_pushline4；不调就退化成 3 参数的 sb_pushline
	vterm_screen_callbacks_has_pushline4(m_screen);

	vterm_screen_reset(m_screen, 1);

	VTermPos cursor = {};
	vterm_state_get_cursorpos(m_state, &cursor);
	m_cursorRow = cursor.row;
	m_cursorCol = cursor.col;

	m_rowBuffer.assign(static_cast<size_t>(m_cols), TermCell());
}

TerminalVt::~TerminalVt()
{
	if (m_vt)
		vterm_free(m_vt);
}

void TerminalVt::feed(const char* bytes, int len)
{
	if (!m_vt || !bytes || len <= 0)
		return;

	vterm_input_write(m_vt, bytes, static_cast<size_t>(len));
	vterm_screen_flush_damage(m_screen);

	// 光标位置以回调为准，这里再取一次兜住"引擎没送过任何光标移动"的初始态
	VTermPos cursor = {};
	vterm_state_get_cursorpos(m_state, &cursor);
	m_cursorRow = cursor.row;
	m_cursorCol = cursor.col;
}

void TerminalVt::reset()
{
	m_scrollback.clear();
	m_dirtyRows.clear();
	m_fullRepaint = true;
	m_viewOffset = 0;
	m_title.clear();
	m_keyOutput.clear();
	m_cursorVisible = true;
	m_cursorRow = 0;
	m_cursorCol = 0;

	vterm_screen_reset(m_screen, 1);
	// 硬复位会把调色板冲回默认，覆盖过的要重放一次
	if (m_paletteSet)
		applyAnsiPalette();
}

void TerminalVt::setSize(int rows, int cols)
{
	if (rows < 1 || cols < 1 || (rows == m_rows && cols == m_cols))
		return;

	// 改尺寸会往回滚里塞行/从回滚里拿行（见 screen.c 的 resize_buffer），视口的"绝对行号"体系整体位移。
	// 这里先把视口顶锚在哪一行记下来：滚在历史里的用户，改尺寸前后看的是同一段内容，不会被拽回底部。
	// （原来无条件 m_viewOffset = 0 —— 脱离/回附时正好会把"正在看的几行"从屏幕上抽走，观感就是顶部换了一批字。）
	const bool atBottom = (m_viewOffset == 0);
	const int anchorLine = static_cast<int>(m_scrollback.size()) - m_viewOffset;

	m_rows = rows;
	m_cols = cols;
	vterm_set_size(m_vt, rows, cols);

	if (atBottom)
	{
		m_viewOffset = 0;
	}
	else
	{
		const int maximum = static_cast<int>(m_scrollback.size());
		const int offset = maximum - anchorLine;
		m_viewOffset = offset < 0 ? 0 : (offset > maximum ? maximum : offset);
	}

	m_dirtyRows.clear();
	m_fullRepaint = true;
	m_rowBuffer.assign(static_cast<size_t>(m_cols), TermCell());
}

void TerminalVt::keyUnichar(uint32_t codepoint, int modifiers)
{
	if (!m_vt)
		return;
	vterm_keyboard_unichar(m_vt, codepoint, toVTermModifier(modifiers));
}

void TerminalVt::keySpecial(SpecialKey key, int modifiers)
{
	if (!m_vt)
		return;
	vterm_keyboard_key(m_vt, toVTermKey(key), toVTermModifier(modifiers));
}

void TerminalVt::keyFunction(int index, int modifiers)
{
	if (!m_vt || index < 1)
		return;
	vterm_keyboard_key(m_vt, static_cast<VTermKey>(VTERM_KEY_FUNCTION_0 + index), toVTermModifier(modifiers));
}

std::vector<std::string> TerminalVt::takeKeyOutput()
{
	std::vector<std::string> out;
	out.swap(m_keyOutput);
	return out;
}

void TerminalVt::setViewOffset(int offset)
{
	const int maximum = static_cast<int>(m_scrollback.size());
	const int clamped = offset < 0 ? 0 : (offset > maximum ? maximum : offset);
	if (clamped == m_viewOffset)
		return;

	m_viewOffset = clamped;
	m_fullRepaint = true;
}

int TerminalVt::absoluteLine(int viewRow) const
{
	if (viewRow < 0 || viewRow >= m_rows)
		return -1;
	// 视口是 [回滚 + 屏幕] 这个拼接条上一个高 rows 的窗口；viewOffset 把它从底部往上挪
	return static_cast<int>(m_scrollback.size()) - m_viewOffset + viewRow;
}

void TerminalVt::readAbsoluteLine(int line) const
{
	m_rowBuffer.assign(static_cast<size_t>(m_cols), TermCell());

	const int scrollbackCount = static_cast<int>(m_scrollback.size());
	if (line < scrollbackCount)
	{
		const ScrollLine& stored = m_scrollback[static_cast<size_t>(line)];
		const int count = m_cols < static_cast<int>(stored.cells.size())
			? m_cols : static_cast<int>(stored.cells.size());
		for (int col = 0; col < count; ++col)
			m_rowBuffer[static_cast<size_t>(col)] = stored.cells[static_cast<size_t>(col)];
		return;
	}

	const int screenRow = line - scrollbackCount;
	if (screenRow < 0 || screenRow >= m_rows)
		return;   // 视口探到屏幕以下（不该发生，兜底给空行）

	VTermScreenCell cell = {};
	VTermPos pos = {};
	pos.row = screenRow;
	for (int col = 0; col < m_cols; ++col)
	{
		pos.col = col;
		std::memset(&cell, 0, sizeof(cell));
		if (vterm_screen_get_cell(m_screen, pos, &cell))
			toTermCell(cell, m_screen, m_rowBuffer[static_cast<size_t>(col)]);
	}
}

const TermCell* TerminalVt::viewRow(int viewRow) const
{
	const int line = absoluteLine(viewRow);
	if (line < 0)
		m_rowBuffer.assign(static_cast<size_t>(m_cols), TermCell());
	else
		readAbsoluteLine(line);
	return m_rowBuffer.data();
}

std::vector<int> TerminalVt::takeDirtyRows()
{
	std::vector<int> rows;

	// 视口滚在历史里时，屏幕坐标的脏行与视口行不是一套编号 —— 整屏重画，别去映射
	const bool all = m_fullRepaint || m_viewOffset != 0;
	if (all)
	{
		rows.reserve(static_cast<size_t>(m_rows));
		for (int row = 0; row < m_rows; ++row)
			rows.push_back(row);
	}
	else
	{
		for (int row : m_dirtyRows)
			if (row >= 0 && row < m_rows)
				rows.push_back(row);
	}

	m_dirtyRows.clear();
	m_fullRepaint = false;
	return rows;
}

void TerminalVt::setAnsiPalette(const uint32_t rgb[16])
{
	if (!rgb)
		return;
	for (int index = 0; index < 16; ++index)
		m_ansiPalette[index] = rgb[index];
	m_paletteSet = true;
	applyAnsiPalette();
}

void TerminalVt::applyAnsiPalette()
{
	for (int index = 0; index < 16; ++index)
	{
		const uint32_t rgb = m_ansiPalette[index];
		VTermColor color = {};
		vterm_color_rgb(&color, static_cast<uint8_t>((rgb >> 16) & 0xff),
			static_cast<uint8_t>((rgb >> 8) & 0xff), static_cast<uint8_t>(rgb & 0xff));
		vterm_state_set_palette_color(m_state, index, &color);
	}
}
