// 见 TerminalVtModel.h：VT 状态与 QML 渲染之间的桥（按行暴露网格 + 脏行通知 + 键盘编码）。
//
// ⚠️ 本文件里没有一处绘制、也没有一处 VT 解析：前者在 QML，后者在 common/terminal/TerminalVt。

#include "qmlbridge/TerminalVtModel.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QtGlobal>

#include <cstring>
#include <vector>

namespace
{
	// 段里的 width 值（渲染遇到它必须跳过这一格 —— 宽字符首格已占两格）
	constexpr int kWideContinuation = 0;

	// "RRGGBB" / "#RRGGBB" → 0xRRGGBB；认不出来给 -1（该索引回落到库内置调色板）
	int parseRgb(const QString& text)
	{
		QString value = text.trimmed();
		if (value.startsWith(QLatin1Char('#')))
			value.remove(0, 1);
		if (value.size() != 6)
			return -1;
		bool ok = false;
		const uint rgb = value.toUInt(&ok, 16);
		return ok ? static_cast<int>(rgb) : -1;
	}

	// 一格 → 段用的文本。空串 = 宽字符续格（width==0）或真空格。
	QString cellText(const TermCell& cell)
	{
		if (cell.width == 0)
			return QString();   // 续格：渲染跳过
		QString text;
		for (int i = 0; i < 6 && cell.chars[i]; ++i)
			text.append(QString::fromUcs4(reinterpret_cast<const char32_t*>(&cell.chars[i]), 1));
		return text;
	}

	// Qt::Key_* → TerminalVt::SpecialKey 的名字。返回空 = 不是这批里的。
	QString specialNameForQtKey(int qtKey)
	{
		switch (qtKey)
		{
		case Qt::Key_Return:
		case Qt::Key_Enter:      return QStringLiteral("enter");
		case Qt::Key_Tab:        return QStringLiteral("tab");
		case Qt::Key_Backtab:    return QStringLiteral("tab");   // Shift+Tab：由修饰键带出序列
		case Qt::Key_Backspace:  return QStringLiteral("backspace");
		case Qt::Key_Escape:     return QStringLiteral("escape");
		case Qt::Key_Up:         return QStringLiteral("up");
		case Qt::Key_Down:       return QStringLiteral("down");
		case Qt::Key_Left:       return QStringLiteral("left");
		case Qt::Key_Right:      return QStringLiteral("right");
		case Qt::Key_Insert:     return QStringLiteral("insert");
		case Qt::Key_Delete:     return QStringLiteral("delete");
		case Qt::Key_Home:       return QStringLiteral("home");
		case Qt::Key_End:        return QStringLiteral("end");
		case Qt::Key_PageUp:     return QStringLiteral("pageup");
		case Qt::Key_PageDown:   return QStringLiteral("pagedown");
		default:                 return QString();
		}
	}

	// 名字 → TerminalVt::SpecialKey。映射只在本文件里有一份（别让 QML 记枚举数字）。
	bool specialKeyFromName(const QString& name, TerminalVt::SpecialKey* out)
	{
		if (!out)
			return false;
		if (name == QLatin1String("enter"))          *out = TerminalVt::KeyEnter;
		else if (name == QLatin1String("tab"))       *out = TerminalVt::KeyTab;
		else if (name == QLatin1String("backspace")) *out = TerminalVt::KeyBackspace;
		else if (name == QLatin1String("escape"))    *out = TerminalVt::KeyEscape;
		else if (name == QLatin1String("up"))        *out = TerminalVt::KeyUp;
		else if (name == QLatin1String("down"))      *out = TerminalVt::KeyDown;
		else if (name == QLatin1String("left"))      *out = TerminalVt::KeyLeft;
		else if (name == QLatin1String("right"))     *out = TerminalVt::KeyRight;
		else if (name == QLatin1String("insert"))    *out = TerminalVt::KeyInsert;
		else if (name == QLatin1String("delete"))    *out = TerminalVt::KeyDelete;
		else if (name == QLatin1String("home"))      *out = TerminalVt::KeyHome;
		else if (name == QLatin1String("end"))       *out = TerminalVt::KeyEnd;
		else if (name == QLatin1String("pageup"))    *out = TerminalVt::KeyPageUp;
		else if (name == QLatin1String("pagedown"))  *out = TerminalVt::KeyPageDown;
		else return false;
		return true;
	}

