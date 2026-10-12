#pragma once

// 会话消息控制器（M4a）：把 DSH 的事件流翻译成 MessageListModel 的行。
//
// 事件→内容的规则**照搬旧壳**（MessageQuery::appendHistoryEventsToQuery 与 applyStreamEvent），
// 解析辅助函数直接复用既有的 network/DshEventParser（extractReply / extractThinking /
// extractToolCall / extractToolResult / extractEventText / extractChunkType），所以语义不会漂。
//
// 与旧壳的差异（有意）：
//   * 不再有"控件树缓存 + 离屏分批建控件"：QML 的 ListView 自己复用 delegate；
//   * 流式改用 50ms 合并缓冲（旧壳也有，这里保留同样的策略，只是从"重建控件"变成"追加段"）。
//
// 还没做（M4b）：向上翻页（session/page + beforeSeq）、预取、锚点不跳、提问/审批面板。

#include "common/session/ModelSelectionService.h"
#include "common/session/MessageListModel.h"

#include <QObject>
#include <QSet>
#include <QString>
#include <QVector>

#include <functional>

class DshApiClient;
class QJsonArray;
class QJsonObject;
class QTimer;

class ChatController : public QObject
{
	Q_OBJECT
	Q_PROPERTY(MessageListModel* messages READ messages CONSTANT)
	Q_PROPERTY(int messageCount READ messageCount NOTIFY messagesChanged)
	Q_PROPERTY(bool streaming READ streaming NOTIFY streamingChanged)
	// ---- 交互面板（M4b）：mux 上的 question/requested 与 approval/requested ----
	// 旧壳把面板塞进消息列表（MessageHost::handleMuxFrame → InteractionHandler）。
	// QML 侧等价物：这两个状态 + 一个排在输入区上方的面板（见 chat/InteractionPanel.qml）。
	Q_PROPERTY(bool questionActive READ questionActive NOTIFY interactionChanged)
	Q_PROPERTY(QVariantList questions READ questions NOTIFY interactionChanged)
	Q_PROPERTY(bool approvalActive READ approvalActive NOTIFY interactionChanged)
	Q_PROPERTY(QString approvalToolName READ approvalToolName NOTIFY interactionChanged)
	Q_PROPERTY(QString approvalReason READ approvalReason NOTIFY interactionChanged)
	// M4b 翻页：还有更早的历史 / 正在拉
	Q_PROPERTY(bool olderAvailable READ olderAvailable NOTIFY olderChanged)
	Q_PROPERTY(bool loadingOlder READ loadingOlder NOTIFY olderChanged)

public:
	explicit ChatController(DshApiClient* api, QObject* parent = nullptr);

	// 换会话：清空消息区并记住当前会话（其它会话的事件一律丢弃）
	void setSession(const QString& sessionId);
	QString sessionId() const { return m_sessionId; }

	// 开始一轮：重置"本轮有没有可见输出"的标志（旧壳 m_turnHadOutput 同义）
	void beginTurn();
	// 发送一轮提示（session/prompt）。**发送链路的业务**都在这里（2026-10-10 从 qmlbridge/ShellController 迁入）：
	//   · 服务端明确没有可用模型时拦下并说明（原版 MessageHost：chat_no_model_hint）；
	//   · 用户消息立即上屏，随后把 RPC 发出去；
	//   · "会话写句柄还挂在已退出的旧进程上"的自愈：识别 already owned / resume failed →
	//     请宿主重启服务端（`restartServer` 回调）→ 只重试一次，宿主在重连后调 sendPromptAgain()。
	// `restartServer` / `setStatusLine` 由宿主注入：本类不认识 ServerManager，也不认识 QML。
	// ⚠️ 会话 id 是**入参**（不是本类自己记的）：当前会话归宿主（qmlbridge）所有，这里只负责"往哪个会话发"。
	void sendPrompt(const QString& sessionId, const QString& text,
		const std::function<void()>& restartServer,
		const std::function<void(const QString&)>& setStatusLine);
	// 自愈补发（宿主在"服务端重连"之后调）。没攒着待补发的就什么都不做。
	void sendPromptAgain();
	// 发送链路是否攒着一条待补发的（宿主在每次重连时问一下，见 ChatController.cpp 的自愈说明）
	bool hasPendingRetry() const { return m_pendingRetry; }
	// 中止当前回合（session/cancel）
	void cancel();
	// 接线（在构造之后调）：把 DshApiClient 的 follow 快照 / mux 帧 / 会话投影接进本类。
	// 为什么不放构造函数：这两个信号的发送者要在宿主建好之后才存在，而且宿主还要把 mux 帧那条
	// 同时接给 SessionCatalog（同一条信号多个接收者，顺序由宿主决定）——所以由宿主调一次。
	void connectApi();
	MessageListModel* messages() const { return m_messages; }
	int messageCount() const;
	bool streaming() const { return m_streaming; }
	// 最近一次收尾的回合有没有可见输出（false 通常意味着后端没干活：没配模型/凭据被拒）
	bool lastTurnHadOutput() const { return m_lastTurnHadOutput; }

