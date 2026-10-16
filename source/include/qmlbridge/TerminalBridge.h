#pragma once

// TerminalBridge —— 右栏面板坞「终端」块的接线层（QML 单例名 `Terminal`）。
//
// 为什么是"接线"而不是自绘终端：引擎（@deepseek-ai/dsh-api-terminal-controller）给客户端的只有
// **原始 PTY 字节流 + 一份序列化的 VT 重放串**（`terminal/follow` 的 snapshot.screen 是
// @xterm/addon-serialize 的产物），没有任何"单元格网格"接口 —— 自绘等于重写一个 xterm。
// 所以路线定为：WebEngine 里跑引擎自带包里抽出来的 **xterm.js 6.0.0**，传输留在 C++：
//   · 页面（qrc:/ULinHub/Terminal/terminal.html）一个网络请求都不发（引擎的信任边界要求
//     Origin == Host，qrc: 页面发了就是 403 / WS 1006）；
//   · 本类经既有 DshApiClient 代发 terminal/*（认证 cookie、mux、rpcId 全是现成的）；
//   · 帧经 QWebChannel 推给页面 term.write()，页面 onData 再回来调 write()。
// 调查与实测证据见 build/temp/terminal-probe/TERMINAL-REPORT.md（§2 契约 / §4 实测 / §5.5 MVP）。
//
// 分层：本文件在 qmlbridge/**，不放解析也不放渲染（都在页面里）。它承担的确实是**协议状态**：
//   ① mount 流程：environment → list（有 running 的就 adopt，否则 shells + create，id 客户端铸、
//      create 幂等）；② attach：每次换新 attachmentId，**首帧必须是 snapshot**，否则判协议异常重连；
//   ③ sequence 连续性：gap 视为丢帧（官方客户端也当致命），重连取新快照；
//   ④ 输入串行化 + maxInputBytes(64KB) 记账；⑤ resize 限幅、同尺寸不发；⑥ close；
//   ⑦ terminal/control-unavailable 只置只读，不当致命错误。
// 之所以不收进 common/**：它整体是"贴着 DshApiClient 与 QWebChannel 的接线"，没有能脱离
// QObject/网络独立单测的纯算法；真要抽，抽的也是"sequence 判定 / 切片记账"那几条纯规则。
//
// QWebChannel 的**注册对象是本类（单例）**，不是每个会话对象：WebChannel 只在组件完成时注册一次
// （QQmlWebChannel 的 registeredObjects 事后改不生效），而"块"是会增删的。所以页面上的每个调用都带上
// blockId，由本类路由到对应的 TerminalSession；反过来每一个上行信号也都带 blockId 供页面过滤。

#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QQueue>
#include <QSet>
#include <QString>

class DshApiClient;
class QJsonObject;
class QTimer;
class QWebChannel;
class TerminalVtModel;

// 一个「终端块」的会话状态：终端 id / attachmentId / 尺寸 / 帧序 / 待投队列 / 输入队列。
// 块被切走、右栏收起时页面会销毁，但本对象**活着**：终端进程继续跑，重新绑定页面时换新 attachment
// 再取一份快照恢复画面。只有"关块"才会真去 terminal/close。
class TerminalSession : public QObject
{
	Q_OBJECT
		Q_PROPERTY(QString blockId READ blockId CONSTANT)
		Q_PROPERTY(QString state READ state NOTIFY stateChanged)              // idle|mounting|attached|reconnecting|closed|error
		Q_PROPERTY(QString statusText READ statusText NOTIFY statusTextChanged)
		Q_PROPERTY(bool readOnly READ readOnly NOTIFY readOnlyChanged)
		Q_PROPERTY(QString title READ title NOTIFY titleChanged)

public:
	TerminalSession(const QString& blockId, DshApiClient* api, QObject* parent = nullptr);
	~TerminalSession() override;

	QString blockId() const { return m_blockId; }
	QString state() const { return m_attachState; }
	QString statusText() const { return m_statusText; }
	bool readOnly() const { return m_readOnly; }
	QString title() const { return m_title; }

	// 绑定到某个会话的终端：同会话重复调是"重新附着"，换会话则改挂到那个会话的终端上（旧终端不关）
	void start(const QString& sessionId);
	// 关块：断开附着 + terminal/close（之后 terminal/list 里就没有它了）
	void shutdown();

