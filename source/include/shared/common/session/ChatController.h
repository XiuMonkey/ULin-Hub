#pragma once

// 会话消息控制器（M4a）：把 DSH 的事件流翻译成 MessageListModel 的行。
// 事件→内容的规则**沿用既有**（MessageQuery::appendHistoryEventsToQuery 与 applyStreamEvent），
// 解析辅助函数直接复用既有的 network/DshEventParser（extractReply / extractThinking /
// extractToolCall / extractToolResult / extractEventText / extractChunkType），所以语义不会漂。
// 与现有实现的差异（有意）：
//  * 不再有"控件树缓存 + 离屏分批建控件"：QML 的 ListView 自己复用 delegate；
// * 流式改用 33ms 合并缓冲。
// 还没做（M4b）：向上翻页（session/page + beforeSeq）、预取、锚点不跳、提问/审批面板。

#include "common/session/ModelSelectionService.h"
#include "common/session/MessageListModel.h"
// ⚠️ 必须 include 而不是前向声明：Q_PROPERTY(TraceModel* ...) 会让 moc 在 staticMetaObject 里
//  实例化 QMetaType 校验，此时类型不完整就触发
//  "Pointer Meta Types must either point to fully-defined types or be declared with
//  Q_DECLARE_OPAQUE_POINTER(T *)"（C2338），连带 C2737 staticMetaObject 未初始化。
//  这类错误报在 moc_*.cpp 里，看着像是 moc 生成的代码坏了，其实源码头文件少了 include。
#include "common/session/TraceModel.h"

#include <QJsonArray>
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
	// 轨迹视图的记录表（对照官方 dsh-client-ui-trajectory）：与本类吃同一条事件流，
	// 只是不做"翻译成气泡"，而是把账本事件原样摊成按轮次分组的记录。
	Q_PROPERTY(TraceModel* trace READ trace CONSTANT)
	// 待发图片：[{ name, mediaType, data(base64), preview(data URL) }]
	Q_PROPERTY(QVariantList pendingAttachments READ pendingAttachments NOTIFY pendingAttachmentsChanged)
	Q_PROPERTY(int messageCount READ messageCount NOTIFY messagesChanged)
	Q_PROPERTY(bool streaming READ streaming NOTIFY streamingChanged)
	// "已请求中止、正在等回合收尾"：QML 拿它把停止键切成"停止中"，用户点下去立刻有反馈。
	// 为什么不复用 streaming：这段期间 streaming **必须仍为 true** —— 回合真的还在跑，按钮一旦变回
	// "发送"，用户就会以为已经停了而继续发消息（那一条会排在中止后端的那一轮后面）。
	Q_PROPERTY(bool cancelling READ cancelling NOTIFY cancellingChanged)
	Q_PROPERTY(bool questionActive READ questionActive NOTIFY interactionChanged)
	Q_PROPERTY(QVariantList questions READ questions NOTIFY interactionChanged)
	// InteractionPanel 的选择状态：默认项、单选/多选切换、自定义答案都在 C++ 侧。
	Q_PROPERTY(QVariantList answerSelections READ answerSelections NOTIFY answerSelectionsChanged)
	Q_PROPERTY(bool approvalActive READ approvalActive NOTIFY interactionChanged)
	Q_PROPERTY(QString approvalToolName READ approvalToolName NOTIFY interactionChanged)
	Q_PROPERTY(QString approvalReason READ approvalReason NOTIFY interactionChanged)
	// M4b 翻页：还有更早的历史 / 正在拉
	Q_PROPERTY(bool olderAvailable READ olderAvailable NOTIFY olderChanged)
	Q_PROPERTY(bool loadingOlder READ loadingOlder NOTIFY olderChanged)
	// 已切到某个会话、但首帧 follow 快照还没到：QML 用它在会话没加载出来时保持空白。
	Q_PROPERTY(bool sessionLoading READ sessionLoading NOTIFY sessionLoadingChanged)
	// 后台任务名册（顶栏"后台任务"入口的数据面）：行来自引擎 job/list 流（DshApiClient 第六条逻辑流），
	// 这里只把 JobView 摊成 QML 好用的列表并保持官方的"活的任务在前、已结束的按结束时间倒序"顺序。
	// 放在本类而不是再开一个类：它与会话同生命周期（换会话即清空），与 modelGroups/statsLine 同一档
	// 会话级实时数据；网络细节仍在 DshApiClient。
	Q_PROPERTY(QVariantList jobs READ jobs NOTIFY jobsChanged)