	QString statsLine() const { return m_statsLine; }
	QVariantList modelGroups() const { return m_modelGroups; }
	QVariantList reasoningLevels() const { return m_reasoningLevels; }
	QString currentModelName() const;
	QString currentLevelName() const;
	bool useGenericLevels() const;
	bool hasModels() const;
	bool modelCatalogMissing() const { return m_catalogLoaded && m_directory.groups.isEmpty(); }

	// 服务端模型目录（session/modelCatalog，全局端点）；会话/新增模型后重拉
	void refreshModelCatalog();
	// 选模型（reasoningEffort 清空，交给新模型的默认档位）与选档位
	void selectModel(const QString& provider, const QString& model);
	void selectLevel(const QString& levelId);
	// 用会话记录过的选择覆盖 chip（provider/model 为空 = 服务端没记录，保持目录默认）
	void applySessionModelSelection(const QString& provider, const QString& model, const QString& reasoningEffort);
	// 合并投影（sessionStats / tokenUsage）并刷新统计行
	void applyProjections(int asOfSeq, const QJsonObject& values);

	// ---- 交互面板 ----
	bool questionActive() const { return !m_questionRpcId.isEmpty(); }
	QVariantList questions() const { return m_questions; }
	bool approvalActive() const { return !m_approvalRpcId.isEmpty(); }
	QString approvalToolName() const { return m_approvalToolName; }
	QString approvalReason() const { return m_approvalReason; }

	// QML 调用：提交提问答案（每项 {id, selected:[标签…], custom?}）；与旧壳的 answer 结构一致：
	//   { sessionId, answer: { answers: [ {id, selected, custom?} ] } }
	Q_INVOKABLE void submitAnswers(const QVariantList& answers);
	// QML 调用：审批结果（true = allowed-once，false = rejected）
	Q_INVOKABLE void respondApproval(bool allow);

	// ---- M4b 翻页 ----
	bool olderAvailable() const { return m_hasOlder; }
	bool loadingOlder() const { return m_loadingOlder; }
	// 拉更早的一页（session/page + beforeSeq）：插到内容头部，QML 侧用 olderPrepended 补偿锚点
	Q_INVOKABLE void loadOlder();

public slots:
	// follow 首帧快照（session/follow）：历史 + 游标 + 是否还有更早内容
	void onFollowSnapshot(const QString& sessionId, int cursor, const QJsonArray& records, bool hasMore);
	// mux 帧：payload.type == "session/event" 时取出 event 交给流式处理
	void onMuxFrame(const QJsonObject& frame);

signals:
	void messagesChanged();
	void streamingChanged();
	void turnFinished();
	void statsLineChanged();
	void catalogChanged();
	// 提问/审批面板的出现与收起
	void interactionChanged();
	// 翻页状态变化 / 更早的历史已插入头部（QML 收到后者时补偿 contentY，保证锚点不跳）
	void olderChanged();
	void olderPrepended();
	// 命令注册表变了（会话流里的 `commands/change` 通知）：宿主据此重拉一次命令表。
	// 本类不持有命令表（那是 CommandController 的事），只负责把这个事实翻出来。
	void commandRegistryChanged();

private:
	// question/requested：payload.questions 翻成 QML 好用的列表并记住 rpcId/sessionId
	void handleQuestionFrame(const QJsonObject& frame);
	// approval/requested：记住 rpcId/sessionId/approvalId 与展示用的工具名/原因
	void handleApprovalFrame(const QJsonObject& frame);
	void rebuildFromRecords(const QJsonArray& records);
	void applyHistoryEvent(const QJsonObject& event);
	void applyStreamEvent(const QJsonObject& event);
	// 回合收尾：无可见输出时补兜底提示，然后广播 turnFinished
	void finishTurn();
	// 目录变化后重建 chip 与菜单的数据
	void rebuildCatalog();
	// 当前档位 id（原版 currentLevel 的 id）
	QString currentLevelId() const;