	// Ctrl+可打印键 → C0 控制码。与 xterm 一致：先按**大写**归一化再 &0x1F；Ctrl+Shift 不归一化
	// （xterm 把 Ctrl+Shift+字母留给快捷键），所以调用方要求没按 Shift。
	bool controlByteFor(uint32_t codepoint, uint32_t* out)
	{
		if (!out || codepoint > 0x7f)
			return false;
		if (codepoint == ' ')
		{
			*out = 0x00;   // Ctrl+空格 = NUL
			return true;
		}
		if (codepoint < '@' || codepoint > 'z')
			return false;
		const uint32_t upper = (codepoint >= 'a' && codepoint <= 'z') ? codepoint - 32 : codepoint;
		if (upper < 'A' && upper != '[' && upper != '\\' && upper != ']' && upper != '^' && upper != '_')
			return false;
		*out = upper & 0x1Fu;
		return true;
	}
}

TerminalVtModel::TerminalVtModel(QObject* parent)
	: QObject(parent)
	, m_vt(24, 80)
{
}

// ------------------------------------------------------------------
// 会话侧驱动
// ------------------------------------------------------------------

void TerminalVtModel::feed(const QByteArray& bytes)
{
	if (bytes.isEmpty())
		return;
	m_vt.feed(bytes.constData(), bytes.size());
	afterFeed();
}

// 快照 = xterm 的序列化重放串：它自己以 \e[?25l\e[H\e[2J 起手，语义上就是整屏重来。
// 这里再硬复位一次，为的是把"上一代的回滚历史/标题/光标"也清干净 —— 状态不跨快照。
void TerminalVtModel::feedSnapshot(const QByteArray& bytes)
{
	m_vt.reset();
	if (m_paletteCount > 0)
		m_vt.setAnsiPalette(m_paletteRgb);
	m_vt.feed(bytes.constData(), bytes.size());
	afterFeed();
}

void TerminalVtModel::afterFeed()
{
	const QString title = QString::fromStdString(m_vt.title());
	if (m_title != title) {
		m_title = title;
		emit titleChanged();
	}
	emitDirty();
	emit cursorChanged();
}

void TerminalVtModel::emitDirty()
{
	const std::vector<int> dirty = m_vt.takeDirtyRows();
	if (dirty.empty())
		return;

	QVariantList rows;
	rows.reserve(static_cast<int>(dirty.size()));
	for (int row : dirty)
		rows.append(row);
	emit dirtyRowsChanged(rows);

	// 滚屏/换行会顶掉回滚行数（视图靠它钳住滚动范围），真变了才通知
	if (m_lastScrollback != m_vt.scrollbackLines()) {
		m_lastScrollback = m_vt.scrollbackLines();
		emit scrollbackChanged();
	}
}

void TerminalVtModel::setSessionState(const QString& state, const QString& statusText,
	const QString& title, bool readOnly)
{
	if (m_state != state) {
		m_state = state;
		emit stateChanged();
	}
	if (m_statusText != statusText) {
		m_statusText = statusText;
		emit statusTextChanged();
	}
	if (m_readOnly != readOnly) {
		m_readOnly = readOnly;
		emit readOnlyChanged();
	}
	// 标题以 VT 里 OSC 0/2 设的为准；VT 还没设过时用引擎报的兜底
	if (m_title.isEmpty() && !title.isEmpty()) {
		m_title = title;
		emit titleChanged();
	}
}

