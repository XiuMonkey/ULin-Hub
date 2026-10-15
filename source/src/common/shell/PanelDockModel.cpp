// 见 PanelDockModel.h：右栏面板坞的块（页签）列表。行为逐条对齐搬家前的 JS 数组。

#include "common/shell/PanelDockModel.h"

#include <QModelIndex>
#include <QVariantMap>

namespace
{
	const char kKindTerminal[] = "terminal";
	const char kKindBrowser[] = "browser";

	// 启动器一行（{ kind, label, hint }）；label 统一由 kindLabel() 给，中文名只有一份。
	QVariantMap launcherRow(const QString& kind, const QString& hint)
	{
		QVariantMap item;
		item.insert(QStringLiteral("kind"), kind);
		item.insert(QStringLiteral("label"), PanelDockModel::kindLabel(kind));
		item.insert(QStringLiteral("hint"), hint);
		return item;
	}
}

PanelDockModel& PanelDockModel::instance()
{
	static PanelDockModel model;
	return model;
}

PanelDockModel::PanelDockModel(QObject* parent)
	: QAbstractListModel(parent)
{
}

QString PanelDockModel::kindLabel(const QString& kind)
{
	if (kind == QLatin1String(kKindBrowser))
		return QStringLiteral("网页浏览");
	if (kind == QLatin1String(kKindTerminal))
		return QStringLiteral("终端");
	return QStringLiteral("文件浏览");   // 与搬家前一致：未知 kind 兜底成"文件浏览"
}

PanelDockModel::ViewSpec PanelDockModel::viewFor(const QString& kind, bool webEngineAvailable)
{
	// WebEngine 是浏览器/终端两个视图的运行期依赖：缺了就降级成说明页。
	// ⚠️ 这里不能"先试后错" —— 未调 QtWebEngineQuick::initialize() 的构建里建 WebEngineView
	//    是直接崩（不是报错），所以必须在建之前就判掉，判定权因此收在 C++。
	const bool needsWebEngine = kind == QLatin1String(kKindBrowser) || kind == QLatin1String(kKindTerminal);
	if (needsWebEngine && !webEngineAvailable) {
		return ViewSpec{ QString(), false,
			QStringLiteral("本构建未包含 Qt WebEngine —— %1面板跑不起来（引擎只给原始字节与序列化的 VT 串，"
						   "没有网格接口）。装好后打开 vcxproj 的 webenginequick 与 main.cpp 的 "
						   "QtWebEngineQuick::initialize()。").arg(kindLabel(kind)) };
	}

	// 逻辑路径，不是 url：QML 侧还要过 Style.url() 让 <exe>/ui/ 的同名文件能遮蔽内置
	if (kind == QLatin1String(kKindBrowser))
		return ViewSpec{ QStringLiteral("basic/browser/WebBrowserPanel.qml"), false, QString() };
	if (kind == QLatin1String(kKindTerminal))
		return ViewSpec{ QStringLiteral("basic/terminal/TerminalPanel.qml"), true, QString() };

	// files 与未知 kind 一样走占位页（文案与搬家前 PanelDock.qml 的 placeholderComp 逐字一致）
	return ViewSpec{ QString(), false,
		QStringLiteral("文件浏览还没接入：需要挂上 api-workspace-files（workspaceFiles/*）做目录树与预览。") };
}

QString PanelDockModel::activeKind() const
{
	if (m_activeIndex < 0 || m_activeIndex >= m_blocks.size())
		return QString();
	return m_blocks.at(m_activeIndex).kind;
}

QString PanelDockModel::activeTitle() const
{
	if (m_activeIndex < 0 || m_activeIndex >= m_blocks.size())
		return QString();
	return m_blocks.at(m_activeIndex).title;
}

QString PanelDockModel::activeBlockId() const
{
	if (m_activeIndex < 0 || m_activeIndex >= m_blocks.size())
		return QString();
	return m_blocks.at(m_activeIndex).id;
}

QString PanelDockModel::activeViewPath() const
{
	return viewFor(activeKind(), m_webEngineAvailable).path;
}

bool PanelDockModel::activeViewBlockScoped() const
{
	return viewFor(activeKind(), m_webEngineAvailable).blockScoped;
}

QString PanelDockModel::activeViewDetail() const
{
	return viewFor(activeKind(), m_webEngineAvailable).detail;
}

void PanelDockModel::setWebEngineAvailable(bool available)
{
	if (m_webEngineAvailable == available)
		return;

	m_webEngineAvailable = available;
	emit activeViewChanged();   // 浏览器/终端会在"真视图 ⇄ 说明页"之间换一个
}

