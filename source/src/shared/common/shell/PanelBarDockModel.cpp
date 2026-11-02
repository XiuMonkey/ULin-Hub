// 见 PanelBarDockModel.h：右栏面板坞的块（页签）列表。行为逐条对齐的 JS 数组。

#include "common/shell/PanelBarDockModel.h"

#include <QModelIndex>
#include <QVariantMap>

namespace
{
	const char kKindTerminal[] = "terminal";
	const char kKindBrowser[] = "browser";

	// 插件数据块：一个插件的一份数据面的**原生**视图（扩展把内容 QML 注入到 panel 里的容器）。
	// 与浏览器块的区别：这里没有 WebEngine，数据由扩展 C++ 取、QML 原生画。
	const char kKindPluginData[] = "pluginData";

	QVariantMap launcherRow(const QString& kind, const QString& hint)   // 启动器一行（{ kind, label, hint }）；label 统一由 kindLabel() 给，中文名只有一份
	{
		QVariantMap item;
		item.insert(QStringLiteral("kind"), kind);
		item.insert(QStringLiteral("label"), PanelBarDockModel::kindLabel(kind));
		item.insert(QStringLiteral("hint"), hint);
		return item;
	}
}

PanelBarDockModel& PanelBarDockModel::instance()
{
	static PanelBarDockModel model;
	return model;
}

PanelBarDockModel::PanelBarDockModel(QObject* parent)
	: QAbstractListModel(parent)
{
}

QString PanelBarDockModel::kindLabel(const QString& kind)
{
	if (kind == QLatin1String(kKindBrowser))
		return QStringLiteral("网页浏览");
	if (kind == QLatin1String(kKindTerminal))
		return QStringLiteral("终端");
	if (kind == QLatin1String(kKindPluginData))
		return QStringLiteral("插件数据");
	return QStringLiteral("文件浏览");   // 保持一致：未知 kind 兜底成"文件浏览"
}

PanelBarDockModel::ViewSpec PanelBarDockModel::viewFor(const QString& kind, bool webEngineAvailable)
{
	// WebEngine 是浏览器/终端两个视图的运行期依赖：缺了就降级成说明页。
	// ⚠️ 这里不能"先试后错" —— 未调 QtWebEngineQuick::initialize() 的构建里建 WebEngineView
	// 是直接崩（不是报错），所以必须在建之前就判掉，判定权因此收在 C++。
	const bool needsWebEngine = kind == QLatin1String(kKindBrowser) || kind == QLatin1String(kKindTerminal);
	if (needsWebEngine && !webEngineAvailable) {
		return ViewSpec{ QString(), false,
			QStringLiteral("当前版本不支持%1：缺少网页渲染组件，面板暂时打不开。"
						   "请使用完整安装版本，或联系维护者更新客户端。").arg(kindLabel(kind)) };
	}

	if (kind == QLatin1String(kKindBrowser))   // 逻辑路径，不是 url：QML 侧还要过 Style.url() 让 <exe>/ui/ 的同名文件能遮蔽内置
		return ViewSpec{ QStringLiteral("basic/browser/WebBrowserPanel.qml"), false, QString() };
	if (kind == QLatin1String(kKindTerminal))
		return ViewSpec{ QStringLiteral("basic/terminal/TerminalPanel.qml"), true, QString() };

	// 插件数据块：宿主只提供外壳（标题 + 空态 + 一个名为 pluginData.host 的容器），
	// 内容由扩展 MountInto 进来 —— 因此它不依赖 WebEngine。
	if (kind == QLatin1String(kKindPluginData))
		return ViewSpec{ QStringLiteral("basic/panelui/PluginDataPanel.qml"), false, QString() };

	if (kind == QLatin1String("files"))
		return ViewSpec{ QStringLiteral("basic/files/FileBrowserPanel.qml"), false, QString() };

	return ViewSpec{ QString(), false,
		QStringLiteral("文件浏览还在接入中，暂时不可用。") };
}

QString PanelBarDockModel::activeKind() const
{
	if (m_activeIndex < 0 || m_activeIndex >= m_blocks.size())
		return QString();
	return m_blocks.at(m_activeIndex).kind;
}

QString PanelBarDockModel::activeTitle() const
{
	if (m_activeIndex < 0 || m_activeIndex >= m_blocks.size())
		return QString();
	return m_blocks.at(m_activeIndex).title;
}

QString PanelBarDockModel::activeBlockId() const
{
	if (m_activeIndex < 0 || m_activeIndex >= m_blocks.size())
		return QString();
	return m_blocks.at(m_activeIndex).id;
}

QString PanelBarDockModel::activeViewPath() const
{
	return viewFor(activeKind(), m_webEngineAvailable).path;
}

bool PanelBarDockModel::activeViewBlockScoped() const
{
	return viewFor(activeKind(), m_webEngineAvailable).blockScoped;
}

QString PanelBarDockModel::activeViewDetail() const
{
	return viewFor(activeKind(), m_webEngineAvailable).detail;
}