	// ---- 视图（QML）面 ----
	// 自绘视图挂上来看网格：建 / 取本块的 VT 模型。**由视图调用**，等价于原来页面的 pageReady。
	// 没有绑定就没有快照取帧（见 attach()）—— "谁在看"决定要不要占一条 follow 流。
	// ⚠️ Q_INVOKABLE 的名字要唯一（重载会让 moc 生成两个签名、QML 侧解析不出重载）—— 所以是
	//    "viewModel() 建、vtModel() 取"这一对，别写成重载。
	Q_INVOKABLE TerminalVtModel* viewModel();
	Q_INVOKABLE TerminalVtModel* vtModel() const { return m_vt; }
	// 视图销毁（切块/收右栏）：只断附着，**不关终端**
	void viewGone();
	// 视图键盘编好的字节 → 写队列（Terminal.flushInput 转进来）
	void flushViewModelInput();

	// ---- 页面（JS）面（由 TerminalBridge 按 blockId 转进来）----
	void pageGone();                  // 页面要销毁/换绑了（切块/收右栏），**不关终端**
	void write(const QString& data);  // 键盘/粘贴：与扩展后端同形状的入口 → terminal/write
	void writeBytes(const QByteArray& data);   // 键盘编好的字节（UTF-8 串走 write 就行）
	void resize(int cols, int rows);  // 视图量出来的尺寸 → terminal/resize（限幅/去重）
	void ack();                       // 扩展后端：上一帧已收下，放行下一帧（背压）
	void log(const QString& message); // 诊断信息（页面 console 不落盘）→ 项目日志

signals:
	// Qt → 页面（仅当后端被扩展顶替时还有消费者，见 TerminalBridge::registerOn）
	void frameReady(const QString& kind, int sequence, const QString& text);
	void stateChanged();
	void statusTextChanged();
	void readOnlyChanged();
	void titleChanged();

private:
	// 一帧待投给页面的终端帧
	struct PendingFrame
	{
		QString kind;
		int sequence = -1;
		QString text;
	};

	// ---- 流程 ----
	void beginMount();                                  // environment → list → (adopt | shells+create) → attach
	void mountFromList();                               // list 里挑 running 的 adopt，没有就建
	void createTerminal(const QString& shellPath);
	void attach();                                      // 换新 attachmentId 开 follow 流
	void detachStream();                                // 取消流（不碰终端进程）
	void resetForNewAttachment(const QString& why);     // gap / 首帧不是 snapshot / 队列超限 ⇒ 重连取新快照
	void fail(const QString& status);                   // 置 error 态并写状态行
	void setAttachState(const QString& state);
	void setStatusText(const QString& text);
	void setReadOnly(bool value);
	void applyInfo(const QJsonObject& info);            // WebTerminalInfo → 本地镜像（title/cols/rows/进程态）
	void syncViewModel();                               // 把状态镜像推给 VT 模型（状态行/只读/标题）

	// ---- 帧路径 ----
	void onStreamOpened(const QString& streamId);
	void onFrame(const QString& streamId, const QJsonObject& frame);
	void onStreamEnded(const QString& streamId);
	void onStreamError(const QString& streamId, const QString& code, const QString& message);
	// 自绘路径：直接喂 VT 模型（不再排队等 ack —— 没有页面就没有"跟不上"这回事）
	void feedViewModel(const QString& kind, const QString& text);
	// 扩展后端路径：仍旧按"发一帧、等 ack、再发下一帧"排队
	void enqueueFrame(const QString& kind, int sequence, const QString& text);
	void pumpFrames();

	// ---- 输入路径 ----
	void pumpWrites();
	void pushResize();                                      // 把期望尺寸推给引擎（已附着且确实变了才发）
	void noteControlUnavailable(const QString& message);   // terminal/control-unavailable → 只读

	DshApiClient* m_api = nullptr;
	QString m_blockId;
	QString m_sessionId;        // agentId == sessionId
	QString m_terminalId;       // 客户端铸的幂等键；空 = 还没 create/adopt
	QString m_attachmentId;     // 每次 attach 换新的
	QString m_streamId;         // 当前 follow 流的 streamId（用于分流）

	QString m_attachState = QStringLiteral("idle");
	QString m_processState;     // 引擎报的 running|exited|failed
	QString m_statusText;
	QString m_title;
	bool m_readOnly = false;
	bool m_pageReady = false;   // 有视图挂上来了（自绘视图 bindView，或扩展后端的 pageReady）
	bool m_mounting = false;

	// 自绘网格模型：视图 bindView 时建、随本会话活（终端会话在，VT 状态就在）
	TerminalVtModel* m_vt = nullptr;

	// 引擎能力（terminal/environment）
	int m_maxCols = 500;
	int m_maxRows = 200;
	int m_maxInputBytes = 65536;

	// 尺寸：m_cols/m_rows = 引擎侧已知值；m_wantCols/m_wantRows = 页面量出来的期望值
	int m_cols = 0;
	int m_rows = 0;
	int m_wantCols = 0;
	int m_wantRows = 0;

	// 帧序（snapshot 起算；gap 判定的基准）
	int m_sequence = -1;
	bool m_awaitSnapshot = false;
	int m_retryCount = 0;