// 网格尺寸：**下限 2×4** —— libvterm 的 screen_resize 在退化尺寸（实测 2×1）上会 abort
// （"screen_resize failed to update cursor position"），所以这里把下限也钉住，不依赖调用方自觉。
void TerminalVtModel::setGridSize(int rows, int cols)
{
	const int safeRows = qBound(2, rows, 500);
	const int safeCols = qBound(4, cols, 1000);
	if (rows < 1 || cols < 1 || (safeRows == m_vt.rows() && safeCols == m_vt.cols()))
		return;
	m_vt.setSize(safeRows, safeCols);
	emit sizeChanged();
	emit scrollbackChanged();
	emit viewOffsetChanged();
	emit cursorChanged();
	emitDirty();
}

// ------------------------------------------------------------------
// 网格读取：按行给"同样式合并后的段"
// ------------------------------------------------------------------

QVariantList TerminalVtModel::rows(int first, int last) const
{
	// 结构化的同一份数据：**复用 rowsJson 的合并逻辑**（段合并只此一份，别复制），
	// 只是把序列化成文本换成在 C++ 里直接给出 QVariantList。
	// 好处：QML 不再 JSON.parse（分层上那属于越界逻辑），且省掉每帧一次文本编解码。
	const QJsonDocument doc = QJsonDocument::fromJson(rowsJson(first, last).toUtf8());
	return doc.array().toVariantList();
}

QString TerminalVtModel::rowsJson(int first, int last) const
{
	const int rows = m_vt.rows();
	const int cols = m_vt.cols();
	const int from = qBound(0, first, rows);
	const int to = qBound(from, last, rows - 1);

	QJsonArray out;
	for (int row = from; row <= to; ++row)
	{
		const TermCell* cells = m_vt.viewRow(row);

		QJsonArray segments;
		QString runText;
		uint32_t runFg = 0;
		uint32_t runBg = 0;
		uint16_t runFlags = 0;
		bool runOpen = false;

		const auto flush = [&segments, &runText, &runFg, &runBg, &runFlags, &runOpen]() {
			if (!runOpen)
				return;
			QJsonArray segment;
			segment.append(runText);
			segment.append(static_cast<int>(runFg));
			segment.append(static_cast<int>(runBg));
			segment.append(static_cast<int>(runFlags));
			segments.append(segment);
			runText.clear();
			runOpen = false;
		};

		for (int col = 0; col < cols; ++col)
		{
			const TermCell& cell = cells[col];

			if (cell.width == 0)
			{
				// 宽字符续格：自成一段（文本空串 + 专有标志），渲染据此跳过这一格
				flush();
				QJsonArray segment;
				segment.append(QString());
				segment.append(0);
				segment.append(0);
				segment.append(TermStyle::FgDefault | TermStyle::BgDefault | TermStyle::Conceal);
				segments.append(segment);
				continue;
			}

			if (runOpen && (cell.style.fg != runFg || cell.style.bg != runBg || cell.style.flags != runFlags))
				flush();

			if (!runOpen) {
				runOpen = true;
				runFg = cell.style.fg;
				runBg = cell.style.bg;
				runFlags = cell.style.flags;
			}
			runText += cellText(cell);
		}

		flush();
		out.append(segments);
	}

	return QString::fromUtf8(QJsonDocument(out).toJson(QJsonDocument::Compact));
}

// ------------------------------------------------------------------
// 键盘
// ------------------------------------------------------------------

void TerminalVtModel::typeText(const QString& text)
{
	if (text.isEmpty())
		return;

	// QKeyEvent::text 可能带**多个**码点（组合字符等）：逐码点走键盘表，别整串塞进去
	const QList<uint> codepoints = text.toUcs4();
	for (uint codepoint : codepoints)
		m_vt.keyUnichar(codepoint, TerminalVt::ModNone);

	collectKeyOutput();
}

void TerminalVtModel::sendKey(const QString& name, int modifiers)
{
	TerminalVt::SpecialKey key = TerminalVt::KeyEnter;
	if (!specialKeyFromName(name, &key))
		return;
	m_vt.keySpecial(key, modifiers);
	collectKeyOutput();
}

void TerminalVtModel::sendFunction(int index, int modifiers)
{
	m_vt.keyFunction(index, modifiers);
	collectKeyOutput();
}