QString PanelBarDockModel::activeViewUrl() const
{
	if (m_activeIndex < 0 || m_activeIndex >= m_blocks.size())
		return QString();

	const Block& block = m_blocks.at(m_activeIndex);
	if (!block.url.isEmpty())          // openUrl 开出来的块：地址记在块上
		return block.url;

	return QString();                  // 用户自己开的浏览器块：空（面板显示空态）
}

void PanelBarDockModel::setWebEngineAvailable(bool available)
{
	if (m_webEngineAvailable == available)
		return;

	m_webEngineAvailable = available;
	emit activeViewChanged();   // 浏览器/终端会在"真视图 ⇄ 说明页"之间换一个
}

QVariantList PanelBarDockModel::launcherItems() const
{
	QVariantList items;   // 内置三行（终端 / 网页 / 文件），扩展登记的行追加在后面
	items.append(launcherRow(QStringLiteral("terminal"),
		QStringLiteral("在当前会话的工作目录里运行命令")));
	items.append(launcherRow(QStringLiteral("browser"),
		QStringLiteral("在右栏浏览网页")));
	items.append(launcherRow(QStringLiteral("files"),
		QStringLiteral("浏览当前工作区的文件")));
	items.append(m_extraLaunchers);   // 扩展登记的行追加在宿主内置三行之后
	return items;
}

void PanelBarDockModel::registerLauncher(const QString& kind, const QString& label, const QString& hint)
{
	if (kind.isEmpty())
		return;
	for (const QVariant& v : m_extraLaunchers) {
		if (v.toMap().value(QStringLiteral("kind")).toString() == kind)
			return;   // 已登记：幂等
	}
	QVariantMap row;
	row.insert(QStringLiteral("kind"), kind);
	row.insert(QStringLiteral("label"), label.isEmpty() ? kindLabel(kind) : label);
	row.insert(QStringLiteral("hint"), hint);
	m_extraLaunchers.append(row);
	emit launcherItemsChanged();
}

void PanelBarDockModel::unregisterLauncher(const QString& kind)
{
	for (int i = 0; i < m_extraLaunchers.size(); ++i) {
		if (m_extraLaunchers.at(i).toMap().value(QStringLiteral("kind")).toString() == kind) {
			m_extraLaunchers.removeAt(i);
			emit launcherItemsChanged();
			return;
		}
	}
}

void PanelBarDockModel::open(const QString& kind)
{
	appendBlock(kind, QString(), QString());
}

QString PanelBarDockModel::openUrl(const QString& url, const QString& title)
{
	if (url.isEmpty())   // URL 为空 = 打开来是一块空面板，对调用方没有意义 ⇒ 拒绝（返回空串，什么也不开）
		return QString();

	return appendBlock(QStringLiteral("browser"), url, title);
}

bool PanelBarDockModel::closeBlock(const QString& blockId)
{
	for (int index = 0; index < m_blocks.size(); ++index) {
		if (m_blocks.at(index).id != blockId)
			continue;
		close(index);   // 与按索引关同一条收敛逻辑（活动索引越界才收敛、关光了回主界面）
		return true;
	}
	return false;
}

QStringList PanelBarDockModel::blockIds() const
{
	QStringList ids;
	ids.reserve(m_blocks.size());
	for (const Block& block : m_blocks)
		ids.append(block.id);
	return ids;
}

bool PanelBarDockModel::detachBlock(const QString& blockId)
{
	for (int index = 0; index < m_blocks.size(); ++index) {
		if (m_blocks.at(index).id != blockId)
			continue;

		const Block block = m_blocks.at(index);
		beginRemoveRows(QModelIndex(), index, index);
		m_blocks.removeAt(index);
		endRemoveRows();
		m_detachedBlocks.append(block);

		if (m_blocks.isEmpty()) {
			setHomeVisible(true);
		} else if (m_activeIndex >= m_blocks.size()) {
			setActiveIndex(m_blocks.size() - 1);
		} else {
			// 移除点在活动块之前：活动块还是同一块，但索引要跟着左移。
			if (index < m_activeIndex) {
				--m_activeIndex;
				emit activeIndexChanged();
			}
			// 行数/索引变了：activeView 要重算（和 close 的语义一致）。
			emit activeChanged();
			emit activeViewChanged();
		}
		emit countChanged();
		emit detachedBlocksChanged();
		return true;
	}
	return false;
}

bool PanelBarDockModel::attachBlock(const QString& blockId)
{
	for (int i = 0; i < m_detachedBlocks.size(); ++i) {
		if (m_detachedBlocks.at(i).id != blockId)
			continue;

		const Block block = m_detachedBlocks.takeAt(i);
		const int row = m_blocks.size();
		beginInsertRows(QModelIndex(), row, row);
		m_blocks.append(block);
		endInsertRows();
		emit countChanged();
		emit detachedBlocksChanged();
		setActiveIndex(row);
		setHomeVisible(false);
		return true;
	}
	return false;
}