	QQueue<PendingFrame> m_frames;
	qint64 m_pendingBytes = 0;
	bool m_frameInFlight = false;
	QTimer* m_frameWatchdog = nullptr;

	QQueue<QByteArray> m_writes;
	bool m_writeInFlight = false;
};

class TerminalBridge : public QObject
{
	Q_OBJECT

public:
	static TerminalBridge& instance();   // 程序级唯一实例；刻意不析构 —— QML 单例注册要求它活过引擎

	// 启动期调用（QML 装载之前）：拿到唯一的出站客户端（RPC 只许从这里走），并给本对象起 objectName
	// （QWebChannel 用它当注册 key：QML 侧 WebChannel.registeredObjects: [Terminal]）
	void init(DshApiClient* api);

	// ---- QML 面 ----
	// 把终端后端的"要注册的对象"注册到 QWebChannel 上（QML 侧的 WebChannel 对象）。**必须**从 C++ 注册，
	// 别在 QML 里写 `registeredObjects: [Terminal]`：那是个 JS 数组字面量，C++ 注册的 QML 单例在这个位置
	// 是"类型包装"，转 QObject* 得到 null（实测日志：`Cannot register null object to WebChannel`）。
	// transport 非空 = 该块后端被扩展顶替（HostUi::panelBackend("terminal")），注册它、页面直连它；
	// 空 = 用内置的本桥。返回是否注册成功。
	Q_INVOKABLE bool registerOn(QObject* channel, QObject* transport);
	// 取某个块的会话对象（按稳定 id 复用，不存在则建）；关块才调 closeBlock
	Q_INVOKABLE QObject* session(const QString& blockId);
	Q_INVOKABLE void closeBlock(const QString& blockId);

	// ---- 自绘视图（QML）面：视图挂上/摘下，以及按块路由输入 ----
	// 视图挂上来（面板给终端块建 TerminalView 时调一次）：建/取该块的网格模型，并让它开始取帧。
	// **这是"谁在看"的唯一入口**：不调它就不会 attach、也不会去引擎建终端。
	Q_INVOKABLE TerminalVtModel* bindView(const QString& blockId);
	// 视图收起来了（切块/收右栏）：只解绑附着，终端进程继续跑
	Q_INVOKABLE void viewGone(const QString& blockId);
	// 视图的键盘：模型攒好的字节取出来写 PTY（terminal/write 的串行化/记账仍在会话里）
	Q_INVOKABLE void flushInput(const QString& blockId);
	// 视图量出来的网格尺寸（列/行）→ 引擎 terminal/resize
	Q_INVOKABLE void applyGridSize(const QString& blockId, int cols, int rows);

	// ---- 页面（JS）面：每个调用都带 blockId，路由到对应会话 ----
	Q_INVOKABLE void pageHello();
	Q_INVOKABLE void pageGone(const QString& blockId);
	Q_INVOKABLE void start(const QString& blockId, const QString& sessionId);
	Q_INVOKABLE void write(const QString& blockId, const QString& data);
	Q_INVOKABLE void resize(const QString& blockId, int cols, int rows);
	Q_INVOKABLE void ack(const QString& blockId);
	Q_INVOKABLE void log(const QString& blockId, const QString& message);

signals:
	// Qt → 页面
	void frameReady(const QString& blockId, const QString& kind, int sequence, const QString& text);
	// 状态行/只读态/标题的变化（页面据此画顶部那行提示）；pageReady 后也会补发一次
	void statusChanged(const QString& blockId, const QString& state, const QString& statusText,
		bool readOnly, const QString& title);
	// 页面刚接上 QWebChannel：叫宿主把"这个页面属于哪个块"推过去（所有面板都会收到，幂等）
	void pageHelloReceived();

private:
	explicit TerminalBridge(QObject* parent = nullptr);
	TerminalBridge(const TerminalBridge&) = delete;
	TerminalBridge& operator=(const TerminalBridge&) = delete;

	// 会话 ↔ 桥的信号转发接线（建会话时接一次）
	void wireSession(TerminalSession* session);
	TerminalSession* findSession(const QString& blockId) const;
	TerminalSession* ensureSession(const QString& blockId);   // 没有就建（页面绑定时也会走到）
	void emitStatus(TerminalSession* session);

	DshApiClient* m_api = nullptr;
	QHash<QString, TerminalSession*> m_sessions;
	QSet<QWebChannel*> m_channels;   // 已注册过的 WebChannel（每个面板一个；同一个桥可注册到多个）
	// 被扩展顶替时的替代后端（HostUi::panelBackend("terminal")，registerOn 时记下）：
	// 页面直连它，本桥只负责把 QML 面那几处（pageGone / closeBlock）转过去
	QPointer<QObject> m_transport;
};