public:
	explicit ChatController(DshApiClient* api, QObject* parent = nullptr);

	// 换会话：清空消息区并记住当前会话（其它会话的事件一律丢弃）
	void setSession(const QString& sessionId);
	QString sessionId() const { return m_sessionId; }

	// 开始一轮：重置"本轮有没有可见输出"的标志
	void beginTurn();
	// 发送一轮提示（session/prompt）。**发送链路的业务**都在这里：
	//  · 服务端明确没有可用模型时拦下并说明（原版 MessageHost：chat_no_model_hint）；
	//  · 用户消息立即上屏，随后把 RPC 发出去；
	//  · "会话写句柄还挂在已退出的旧进程上"的自愈：识别 already owned / resume failed →
	//  请宿主重启服务端（`restartServer` 回调）→ 只重试一次，宿主在重连后调 sendPromptAgain。
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
	TraceModel* trace() const { return m_trace; }
	int messageCount() const;
	bool streaming() const { return m_streaming; }
	// 是否有一个已发出、尚未被 turn/end 确认的中止请求（见 m_cancelPending 的说明）
	bool cancelling() const { return m_cancelPending; }
	// 最近一次收尾的回合有没有可见输出（false 通常意味着后端没干活：没配模型/凭据被拒）
	bool lastTurnHadOutput() const { return m_lastTurnHadOutput; }

	QVariantList jobs() const { return m_jobs; }
	int jobCount() const { return m_jobs.size(); }
	// 有没有在跑（running / stopping）的任务：顶栏那颗状态点用它
	bool jobRunning() const;

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

	bool questionActive() const { return !m_questionRpcId.isEmpty(); }
	QVariantList questions() const { return m_questions; }
	bool approvalActive() const { return !m_approvalRpcId.isEmpty(); }
	QString approvalToolName() const { return m_approvalToolName; }
	QString approvalReason() const { return m_approvalReason; }

	// QML 调用：提交提问答案（每项 {id, selected:[标签…], custom?}）；与现有实现的 answer 结构一致：
	//  { sessionId, answer: { answers: [ {id, selected, custom?} ] } }
	// ---- 图片附件（协议：session/prompt 的 image 部件；读回用 session/attachment）----
	QVariantList pendingAttachments() const { return m_pendingImages; }
	// 图片走内联 base64；普通文件先落成待发卡片，正文里带本地路径。
	Q_INVOKABLE void addFiles(const QVariantList& urls);
	Q_INVOKABLE void addImageFiles(const QVariantList& urls);   // 兼容旧调用，等价 addFiles
	Q_INVOKABLE void removePendingAttachment(int index);
	Q_INVOKABLE void clearPendingAttachments();
	// attachmentId -> file:// 源（异步取字节并落临时文件；首次返回空）
	Q_INVOKABLE QString attachmentSource(const QString& attachmentId);

	Q_INVOKABLE void submitAnswers(const QVariantList& answers);
	QVariantList answerSelections() const { return m_answerSelections; }
	Q_INVOKABLE void resetAnswerSelections();
	Q_INVOKABLE void toggleAnswerOption(int questionIndex, const QString& label);
	Q_INVOKABLE void setAnswerCustom(int questionIndex, const QString& text);
	Q_INVOKABLE void submitAnswerSelections();
	// QML 调用：审批结果（true = allowed-once，false = rejected）
	Q_INVOKABLE void respondApproval(bool allow);

	bool olderAvailable() const { return m_hasOlder; }
	bool loadingOlder() const { return m_loadingOlder; }
	bool sessionLoading() const { return m_sessionLoading; }
	// 拉更早的一页（session/page + beforeSeq）：插到内容头部，QML 侧用 olderPrepended 补偿锚点
	Q_INVOKABLE void loadOlder();