bool PanelBarDockModel::discardDetachedBlock(const QString& blockId)
{
	for (int i = 0; i < m_detachedBlocks.size(); ++i) {
		if (m_detachedBlocks.at(i).id != blockId)
			continue;
		m_detachedBlocks.removeAt(i);
		emit detachedBlocksChanged();
		return true;
	}
	return false;
}

QVariantMap PanelBarDockModel::blockInfo(const QString& blockId) const
{
	const Block* found = nullptr;
	for (const Block& block : m_blocks) {
		if (block.id == blockId) { found = &block; break; }
	}
	if (found == nullptr) {
		for (const Block& block : m_detachedBlocks) {
			if (block.id == blockId) { found = &block; break; }
		}
	}
	if (found == nullptr)
		return {};

	const ViewSpec view = viewFor(found->kind, m_webEngineAvailable);
	QVariantMap info;
	info.insert(QStringLiteral("id"), found->id);
	info.insert(QStringLiteral("kind"), found->kind);
	info.insert(QStringLiteral("title"), found->title);
	info.insert(QStringLiteral("url"), found->url);
	info.insert(QStringLiteral("viewPath"), view.path);
	info.insert(QStringLiteral("viewBlockScoped"), view.blockScoped);
	info.insert(QStringLiteral("viewDetail"), view.detail);
	return info;
}

QStringList PanelBarDockModel::detachedBlocks() const
{
	QStringList ids;
	for (const Block& block : m_detachedBlocks)
		ids.append(block.id);
	return ids;
}

QString PanelBarDockModel::appendBlock(const QString& kind, const QString& url, const QString& customTitle)
{
	int seq = 0;   // 同类计数：标题在**开块这一刻**定下来（关块不重排，见头文件）
	for (const Block& block : m_blocks) {
		if (block.kind == kind)
			++seq;
	}

	const QString title = customTitle.isEmpty()   // 扩展面板自带标题时用它（同类的编号照算，但不进块名）
		? kindLabel(kind) + QLatin1Char(' ') + QString::number(seq + 1)
		: customTitle;

	const int row = m_blocks.size();
	const QString id = QStringLiteral("blk-%1").arg(m_nextBlockSeq++);
	beginInsertRows(QModelIndex(), row, row);
	m_blocks.append(Block{ kind, title, id, url });
	endInsertRows();
	emit countChanged();

	setActiveIndex(row);      // 追加即激活（新索引必然不同于旧值 ⇒ 一定会通知）
	setHomeVisible(false);    // 并且离开主界面

	return id;                // 谁开的谁拿 id，模型不记"是谁开的"
}

void PanelBarDockModel::close(int index)
{
	if (index < 0 || index >= m_blocks.size())
		return;

	beginRemoveRows(QModelIndex(), index, index);
	m_blocks.removeAt(index);
	endRemoveRows();
	emit countChanged();

	if (m_activeIndex >= m_blocks.size()) {   // 活动索引只在越界时收敛（的 `if（activeIndex >= list.length) activeIndex = list.length - 1`）
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

void PanelBarDockModel::moveBlock(int from, int to)
{
	if (from < 0 || from >= m_blocks.size() || to < 0 || to >= m_blocks.size() || from == to)
		return;

	const QString activeId = activeBlockId();
	const int destinationRow = to + (from < to ? 1 : 0);
	beginMoveRows(QModelIndex(), from, from, QModelIndex(), destinationRow);
	const Block block = m_blocks.takeAt(from);
	m_blocks.insert(to, block);
	endMoveRows();

	if (!activeId.isEmpty()) {
		for (int i = 0; i < m_blocks.size(); ++i) {
			if (m_blocks.at(i).id == activeId) {
				setActiveIndex(i);
				break;
			}
		}
	}
	emit activeChanged();
	emit activeViewChanged();
}

void PanelBarDockModel::activate(int index)
{
	if (index < 0 || index >= m_blocks.size())
		return;

	setActiveIndex(index);   // 点当前块是空操作：值没变 ⇒ 不重放内容动画
	setHomeVisible(false);
}

void PanelBarDockModel::showHome()
{
	setHomeVisible(true);
}

void PanelBarDockModel::setActiveIndex(int index)
{
	if (m_activeIndex == index)
		return;

	m_activeIndex = index;
	emit activeIndexChanged();
	emit activeChanged();
	emit activeViewChanged();
}

void PanelBarDockModel::setHomeVisible(bool visible)
{
	if (m_homeVisible == visible)
		return;

	m_homeVisible = visible;
	emit homeVisibleChanged();
}

int PanelBarDockModel::rowCount(const QModelIndex& parent) const
{
	return parent.isValid() ? 0 : m_blocks.size();
}

QVariant PanelBarDockModel::data(const QModelIndex& index, int role) const
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

QHash<int, QByteArray> PanelBarDockModel::roleNames() const
{
	QHash<int, QByteArray> names;
	names.insert(KindRole, "kind");
	names.insert(TitleRole, "title");
	names.insert(BlockIdRole, "blockId");
	return names;
}