QVariantList PanelDockModel::launcherItems() const
{
	// 与搬家前 PanelDock.qml 里那张表逐字一致（顺序、文案都没动）
	QVariantList items;
	items.append(launcherRow(QStringLiteral("terminal"),
		QStringLiteral("在当前会话的工作目录里跑命令（引擎的 terminal 面）")));
	items.append(launcherRow(QStringLiteral("browser"),
		QStringLiteral("用 Qt WebEngine 在右栏看网页")));
	items.append(launcherRow(QStringLiteral("files"),
		QStringLiteral("浏览工作区文件（待接入 workspaceFiles）")));
	return items;
}

void PanelDockModel::open(const QString& kind)
{
	// 同类计数：标题在**开块这一刻**定下来（关块不重排，见头文件）
	int seq = 0;
	for (const Block& block : m_blocks) {
		if (block.kind == kind)
			++seq;
	}

	const int row = m_blocks.size();
	beginInsertRows(QModelIndex(), row, row);
	m_blocks.append(Block{ kind, kindLabel(kind) + QLatin1Char(' ') + QString::number(seq + 1),
		QStringLiteral("blk-%1").arg(m_nextBlockSeq++) });
	endInsertRows();
	emit countChanged();

	setActiveIndex(row);      // 追加即激活（新索引必然不同于旧值 ⇒ 一定会通知）
	setHomeVisible(false);    // 并且离开主界面
}

void PanelDockModel::close(int index)
{
	if (index < 0 || index >= m_blocks.size())
		return;

	beginRemoveRows(QModelIndex(), index, index);
	m_blocks.removeAt(index);
	endRemoveRows();
	emit countChanged();

	// 活动索引只在越界时收敛（搬家前的 `if (activeIndex >= list.length) activeIndex = list.length - 1`）。
	// 关掉活动块**前面**那块时索引不动 ⇒ 活动块换成"原来的下一块"，这是既有行为。
	if (m_activeIndex >= m_blocks.size()) {
		setActiveIndex(m_blocks.size() - 1);
	} else {
		// 索引没动，但行少了 ⇒ 活动块可能就是另一块（关的是它前面那块）：kind 也跟着换，
		// 所以视图那组值必须一起通知（路由读的是它们，不是 activeKind）。
		emit activeChanged();
		emit activeViewChanged();
	}

	if (m_blocks.isEmpty())
		setHomeVisible(true);   // 关光了自动回主界面
}

void PanelDockModel::activate(int index)
{
	if (index < 0 || index >= m_blocks.size())
		return;

	setActiveIndex(index);    // 点当前块是空操作：值没变 ⇒ 不重放内容动画（与搬家前一致）
	setHomeVisible(false);
}

void PanelDockModel::showHome()
{
	setHomeVisible(true);
}

// ---------------- 脱离 / 回附（独立窗口） ----------------
// 分工：模型只记"哪个块脱离了、窗口显示不显示、窗口多大在哪"，**窗口对象与原生细节在 QML/qmlbridge**。
// 这里刻意不碰任何窗口 API（common/** 不依赖 qmlbridge，见头文件的分层说明）。

int PanelDockModel::rowForId(const QString& id) const
{
	for (int i = 0; i < m_blocks.size(); ++i) {
		if (m_blocks.at(i).id == id)
			return i;
	}
	return -1;
}

QVariantList PanelDockModel::detachedBlocks() const
{
	QVariantList list;
	for (const Block& block : m_blocks) {
		if (!block.detached.active)
			continue;

		// 视图那三样按**这个块自己的 kind** 解析（activeViewPath 那组只认活动块，独立窗口里的块
		// 不一定还是活动块 —— 例如两块都脱离后，活动块停了在坞里的那一块）
		const ViewSpec spec = viewFor(block.kind, m_webEngineAvailable);
		QVariantMap item;
		item.insert(QStringLiteral("blockId"), block.id);
		item.insert(QStringLiteral("kind"), block.kind);
		item.insert(QStringLiteral("title"), block.title);
		item.insert(QStringLiteral("visible"), block.detached.visible);
		item.insert(QStringLiteral("viewPath"), spec.path);
		item.insert(QStringLiteral("viewBlockScoped"), spec.blockScoped);
		item.insert(QStringLiteral("viewDetail"), spec.detail);
		item.insert(QStringLiteral("x"), block.detached.x);
		item.insert(QStringLiteral("y"), block.detached.y);
		item.insert(QStringLiteral("w"), block.detached.w);
		item.insert(QStringLiteral("h"), block.detached.h);
		list.append(item);
	}
	return list;
}