public slots:
	// follow 首帧快照（session/follow）：历史 + 游标 + 是否还有更早内容
	void onFollowSnapshot(const QString& sessionId, int cursor, const QJsonArray& records, bool hasMore);
	// mux 帧：payload.type == "session/event" 时取出 event 交给流式处理
	void onMuxFrame(const QJsonObject& frame);
	// 引擎 job/list 流的一帧（整份名册，服务端每次给全量）：见 DshApiClient::jobRosterReady
	void onJobRoster(const QJsonArray& jobs);

signals:
	void messagesChanged();
	void pendingAttachmentsChanged();
	void attachmentSourceReady(const QString& attachmentId);
	void streamingChanged();
	// "正在中止"的进出（与 streamingChanged 分开：QML 只重绘按钮态，不必重排消息区）
	void cancellingChanged();
	void turnFinished();
	void statsLineChanged();
	void catalogChanged();
	// 子代理那边"有事发生"（目录新增 / 回传消息 / 结束通知）：宿主据此轻量重拉一次 session.list，
	// 把 running 刷准。运行态**不在** session/control 投影里，只能靠这条重拉。
	void peerActivitySeen();
	// 提问/审批面板的出现与收起
	void interactionChanged();
	void answerSelectionsChanged();
	// 翻页状态变化 / 更早的历史已插入头部（QML 收到后者时补偿 contentY，保证锚点不跳）
	void olderChanged();
	void olderPrepended();
	void sessionLoadingChanged();
	// 命令注册表变了（会话流里的 `commands/change` 通知）：宿主据此重拉一次命令表。
	// 本类不持有命令表（那是 CommandController 的事），只负责把这个事实翻出来。
	void commandRegistryChanged();
	// 后台任务名册变了（整表替换，含清空）
	void jobsChanged();

