#pragma once

// ToolsFilter —— 工具过滤器（对应 DSH 侧 `ToolsFilterPlugin` 暴露的 `/api/tools-filter`）。
//
// 本类由旧壳的 `common/appearance/TopBarTools.{h,cpp}` 迁移而来（2026-10-10），原来那套名字退役。
// 迁移的同时**吸收了两块原先散在外面的职责**：
//   · 形状计算：插件目录 + 已存配置 → 界面用的目录树（原 ToolsFilter::buildDirectories 等静态函数）；
//   · 委托模型：把目录树摊平成 QML 的 ListView 行（行有两种：目录头 / 工具行）——原在 Widgets 的
//     ToolsFilterDirectoryEntry 里靠控件树表达，QML 侧必须由模型表达。
// 于是本类 = 数据层（HTTP/JSON）+ 视图模型（QAbstractListModel），QML 只画。
//
// 协议事实（别按记忆改）：
//   GET  ?session=<id> → { tools:[{name,description,parameters,directory}], degraded, configFound,
//                          dropGuidance, hideContexts, visibleCount }
//   POST { sessionId, config: { FilterList, DropGuidance } }  整份写回 ToolsFilterConfig.json
// 三条陷阱（都来自旧实现，血泪）：
//   ① 隐藏只能靠 `IsVisible:"False"`；把条目从 `ToolsList` 里删掉**表达不了**隐藏。
//   ② 目录的 `IsExpanded` 是**筛选语义**（"False" = 整组隐藏），不是界面上的展开/收起；
//      界面展开态在本类里是 `expanded`（纯显示，不落盘，默认收起）。
//   ③ `HideContexts` / `DropGuidance` 必须**原样带回**，否则整份替换会冲掉它们。

#include <QAbstractListModel>
#include <QByteArray>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVector>

class QNetworkAccessManager;

class ToolsFilter : public QAbstractListModel
{
	Q_OBJECT
	Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
	Q_PROPERTY(QString status READ status NOTIFY statusChanged)
	Q_PROPERTY(bool loaded READ loaded NOTIFY loadedChanged)
	Q_PROPERTY(int visibleCount READ visibleCount NOTIFY summaryChanged)
	Q_PROPERTY(int totalCount READ totalCount NOTIFY summaryChanged)
	Q_PROPERTY(bool degraded READ degraded NOTIFY loadedChanged)

public:
	enum Role
	{
		KindRole = Qt::UserRole + 1,   // "directory" | "tool"
		DirectoryRole,        // 目录头：目录名；工具行：所属目录名
		DescriptionRole,
		CountRole,            // 目录头：成员数
		HiddenCountRole,      // 目录头：该目录里被隐藏的工具数（"N 个工具 · 已隐藏 M 个"里的 M）
		VisibleRole,          // 工具行：是否可见（勾选态）
		HiddenRole,           // 目录头：配置里整组隐藏（IsExpanded == "False"）
		ExpandedRole,         // 目录头：界面展开态（纯显示）
		ToolNameRole,         // 工具行：工具名
		ParametersRole        // 工具行：参数 JSON Schema 紧凑文本（可能为空）
	};

	explicit ToolsFilter(QObject* parent = nullptr);

	// 基地址：只借 scheme/host/port（带启动令牌的 URL 直接拼会落到 GET / 拿回 HTML）
	void setBaseUrl(const QUrl& url);

	bool busy() const { return m_busy; }
	QString status() const { return m_status; }
	bool loaded() const { return m_loaded; }
	int visibleCount() const;
	int totalCount() const;
	bool degraded() const { return m_degraded; }

	// 打开面板时调：拉该会话的工具目录（没有配置文件时由插件建一份初始版）
	Q_INVOKABLE void refresh(const QString& sessionId);
	// 勾选变化后立即整份写回
	Q_INVOKABLE void save();
	// 界面上的展开/收起（纯显示，不落盘、不写 IsExpanded）
	Q_INVOKABLE void toggleDirectory(const QString& directoryName);
	// 单个工具勾选
	Q_INVOKABLE void setToolVisible(const QString& toolName, bool visible);
	// 目录头右侧那颗胶囊开关：**写配置**（折叠 = 整组对模型隐藏）
	Q_INVOKABLE void setDirectoryHidden(const QString& directoryName, bool hidden);
	// 全部显示 / 全部隐藏
	Q_INVOKABLE void setAllVisible(bool visible);

	int rowCount(const QModelIndex& parent = QModelIndex()) const override;
	QVariant data(const QModelIndex& index, int role) const override;
	QHash<int, QByteArray> roleNames() const override;

signals:
	void busyChanged();
	void statusChanged();
	void loadedChanged();
	void summaryChanged();

private:
	// ---- 数据形状（迁移自 TopBarTools 的 ToolFilterDirectory / ToolFilterEntry）----
	struct Tool
	{
		QString name;
		QString description;
		QString parameters;   // 参数 Schema 的紧凑文本，只给界面看
		bool visible = true;  // false ⇒ 写回时成 { ToolName, IsVisible: "False" }
	};

	struct Directory
	{
		QString name;
		QString description;
		bool groupHidden = false;  // 配置里的 IsExpanded == "False"
		bool expanded = false;     // 界面展开态（旧壳是"默认全折叠"）
		QVector<Tool> tools;
	};

	// 摊平：目录头 + （展开时的）工具行
	void rebuildRows();
	void setStatus(const QString& text);
	void setBusy(bool busy);
	// 状态行文案：摘要 + （degraded 时）"服务端只答出了全局层"后缀；没有可过滤工具时换成空态文案
	void statusSummaryOrEmpty();
	Directory* findDirectory(const QString& name);
	Tool* findTool(const QString& name);
	void syncSummary();

	// 形状计算（原 ToolsFilter 的三个静态函数）
	static QVector<Directory> buildDirectories(const QJsonArray& tools, const QJsonObject& storedConfig);
	static QJsonArray buildToolsList(const QVector<Tool>& tools);
	static QJsonObject buildDocument(const QVector<Directory>& directories, bool dropGuidance,
		const QStringList& hideContexts);
	static QString defaultDirectoryName();

	struct Row
	{
		QString kind;          // "directory" | "tool"
		int directoryIndex = -1;
		int toolIndex = -1;
	};

	QNetworkAccessManager* m_nam = nullptr;
	QUrl m_baseUrl;

	QVector<Directory> m_directories;
	QVector<Row> m_rows;

	QString m_sessionId;      // 当前拉的是哪个会话（保存时发回去）
	bool m_dropGuidance = true;
	QStringList m_hideContexts;   // 原样带回，别丢
	bool m_configFound = false;
	bool m_degraded = false;

	bool m_loaded = false;
	bool m_busy = false;
	QString m_status;
};