	// 末行是 Agent 就用它（工具事件接在上一气泡后面），否则新建一条
	int targetAgentRow();
	// 流式合并缓冲：攒够 50ms 或遇到收尾/工具结果时一次写进模型
	void bufferChunk(int row, const QString& type, const QString& text, const QString& toolName = QString());
	void flushPending();
	void setStreaming(bool streaming);

	struct Pending
	{
		int row = -1;
		QString type;
		QString text;
		QString toolName;
	};

	DshApiClient* m_api = nullptr;
	MessageListModel* m_messages = nullptr;
	QTimer* m_flushTimer = nullptr;
	// 回合看门狗（单次）：本会话迟迟**一条事件都没有**就补兜底提示。为什么必须有：后端异常时客户端根本
	// 收不到内容 —— `api-session/error` 走 DshApiClient 的 `$events` type=="emit" 分支，那里只记事件名、
	// 不转发负载，所以"等回合收尾"这条路走不通（旧壳有加载遮罩看门狗兜底，QML 壳没有）。
	// 收到本会话任何事件就把表续上，慢模型不会被误判。
	QTimer* m_turnWatchdog = nullptr;
	bool m_turnActive = false;
	// "会话被旧进程占用"自愈：只重试一次（重试期间再失败就只报错，别无限重启服务端）
	bool m_pendingRetry = false;
	QString m_retryText;
	QString m_retrySessionId;  // 待补发消息属哪个会话：发送是"往某会话发"（id 是入参），补发时记住它
	bool m_connected = false;  // connectApi() 只接一次（宿主可能重入）
	// ---- 小灰字统计行（原版 SessionStatsLine 的数据面）----
	// 只认这两块投影（其余 key 丢弃，免得顶高 asOfSeq —— 与旧壳 SessionProjectionState 同规则）
	struct UsageStats
	{
		int turns = 0;
		int steps = 0;
		qint64 llmMs = 0;
		qint64 toolMs = 0;
		qint64 ttftMs = 0;
		int ttftSteps = 0;
		qint64 decodeMs = 0;
		qint64 decodeTokens = 0;
		qint64 uncachedInputTokens = 0;
		qint64 cacheReadTokens = 0;
		qint64 cacheWriteTokens = 0;
		qint64 outputTokens = 0;
	};

	int m_projectionSeq = -1;
	UsageStats m_usage;
	QString m_statsLine;
	// ---- 模型 / 思考档位 chip（原版 ModelSelector 的数据面）----
	SessionModelDirectory m_directory;
	bool m_catalogLoaded = false;
	QVariantList m_modelGroups;
	QVariantList m_reasoningLevels;
	QString m_overrideProvider;
	QString m_overrideModel;
	QString m_overrideEffort;
	// 已经记过日志的"被忽略事件类型"（每类只记一次，别把排障通道淹掉）
	QSet<QString> m_loggedEventTypes;
	QVector<Pending> m_pending;
	// ---- 交互面板状态（一次只可能有一个待答的提问/审批：服务端会等回答）----
	QString m_questionRpcId;
	QString m_questionSessionId;
	QVariantList m_questions;  // [{id, header, question, multiSelect, options:[{label,description}]}]
	QString m_approvalRpcId;
	QString m_approvalSessionId;
	QString m_approvalId;
	QString m_approvalToolName;
	QString m_approvalReason;
	QString m_sessionId;
	// ---- 翻页（M4b）：当前模型内容对应的原始记录（follow 快照那批，oldest→newest），旧页拼在它前面 ----
	QJsonArray m_historyRecords;
	int m_cursor = 0;         // session/page 的 throughSeq
	bool m_hasOlder = false;  // 服务端说还有更早的
	bool m_loadingOlder = false;
	bool m_streaming = false;
	// 本轮（自最近一次 beginTurn 起）是否已有可见输出：无输出且回合收尾时补一条兜底提示，
	// 避免用户对着自己的消息发呆（无模型 / 模型空回复 / 事件解析不出）—— 与旧壳同一套判定
	bool m_turnHadOutput = false;
	bool m_lastTurnHadOutput = true;
};