private:
	// question/requested：payload.questions 翻成 QML 好用的列表并记住 rpcId/sessionId
	void handleQuestionFrame(const QJsonObject& frame);
	// approval/requested：记住 rpcId/sessionId/approvalId 与展示用的工具名/原因
	void handleApprovalFrame(const QJsonObject& frame);
	void rebuildFromRecords(const QJsonArray& records);
	void applyHistoryEvent(const QJsonObject& event);
	static QVector<MessageListModel::ImageRef> extractEventImages(const QJsonObject& event);
	void applyStreamEvent(const QJsonObject& event);
	// 回合收尾：无可见输出时补兜底提示，然后广播 turnFinished
	void finishTurn();
	// 目录变化后重建 chip 与菜单的数据
	void rebuildCatalog();
	// 当前档位 id（原版 currentLevel 的 id）
	QString currentLevelId() const;

	// 本轮（turn）的 Agent 行：**一轮只占一个气泡**（官方 Web UI 的形态：整轮收起成一行、展开才看到
	// 各步骤）。已有就复用，没有就新建。turn/start、本地发送、历史里的 user/message、turn/end 都重置它
	// —— 见 .cpp 里 m_turnRow 的说明。
	// 旧实现是按"最后一行是不是 agent"决定续写/新建（targetAgentRow），一轮有多条 assistant/message 时
	// 只要中间插进别的行就会被拆成多个气泡，已移除。
	int agentRowForTurn();
	// 流式合并缓冲：攒够 33ms 或遇到收尾/工具结果时一次写进模型
	void bufferChunk(int row, const QString& type, const QString& text, const QString& toolName = QString());
	void flushPending();
	void setStreaming(bool streaming);
	// 清掉"正在中止"状态（turn/end 到达、中止看门狗超时、换会话时都走这里）
	void clearCancelPending();

	struct Pending
	{
		int row = -1;
		QString type;
		QString text;
		QString toolName;
	};

	QVariantList m_pendingImages;
	QHash<QString, QString> m_attachmentSources;
	QSet<QString> m_attachmentInFlight;

	DshApiClient* m_api = nullptr;
	MessageListModel* m_messages = nullptr;
	TraceModel* m_trace = nullptr;
	QTimer* m_flushTimer = nullptr;
	QTimer* m_sessionLoadingTimer = nullptr;   // 首帧快照迟迟不到时解除“加载中”，避免永久空白
	// 回合看门狗（单次）：本会话迟迟**一条事件都没有**就补兜底提示。为什么必须有：后端异常时客户端根本
	// 收不到内容 —— `api-session/error` 走 DshApiClient 的 `$events` type=="emit" 分支，那里只记事件名、
	// 不转发负载，所以"等回合收尾"这条路走不通。
	// 收到本会话任何事件就把表续上，慢模型不会被误判。
	QTimer* m_turnWatchdog = nullptr;
	bool m_turnActive = false;
	// 本轮 Agent 行号（-1 = 本轮还没建）。**一轮的所有内容都落到这一行** ⇒ 一个气泡。
	// 为什么不能只靠 lastAgentRow：历史回放里一轮有多条 assistant/message，旧代码每条都 appendAgent，
	// 于是一轮被拆成好几个气泡；而 lastAgentRow 只看"最后一行是不是 agent"，
	// 中间插进系统提示（command/done）就会断掉。回合边界才是正确判据。
	int m_turnRow = -1;
	// "会话被旧进程占用"自愈：只重试一次（重试期间再失败就只报错，别无限重启服务端）
	bool m_pendingRetry = false;
	QString m_retryText;
	QString m_retrySessionId;  // 待补发消息属哪个会话：发送是"往某会话发"（id 是入参），补发时记住它
	QJsonArray m_retryImages;  // 补发时保留图片部件，否则重试只剩文字
	bool m_connected = false;  // connectApi() 只接一次（宿主可能重入）
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
	SessionModelDirectory m_directory;
	bool m_catalogLoaded = false;
	QVariantList m_modelGroups;
	QVariantList m_reasoningLevels;
	QString m_overrideProvider;
	QString m_overrideModel;
	QString m_overrideEffort;
	// 已经记过日志的"被忽略事件类型"（每类只记一次，别把排障通道淹掉）
	QSet<QString> m_loggedEventTypes;
	// 后台任务名册（每行 { id, kind, label, status, progress, detail, live, startedAt, finishedAt }）
	QVariantList m_jobs;
	QVector<Pending> m_pending;
	QString m_questionRpcId;
	QString m_questionSessionId;
	QVariantList m_questions;  // [{id, header, question, multiSelect, options:[{label,description}]}]
	QVariantList m_answerSelections;  // [{id, selected:[label…], custom:""}]
	QString m_approvalRpcId;
	QString m_approvalSessionId;
	QString m_approvalId;
	QString m_approvalToolName;
	QString m_approvalReason;
	QString m_sessionId;
	QJsonArray m_historyRecords;
	int m_cursor = 0;         // session/page 的 throughSeq
	bool m_hasOlder = false;  // 服务端说还有更早的
	bool m_loadingOlder = false;
	bool m_sessionLoading = false;
	bool m_streaming = false;
	bool m_cancelPending = false;
	qint64 m_cancelSentAtMs = 0;         // 上一次发出 cancel 的时刻（连点去重）
	QTimer* m_cancelWatchdog = nullptr;  // 发出 cancel 后的收尾兜底（见 kCancelConfirmMs）
	// 本轮（自最近一次 beginTurn 起）是否已有可见输出：无输出且回合收尾时补一条兜底提示，
	// 避免用户对着自己的消息发呆（无模型 / 模型空回复 / 事件解析不出）—— 同一套判定
	bool m_turnHadOutput = false;
	bool m_lastTurnHadOutput = true;
};