void TerminalVtModel::sendQtKey(int qtKey, int modifiers)
{
	// ① 特殊键先走库的键盘表（方向键在 DECCKM 下有不同序列，只有库知道）
	const QString name = specialNameForQtKey(qtKey);
	if (!name.isEmpty())
	{
		sendKey(name, modifiers);
		return;
	}

	if (qtKey >= Qt::Key_F1 && qtKey <= Qt::Key_F24)
	{
		sendFunction(qtKey - Qt::Key_F1 + 1, modifiers);
		return;
	}

	// ② 可打印键：有 Ctrl 就按控制码规则编；否则当普通 ASCII 字符。
	// **只认 ASCII**：非 ASCII（中文/IME）必须走 typeText(QKeyEvent::text) —— Qt 给中文键的
	// QKeyEvent::key 是 Key_unknown（0x01ffffff），当成码点发过去就是一个乱码字符。
	const bool ctrl = (modifiers & TerminalVt::ModCtrl) != 0;
	const bool shift = (modifiers & TerminalVt::ModShift) != 0;

	if (ctrl && !shift && qtKey >= 0x20 && qtKey <= 0x7e)
	{
		uint32_t control = 0;
		if (controlByteFor(static_cast<uint32_t>(qtKey), &control))
		{
			m_pendingInput.append(static_cast<char>(control));
			emit inputReady();
			return;
		}
	}

	if (qtKey >= 0x20 && qtKey <= 0x7e)
	{
		m_vt.keyUnichar(static_cast<uint32_t>(qtKey), modifiers);
		collectKeyOutput();
	}
}

void TerminalVtModel::collectKeyOutput()
{
	const std::vector<std::string> output = m_vt.takeKeyOutput();
	if (output.empty())
		return;
	for (const std::string& chunk : output)
		m_pendingInput.append(chunk.data(), static_cast<int>(chunk.size()));
	emit inputReady();
}

QByteArray TerminalVtModel::takePendingInput()
{
	QByteArray out;
	out.swap(m_pendingInput);
	return out;
}

// ------------------------------------------------------------------
// 视口 / 尺寸 / 主题
// ------------------------------------------------------------------

void TerminalVtModel::scrollBy(int lines)
{
	if (lines == 0)
		return;
	const int offset = m_vt.viewOffset() + lines;
	const int clamped = qBound(0, offset, m_vt.scrollbackLines());
	if (clamped == m_vt.viewOffset())
		return;
	m_vt.setViewOffset(clamped);
	emit viewOffsetChanged();
	emitDirty();
	emit cursorChanged();
}

void TerminalVtModel::scrollToBottom()
{
	if (m_vt.viewOffset() == 0)
		return;
	m_vt.setViewOffset(0);
	emit viewOffsetChanged();
	emitDirty();
	emit cursorChanged();
}

void TerminalVtModel::reset()
{
	m_vt.reset();
	if (m_paletteCount > 0)
		m_vt.setAnsiPalette(m_paletteRgb);
	if (!m_title.isEmpty()) {
		m_title.clear();
		emit titleChanged();
	}
	m_lastScrollback = 0;
	emit sizeChanged();
	emit scrollbackChanged();
	emit viewOffsetChanged();
	emit cursorChanged();
	emitDirty();
}

void TerminalVtModel::resizeGrid(int rows, int cols)
{
	setGridSize(rows, cols);
}

void TerminalVtModel::setAnsiPalette(const QStringList& colors)
{
	if (colors.isEmpty())
		return;

	uint32_t rgb[16] = {};
	for (int index = 0; index < 16; ++index)
	{
		const int parsed = index < colors.size() ? parseRgb(colors.at(index)) : -1;
		rgb[index] = parsed < 0 ? 0 : static_cast<uint32_t>(parsed);
	}
	std::memcpy(m_paletteRgb, rgb, sizeof(rgb));
	m_paletteCount = 16;
	m_vt.setAnsiPalette(m_paletteRgb);
}