bool PanelDockModel::anyDetachedVisible() const
{
	for (const Block& block : m_blocks) {
		if (block.detached.active && block.detached.visible)
			return true;
	}
	return false;
}

void PanelDockModel::detach(int index)
{
	if (index < 0 || index >= m_blocks.size())
		return;

	Block& block = m_blocks[index];
	if (!block.detached.active) {
		block.detached.active = true;
		// 几何只在**第一次**脱离时留空（0 = 让 QML 在光标处给一份默认几何）；
		// 之后再脱离沿用它上一次的位置与尺寸（"拖回去又拖出来"不该跳回原位）。
	}
	block.detached.visible = true;

	// 脱离后坞里不再画它的内容（PanelDock 的路由会看 detachedBlocks），
	// 活动块那组视图值因此可能换一个 ⇒ 一起通知（与 close() 里同一套理由）
	emit activeViewChanged();
	emit detachedChanged();
}

void PanelDockModel::attach(const QString& blockId)
{
	const int index = rowForId(blockId);
	if (index < 0)
		return;

	Block& block = m_blocks[index];
	if (!block.detached.active)
		return;

	// 状态清掉（窗口对象随 detachedBlocks 少一项而销毁），几何留着（下次脱离回到原地）
	block.detached.active = false;
	block.detached.visible = false;

	// "回到它原来待的地方"：激活该块 + 离开主界面（右栏的打开由 QML 侧的壳层动作负责，
	// 与"顶栏 ◫ 打开右栏"走同一条路 —— 见 Shell.qml 的回附/◫ 状态机注释）
	setActiveIndex(index);
	setHomeVisible(false);

	emit activeViewChanged();
	emit detachedChanged();
}

void PanelDockModel::setDetachedVisible(const QString& blockId, bool visible)
{
	const int index = rowForId(blockId);
	if (index < 0)
		return;

	Block& block = m_blocks[index];
	if (!block.detached.active || block.detached.visible == visible)
		return;

	block.detached.visible = visible;
	emit detachedChanged();
}

int PanelDockModel::closeDetachedWindows()
{
	int closed = 0;
	for (Block& block : m_blocks) {
		if (block.detached.active && block.detached.visible) {
			block.detached.visible = false;
			++closed;
		}
	}
	if (closed > 0)
		emit detachedChanged();
	return closed;
}

void PanelDockModel::setDetachedGeometry(const QString& blockId, int x, int y, int w, int h)
{
	const int index = rowForId(blockId);
	if (index < 0)
		return;

	Block& block = m_blocks[index];
	if (!block.detached.active)
		return;

	Detached& d = block.detached;
	// 同值不发通知：QML 侧窗口的 x/y/width/height 是**声明式绑在这四个值上**的，
	// 多发一次通知就多一次"绑定求值 → 窗口 setGeometry"，拖动时会把窗口位置抖回去（实测过）。
	if (d.x == x && d.y == y && d.w == w && d.h == h)
		return;

	d.x = x;
	d.y = y;
	d.w = w;
	d.h = h;
	emit detachedChanged();
}

void PanelDockModel::setActiveIndex(int index)
{
	if (m_activeIndex == index)
		return;

	m_activeIndex = index;
	emit activeIndexChanged();
	emit activeChanged();
	emit activeViewChanged();
}

void PanelDockModel::setHomeVisible(bool visible)
{
	if (m_homeVisible == visible)
		return;

	m_homeVisible = visible;
	emit homeVisibleChanged();
}

int PanelDockModel::rowCount(const QModelIndex& parent) const
{
	return parent.isValid() ? 0 : m_blocks.size();
}

QVariant PanelDockModel::data(const QModelIndex& index, int role) const
{
	if (!index.isValid() || index.row() < 0 || index.row() >= m_blocks.size())
		return QVariant();

	const Block& block = m_blocks.at(index.row());
	switch (role) {
	case KindRole:
		return block.kind;
	case TitleRole:
		return block.title;
	case BlockIdRole:
		return block.id;
	default:
		return QVariant();
	}
}

QHash<int, QByteArray> PanelDockModel::roleNames() const
{
	QHash<int, QByteArray> names;
	names.insert(KindRole, "kind");
	names.insert(TitleRole, "title");
	names.insert(BlockIdRole, "blockId");
	return names;
}
