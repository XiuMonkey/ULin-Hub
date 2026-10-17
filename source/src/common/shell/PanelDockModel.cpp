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

QString PanelDockModel::activeViewUrl() const
{
	// 只有扩展面板记了地址；用户自己开的块恒为空（面板显示"输入网址"的空态）
	if (m_activeIndex < 0 || m_activeIndex >= m_blocks.size())
		return QString();
	return m_blocks.at(m_activeIndex).url;
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
	appendBlock(kind, QString(), QString(), QString());
}

bool PanelDockModel::openUrl(const QString& owner, const QString& url, const QString& title)
{
	// 空 owner / 空 url 一律拒绝：前者会与"用户自己开的块"混成一批（closeOwned 就会误伤），
	// 后者打开来是一块空面板（用户看不出这是扩展开的东西）。
	if (owner.isEmpty() || url.isEmpty())
		return false;

	appendBlock(QStringLiteral("browser"), url, owner, title);
	return true;
}

int PanelDockModel::closeOwned(const QString& owner)
{
	// 空 owner 是"用户自己开的块"：批量关它们不是本方法该干的事（也不该由扩展触发）。
	if (owner.isEmpty())
		return 0;

	// 从后往前关：close() 只收敛活动索引、不重排块，倒序走不会跳块。
	int closed = 0;
	for (int index = m_blocks.size() - 1; index >= 0; --index) {
		if (m_blocks.at(index).owner != owner)
			continue;
		close(index);
		++closed;
	}
	return closed;
}

void PanelDockModel::appendBlock(const QString& kind, const QString& url, const QString& owner,
	const QString& customTitle)
{
	// 同类计数：标题在**开块这一刻**定下来（关块不重排，见头文件）
	int seq = 0;
	for (const Block& block : m_blocks) {
		if (block.kind == kind)
			++seq;
	}

	// 扩展面板自带标题时用它（同类的编号照算，但不进块名）
	const QString title = customTitle.isEmpty()
		? kindLabel(kind) + QLatin1Char(' ') + QString::number(seq + 1)
		: customTitle;

	const int row = m_blocks.size();
	beginInsertRows(QModelIndex(), row, row);
	m_blocks.append(Block{ kind, title, QStringLiteral("blk-%1").arg(m_nextBlockSeq++), url, owner });
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
