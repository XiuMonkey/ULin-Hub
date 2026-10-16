// 见 TerminalBridge.h：右栏「终端」块的接线层（QML 单例 `Terminal`）。
// 协议依据：build/temp/terminal-probe/TERMINAL-REPORT.md（§2 契约、§4 实测、§5.5 MVP 清单）。

#include "qmlbridge/TerminalBridge.h"

#include "network/DshApiClient.h"
#include "network/SessionCommands.h"

#include <QDebug>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QTimer>
#include <QUuid>
#include <QWebChannel>

namespace
{
	// 待投队列的上限：超了就不硬扛，断开重连取一份新快照（屏幕照样对，内存有界）。
	// 页面侧的背压是"一帧在途、ack 才放行"，所以这个队列只在页面跟不上时才涨。
	constexpr qint64 kMaxPendingBytes = 8 * 1024 * 1024;

	// "首帧必须是 snapshot" 的兜底：页面收不到 ack 也不能把整条链卡死（页面崩了没来得及 pageGone）
	constexpr int kFrameWatchdogMs = 5000;

	// gap / 协议异常的重连预算：连着错 3 次就认输，改成面板上的错误状态行
	constexpr int kMaxRetries = 3;

	QString newId()
	{
		return QUuid::createUuid().toString(QUuid::WithoutBraces);
	}

	// 输入切片的边界：只在**完整码点**处切，别把多字节字符（或代理对）劈开
	QList<QByteArray> sliceInput(const QString& data, int maxBytes)
	{
		QList<QByteArray> chunks;
		if (data.isEmpty())
			return chunks;

		const int limit = qMax(1, maxBytes);
		QString current;
		int currentBytes = 0;
		for (int i = 0; i < data.size(); ++i) {
			QString piece = data.mid(i, 1);
			if (piece.at(0).isHighSurrogate() && i + 1 < data.size()) {   // 代理对成对走，别拆
				piece += data.at(i + 1);
				++i;
			}

			const int bytes = piece.toUtf8().size();
			if (currentBytes > 0 && currentBytes + bytes > limit) {
				chunks.append(current.toUtf8());
				current.clear();
				currentBytes = 0;
			}
			current += piece;
			currentBytes += bytes;
		}
		if (!current.isEmpty())
			chunks.append(current.toUtf8());
		return chunks;
	}
}

// ------------------------------------------------------------------
// TerminalSession
// ------------------------------------------------------------------

TerminalSession::TerminalSession(const QString& blockId, DshApiClient* api, QObject* parent)
	: QObject(parent)
	, m_api(api)
	, m_blockId(blockId)
{
	setObjectName(blockId);   // 只为日志/调试好看：注册到 QWebChannel 的是桥自己（见 TerminalBridge 段）

	if (!m_api)
		return;

	// 帧/流的接线都在这里：DshApiClient 上同时可能有好几条流，用 streamId 分流（见各回调）
	connect(m_api, &DshApiClient::terminalFrame, this, &TerminalSession::onFrame);
	connect(m_api, &DshApiClient::terminalStreamOpened, this, &TerminalSession::onStreamOpened);
	connect(m_api, &DshApiClient::terminalStreamEnded, this, &TerminalSession::onStreamEnded);
	connect(m_api, &DshApiClient::terminalStreamError, this, &TerminalSession::onStreamError);
	// mux（重）连上：附着若已断就重新附着（换新 attachmentId ⇒ 首帧又是一份 snapshot，画面自动恢复）
	connect(m_api, &DshApiClient::connected, this, [this]() {
		if (!m_pageReady || m_terminalId.isEmpty() || !m_streamId.isEmpty())
			return;
		if (m_attachState == QStringLiteral("closed") || m_attachState == QStringLiteral("error"))
			return;
		qInfo().noquote() << "[Terminal] mux 已连上，重新附着 block=" << m_blockId;
		attach();
	});
}

void TerminalSession::setAttachState(const QString& state)
{
	if (m_attachState == state)
		return;
	m_attachState = state;
	emit stateChanged();
}

void TerminalSession::setStatusText(const QString& text)
{
	if (m_statusText == text)
		return;
	m_statusText = text;
	emit statusTextChanged();
}

void TerminalSession::setReadOnly(bool value)
{
	if (m_readOnly == value)
		return;
	m_readOnly = value;
	emit readOnlyChanged();
}

// WebTerminalInfo → 本地镜像。title/cols/rows 是**引擎报的**，只用来做初值和错误提示，
// 页面的实际尺寸以页面量出来的为准（resize 会把它推回引擎）。
void TerminalSession::applyInfo(const QJsonObject& info)
{
	const QString title = info.value(QStringLiteral("title")).toString();
	if (!title.isEmpty() && title != m_title) {
		m_title = title;
		emit titleChanged();
	}

	const QString processState = info.value(QStringLiteral("state")).toString();
	if (!processState.isEmpty())
		m_processState = processState;

	if (info.contains(QStringLiteral("cols")))
		m_cols = info.value(QStringLiteral("cols")).toInt(m_cols);
	if (info.contains(QStringLiteral("rows")))
		m_rows = info.value(QStringLiteral("rows")).toInt(m_rows);
}

void TerminalSession::start(const QString& sessionId)
{
	if (sessionId.isEmpty()) {
		setAttachState(QStringLiteral("error"));
		setStatusText(QStringLiteral("当前没有会话：先在左侧新建或选中一个会话，终端才有归属。"));
		return;
	}

	if (m_mounting && m_sessionId == sessionId)
		return;   // 正在开，别重复走一遍 mount

	if (!m_terminalId.isEmpty() && m_sessionId == sessionId) {
		// 同一个会话再来一次（页面重载/右栏收起又打开）= 重新附着，不新建终端
		attach();
		return;
	}

	detachStream();
	m_sessionId = sessionId;
	m_terminalId.clear();
	m_attachmentId.clear();
	m_title.clear();
	m_processState.clear();
	m_retryCount = 0;
	m_readOnly = false;
	beginMount();
}

void TerminalSession::shutdown()
{
	detachStream();

	if (m_terminalId.isEmpty() || m_sessionId.isEmpty()) {
		setAttachState(QStringLiteral("closed"));
		setStatusText(QStringLiteral("终端已关闭。"));
		return;
	}

	const QString terminalId = m_terminalId;
	const QString sessionId = m_sessionId;
	m_terminalId.clear();
	m_attachmentId.clear();
	setAttachState(QStringLiteral("closed"));
	setStatusText(QStringLiteral("终端已关闭。"));

	if (!m_api)
		return;
	m_api->callMethod(QStringLiteral("terminal/close"),
		SessionCommands::terminalClose(sessionId, terminalId),
		[this](const QJsonObject&) {
			qInfo().noquote() << "[Terminal] terminal/close ok block=" << m_blockId;
		},
		[this](const DshApiClient::RpcError& error) {
			qWarning().noquote() << "[Terminal] terminal/close 失败 block=" << m_blockId
				<< error.code << error.message;
		});
}

// mount：environment（拿能力上限）→ list（有 running 的就 adopt）→ 否则 shells + create
void TerminalSession::beginMount()
{
	if (!m_api) {
		fail(QStringLiteral("终端通道没接上（DshApiClient 还没就绪）。"));
		return;
	}

	m_mounting = true;
	setAttachState(QStringLiteral("mounting"));
	setStatusText(QStringLiteral("正在打开终端…"));

	const QString sessionId = m_sessionId;
	m_api->callMethod(QStringLiteral("terminal/environment"),
		SessionCommands::terminalEnvironment(sessionId),
		[this](const QJsonObject& environment) {
			m_maxCols = environment.value(QStringLiteral("maxCols")).toInt(500);
			m_maxRows = environment.value(QStringLiteral("maxRows")).toInt(200);
			m_maxInputBytes = environment.value(QStringLiteral("maxInputBytes")).toInt(65536);
			qInfo().noquote() << "[Terminal] environment block=" << m_blockId
				<< "maxCols=" << m_maxCols << "maxRows=" << m_maxRows
				<< "maxInputBytes=" << m_maxInputBytes
				<< "scrollback=" << environment.value(QStringLiteral("scrollback")).toInt();
			mountFromList();
		},
		[this](const DshApiClient::RpcError& error) {
			m_mounting = false;
			// 引擎没挂控制器时 /api/terminal/* 直接 404，DshApiClient 把这种 HTTP 失败归到 transport
			// （消息里是 "server replied: Not Found"），所以这里两种形状都要认 —— 这条提示是本机
			// "resources 指向旧打包负载"时的第一现场，别让它退化成一句晦涩的 transport 错误。
			const bool notMounted = error.code == QStringLiteral("gateway/not-found")
				|| error.code == QStringLiteral("http-404")
				|| error.message.contains(QStringLiteral("404"))
				|| error.message.contains(QStringLiteral("Not Found"), Qt::CaseInsensitive);
			if (error.code == QStringLiteral("session/not-found"))
				fail(QStringLiteral("这个会话在引擎里找不到了（session/not-found）。"));
			else if (notMounted)
				fail(QStringLiteral("引擎没挂终端控制器：/api/terminal/* 全是 404。"
					"profile 里缺 api-terminal-controller 那行，或引擎负载里没有这个包。"));
			else
				fail(QStringLiteral("终端环境查询失败：%1 %2").arg(error.code, error.message));
		});
}

void TerminalSession::mountFromList()
{
	const QString sessionId = m_sessionId;
	m_api->callMethodValue(QStringLiteral("terminal/list"), SessionCommands::terminalList(sessionId),
		[this](const QJsonValue& value) {
			const QJsonArray terminals = value.toArray();
			// adopt：只挑还 running 的（exited/failed 的留着也没用，下面会新建）
			for (const auto& item : terminals) {
				const QJsonObject info = item.toObject();
				if (info.value(QStringLiteral("state")).toString() != QStringLiteral("running"))
					continue;

				m_terminalId = info.value(QStringLiteral("id")).toString();
				if (m_terminalId.isEmpty())
					continue;
				applyInfo(info);
				m_mounting = false;
				qInfo().noquote() << "[Terminal] adopt 已有终端 block=" << m_blockId
					<< "id=" << m_terminalId << m_cols << "x" << m_rows;
				setStatusText(QStringLiteral("已接上已有终端 %1（%2×%3）。")
					.arg(m_title.isEmpty() ? m_terminalId.left(8) : m_title)
					.arg(m_cols).arg(m_rows));
				attach();
				return;
			}

			// 没有可用终端：拿 shell 清单（第一项 = 执行环境的默认 shell）
			m_api->callMethodValue(QStringLiteral("terminal/shells"), SessionCommands::terminalShells(m_sessionId),
				[this](const QJsonValue& shellsValue) {
					QString shellPath;
					const QJsonArray shells = shellsValue.toArray();
					if (!shells.isEmpty())
						shellPath = shells.first().toObject().value(QStringLiteral("path")).toString();
					createTerminal(shellPath);
				},
				[this](const DshApiClient::RpcError& error) {
					m_mounting = false;
					fail(QStringLiteral("取 shell 清单失败：%1 %2").arg(error.code, error.message));
				});
		},
		[this](const DshApiClient::RpcError& error) {
			m_mounting = false;
			fail(QStringLiteral("列出终端失败：%1 %2").arg(error.code, error.message));
		});
}

void TerminalSession::createTerminal(const QString& shellPath)
{
	m_terminalId = newId();
	m_cols = qMin(80, m_maxCols);
	m_rows = qMin(24, m_maxRows);

	const QString terminalId = m_terminalId;
	qInfo().noquote() << "[Terminal] terminal/create block=" << m_blockId << "id=" << terminalId
		<< "shell=" << (shellPath.isEmpty() ? QStringLiteral("(默认)") : shellPath)
		<< m_cols << "x" << m_rows;

	m_api->callMethod(QStringLiteral("terminal/create"),
		SessionCommands::terminalCreate(m_sessionId, terminalId, m_cols, m_rows, shellPath),
		[this](const QJsonObject& info) {
			applyInfo(info);
			m_mounting = false;
			setStatusText(QStringLiteral("已打开 %1（%2×%3）。")
				.arg(m_title.isEmpty() ? QStringLiteral("终端") : m_title)
				.arg(m_cols).arg(m_rows));
			attach();
		},
		[this](const DshApiClient::RpcError& error) {
			m_mounting = false;
			if (error.code == QStringLiteral("terminal/limit-reached"))
				fail(QStringLiteral("终端数量到上限了：先关掉一个再开。"));
			else if (error.code == QStringLiteral("terminal/control-unavailable"))
				noteControlUnavailable(error.message);
			else
				fail(QStringLiteral("新建终端失败：%1 %2").arg(error.code, error.message));
		});
}

// 附着：**每次换新的 attachmentId**（引擎按它认"当前谁在控制"）
void TerminalSession::attach()
{
	if (!m_api || m_terminalId.isEmpty() || m_sessionId.isEmpty())
		return;
	if (!m_pageReady) {
		setStatusText(QStringLiteral("终端已就绪，等页面装载…"));
		return;
	}

	m_attachmentId = newId();
	m_sequence = -1;
	m_awaitSnapshot = true;      // 新的一代：第一帧必须是 snapshot
	m_frames.clear();
	m_pendingBytes = 0;
	m_frameInFlight = false;
	if (m_frameWatchdog)
		m_frameWatchdog->stop();

	m_api->openTerminalStream(SessionCommands::terminalFollow(m_sessionId, m_terminalId, m_attachmentId));

	if (m_retryCount == 0)
		setAttachState(QStringLiteral("attached"));
}

void TerminalSession::detachStream()
{
	m_attachmentId.clear();
	m_awaitSnapshot = false;
	m_sequence = -1;
	m_streamId.clear();
	m_frames.clear();
	m_pendingBytes = 0;
	m_frameInFlight = false;
	m_writes.clear();
	m_writeInFlight = false;
	if (m_frameWatchdog)
		m_frameWatchdog->stop();
	if (m_api)
		m_api->closeTerminalStream();
}

void TerminalSession::fail(const QString& status)
{
	setAttachState(QStringLiteral("error"));
	setStatusText(status);
	qWarning().noquote() << "[Terminal] 失败 block=" << m_blockId << status;
}

// 页面重载/切块回来都要重新走一遍：断开旧流、开新流、等新快照
void TerminalSession::resetForNewAttachment(const QString& why)
{
	if (m_retryCount >= kMaxRetries) {
		fail(QStringLiteral("终端输出对不上（%1），已重试 %2 次后放弃。").arg(why).arg(m_retryCount));
		return;
	}

	++m_retryCount;
	setAttachState(QStringLiteral("reconnecting"));
	setStatusText(QStringLiteral("终端画面正在重新同步（%1）…").arg(why));
	qWarning().noquote() << "[Terminal] reattach block=" << m_blockId << "reason=" << why
		<< "retry=" << m_retryCount;

	detachStream();
	attach();
}

void TerminalSession::pageReady()
{
	m_pageReady = true;
	if (!m_terminalId.isEmpty())
		attach();
}

void TerminalSession::pageGone()
{
	m_pageReady = false;
	detachStream();   // 只断附着：终端进程继续跑，回来时取新快照
	if (!m_terminalId.isEmpty() && m_attachState != QStringLiteral("closed"))
		setAttachState(QStringLiteral("idle"));
}

// ------------------------------------------------------------------
// 帧路径
// ------------------------------------------------------------------

void TerminalSession::onStreamOpened(const QString& streamId)
{
	m_streamId = streamId;
}

void TerminalSession::onFrame(const QString& streamId, const QJsonObject& frame)
{
	if (streamId.isEmpty() || streamId != m_streamId)
		return;   // 上一代的迟到帧

	const QString type = frame.value(QStringLiteral("type")).toString();

	if (type == QStringLiteral("snapshot")) {
		if (!m_awaitSnapshot)   // 同一代里又来一份快照：按协议不该发生，重新同步
			return resetForNewAttachment(QStringLiteral("同一附着里重复出现 snapshot"));

		m_awaitSnapshot = false;
		m_sequence = frame.value(QStringLiteral("sequence")).toInt();
		applyInfo(frame.value(QStringLiteral("info")).toObject());
		m_retryCount = 0;
		if (m_attachState != QStringLiteral("attached"))
			setAttachState(QStringLiteral("attached"));
		m_readOnly = false;
		emit readOnlyChanged();
		setStatusText(QStringLiteral("已连接 %1（%2×%3）。")
			.arg(m_title.isEmpty() ? QStringLiteral("终端") : m_title).arg(m_cols).arg(m_rows));
		enqueueFrame(QStringLiteral("snapshot"), m_sequence, frame.value(QStringLiteral("screen")).toString());
		pushResize();   // 附着已被引擎认下：这时才把页面量出来的尺寸推过去
		return;
	}

	if (type == QStringLiteral("output")) {
		if (m_awaitSnapshot)   // 首帧不是 snapshot ⇒ 一定丢了开场画面，重连取新的
			return resetForNewAttachment(QStringLiteral("首帧不是 snapshot"));

		const int sequence = frame.value(QStringLiteral("sequence")).toInt();
		if (sequence != m_sequence + 1) {
			// gap：VT 流不能跳帧（官方客户端也当致命）⇒ 重连拿一份完整快照
			return resetForNewAttachment(QStringLiteral("sequence %1 → %2 不连续")
				.arg(m_sequence).arg(sequence));
		}

		m_sequence = sequence;
		enqueueFrame(QStringLiteral("output"), sequence, frame.value(QStringLiteral("data")).toString());
		return;
	}

	if (type == QStringLiteral("state")) {
		// 不占 sequence：只推 info（rename / resize / 进程退出都会来一帧）
		const QJsonObject info = frame.value(QStringLiteral("info")).toObject();
		applyInfo(info);
		const QString processState = info.value(QStringLiteral("state")).toString();
		if (processState == QStringLiteral("exited") || processState == QStringLiteral("failed")) {
			setAttachState(QStringLiteral("closed"));
			setStatusText(QStringLiteral("终端进程已退出（%1）%2")
				.arg(processState == QStringLiteral("failed") ? QStringLiteral("异常") : QStringLiteral("正常"))
				.arg(info.contains(QStringLiteral("exitCode"))
					? QStringLiteral("，退出码 %1").arg(info.value(QStringLiteral("exitCode")).toInt())
					: QString()));
		}
		return;
	}
}

void TerminalSession::onStreamEnded(const QString& streamId)
{
	if (streamId != m_streamId)
		return;
	m_streamId.clear();

	if (m_attachState == QStringLiteral("closed"))   // 终端进程没了：不再附着
		return;

	// 附着断了（mux 重连也会走到这里）：等 DshApiClient::connected 后由 TerminalBridge 重新 start()
	m_awaitSnapshot = false;
	setAttachState(QStringLiteral("reconnecting"));
	setStatusText(QStringLiteral("终端连接断开，等待重连…"));
}

void TerminalSession::onStreamError(const QString& streamId, const QString& code, const QString& message)
{
	if (streamId != m_streamId)
		return;
	m_streamId.clear();

	if (code == QStringLiteral("terminal/control-unavailable")) {
		noteControlUnavailable(message);
		return;
	}
	if (code == QStringLiteral("terminal/unavailable")) {
		// 终端在引擎侧已经没了（被别的窗口关了 / 会话换了）
		m_terminalId.clear();
		m_attachmentId.clear();
		setAttachState(QStringLiteral("closed"));
		setStatusText(QStringLiteral("这个终端已经不存在了（terminal/unavailable）。"));
		return;
	}
	fail(QStringLiteral("终端流出错：%1 %2").arg(code, message));
}

void TerminalSession::noteControlUnavailable(const QString& message)
{
	// 只读不是致命错：另一个 attachment 拿着输入权（官方客户端也这么处理）
	setReadOnly(true);
	setStatusText(QStringLiteral("输入被另一个窗口接管，这里是只读的（%1）。")
		.arg(message.isEmpty() ? QStringLiteral("control-unavailable") : message));
}

void TerminalSession::enqueueFrame(const QString& kind, int sequence, const QString& text)
{
	m_frames.enqueue(PendingFrame{ kind, sequence, text });
	m_pendingBytes += text.size() * 2;   // 粗算（QString 是 UTF-16）；只用它当"内存护栏"的量级判据

	if (m_pendingBytes > kMaxPendingBytes) {
		resetForNewAttachment(QStringLiteral("页面跟不上，积压超过 8MB"));
		return;
	}

	pumpFrames();
}

void TerminalSession::pumpFrames()
{
	if (m_frameInFlight || m_frames.isEmpty())
		return;

	const PendingFrame frame = m_frames.dequeue();
	m_pendingBytes -= frame.text.size() * 2;
	if (m_pendingBytes < 0)
		m_pendingBytes = 0;

	m_frameInFlight = true;
	emit frameReady(frame.kind, frame.sequence, frame.text);

	if (!m_frameWatchdog) {
		m_frameWatchdog = new QTimer(this);
		m_frameWatchdog->setSingleShot(true);
		connect(m_frameWatchdog, &QTimer::timeout, this, [this]() {
			// 页面没回 ack（崩了/太久）：不能把整条链卡死，放行下一帧
			qWarning().noquote() << "[Terminal] 页面 " << kFrameWatchdogMs
				<< "ms 没 ack，强制放行下一帧 block=" << m_blockId;
			m_frameInFlight = false;
			pumpFrames();
		});
	}
	m_frameWatchdog->start(kFrameWatchdogMs);
}

void TerminalSession::ack()
{
	if (m_frameWatchdog)
		m_frameWatchdog->stop();
	m_frameInFlight = false;
	pumpFrames();
}

// 页面里的诊断出口（落项目日志 logs/general/info.log）
void TerminalSession::log(const QString& message)
{
	qInfo().noquote() << "[Terminal][page] block=" << m_blockId << message;
}

// ------------------------------------------------------------------
// 输入路径（串行化 + maxInputBytes 记账）
// ------------------------------------------------------------------

void TerminalSession::write(const QString& data)
{
	if (data.isEmpty())
		return;
	if (m_readOnly) {
		qInfo().noquote() << "[Terminal] 只读态丢弃输入 block=" << m_blockId;
		return;
	}
	if (!m_api || m_terminalId.isEmpty() || m_attachmentId.isEmpty()) {
		// 还没附着上：丢掉比乱序发更好（用户看到的是"没反应"，日志里有据可查）
		qInfo().noquote() << "[Terminal] 未附着，丢弃输入 block=" << m_blockId;
		return;
	}

	for (const QByteArray& chunk : sliceInput(data, m_maxInputBytes))
		m_writes.enqueue(chunk);
	pumpWrites();
}

void TerminalSession::pumpWrites()
{
	if (m_writeInFlight || m_writes.isEmpty())
		return;

	const QString chunk = QString::fromUtf8(m_writes.dequeue());
	m_writeInFlight = true;

	m_api->callMethod(QStringLiteral("terminal/write"),
		SessionCommands::terminalWrite(m_sessionId, m_terminalId, m_attachmentId, chunk),
		[this](const QJsonObject&) {
			m_writeInFlight = false;
			pumpWrites();
		},
		[this](const DshApiClient::RpcError& error) {
			m_writeInFlight = false;
			if (error.code == QStringLiteral("terminal/control-unavailable"))
				noteControlUnavailable(error.message);
			else
				qWarning().noquote() << "[Terminal] terminal/write 失败 block=" << m_blockId
					<< error.code << error.message;
			pumpWrites();
		});
}

void TerminalSession::resize(int cols, int rows)
{
	m_wantCols = qBound(2, cols, qMax(2, m_maxCols));
	m_wantRows = qBound(1, rows, qMax(1, m_maxRows));
	pushResize();
}

// 真正把期望尺寸推给引擎：只在"已附着 + 尺寸确实变了"时发。
// 刚 attach 时不能马上发（引擎还没认下这个 attachmentId），所以 snapshot 到达后也会再调一次。
void TerminalSession::pushResize()
{
	if (m_wantCols <= 0 || m_wantRows <= 0)
		return;
	if (m_wantCols == m_cols && m_wantRows == m_rows)
		return;   // 同尺寸不发（官方客户端也这样省 RPC）
	if (!m_api || m_terminalId.isEmpty() || m_attachmentId.isEmpty())
		return;

	m_cols = m_wantCols;
	m_rows = m_wantRows;
	m_api->callMethod(QStringLiteral("terminal/resize"),
		SessionCommands::terminalResize(m_sessionId, m_terminalId, m_attachmentId, m_cols, m_rows),
		[this](const QJsonObject&) {
			qInfo().noquote() << "[Terminal] resize ->" << m_cols << "x" << m_rows << "block=" << m_blockId;
		},
		[this](const DshApiClient::RpcError& error) {
			if (error.code == QStringLiteral("terminal/control-unavailable"))
				noteControlUnavailable(error.message);
			else
				qWarning().noquote() << "[Terminal] terminal/resize 失败 block=" << m_blockId
					<< error.code << error.message;
		});
}

// ------------------------------------------------------------------
// TerminalBridge：QWebChannel 的注册对象（objectName = "ulinterm"）。
// 注册的是**桥自己**而不是每个会话对象 —— QQmlWebChannel 只在组件完成时注册一次
// （事后改 registeredObjects 不生效），而"块"是会增删的；所以页面上的每个调用都带 blockId。
// ------------------------------------------------------------------

TerminalBridge& TerminalBridge::instance()
{
	static TerminalBridge bridge;
	return bridge;
}

TerminalBridge::TerminalBridge(QObject* parent)
	: QObject(parent)
{
}

void TerminalBridge::init(DshApiClient* api)
{
	m_api = api;
	// QWebChannel 用 objectName 当注册 key（QML 侧 WebChannel.registeredObjects: [Terminal]）；
	// 没有它会注册成一个空 key，页面就找不到对象
	setObjectName(QStringLiteral("ulinterm"));
}

// 把本桥注册到一个 QWebChannel 上。为什么不在 QML 里写 `registeredObjects: [Terminal]`：
// 那是个 JS 数组字面量，而 C++ 注册的 QML 单例在数组元素的位置上是"类型包装"，转 QObject* 得到 null
// —— 实测会在面板创建时打出 `Cannot register null object to WebChannel`，页面永远收不到对象。
// 从 C++ 注册既确定又可日志；同一个桥注册到多个面板的 channel 是允许的（每个 channel 各自一份注册表）。
bool TerminalBridge::registerOn(QObject* channel, QObject* transport)
{
	QWebChannel* webChannel = qobject_cast<QWebChannel*>(channel);
	if (!webChannel) {
		qWarning().noquote() << "[Terminal] registerOn: 拿到的不是 QWebChannel：" << channel;
		return false;
	}
	if (m_channels.contains(webChannel))
		return true;

	// 后端被扩展顶替时注册它的对象，本桥只留 QML 面那几处转接（pageGone / closeBlock）。
	// 页面按 channel.objects 的第一个 key 取对象、不看类型，所以两种情况下 id 都保持 "ulinterm"。
	QObject* target = transport ? transport : this;
	m_transport = transport;
	if (target->objectName().isEmpty())
		target->setObjectName(objectName());

	webChannel->registerObject(target->objectName(), target);
	m_channels.insert(webChannel);
	connect(webChannel, &QObject::destroyed, this, [this, webChannel]() { m_channels.remove(webChannel); });
	qInfo().noquote() << "[Terminal] 已注册到 WebChannel id=" << target->objectName()
					  << (transport ? "（扩展后端）" : "（内置桥）");
	return true;
}

// 会话的信号 → 桥的上行信号（都补上 blockId，页面按自己的块过滤）
void TerminalBridge::wireSession(TerminalSession* session)
{
	const QString blockId = session->blockId();

	connect(session, &TerminalSession::frameReady, this,
		[this, blockId](const QString& kind, int sequence, const QString& text) {
			emit frameReady(blockId, kind, sequence, text);
		});
	const auto relay = [this, session]() { emitStatus(session); };
	connect(session, &TerminalSession::stateChanged, this, relay);
	connect(session, &TerminalSession::statusTextChanged, this, relay);
	connect(session, &TerminalSession::readOnlyChanged, this, relay);
	connect(session, &TerminalSession::titleChanged, this, relay);
}

void TerminalBridge::emitStatus(TerminalSession* session)
{
	if (!session)
		return;
	emit statusChanged(session->blockId(), session->state(), session->statusText(),
		session->readOnly(), session->title());
}

TerminalSession* TerminalBridge::findSession(const QString& blockId) const
{
	const auto it = m_sessions.constFind(blockId);
	return it == m_sessions.constEnd() ? nullptr : it.value();
}

// 取/建会话：页面绑定时（pageReady/start）也要能建出会话，所以不只 session() 会建
TerminalSession* TerminalBridge::ensureSession(const QString& blockId)
{
	if (blockId.isEmpty()) {
		qWarning().noquote() << "[Terminal] 空 blockId";
		return nullptr;
	}

	if (TerminalSession* existing = findSession(blockId))
		return existing;

	TerminalSession* session = new TerminalSession(blockId, m_api, this);
	m_sessions.insert(blockId, session);
	wireSession(session);
	qInfo().noquote() << "[Terminal] 新建会话对象 block=" << blockId;
	return session;
}

QObject* TerminalBridge::session(const QString& blockId)
{
	return ensureSession(blockId);
}

void TerminalBridge::closeBlock(const QString& blockId)
{
	// 后端被扩展顶替：会话由它管，本桥里没有这个块的 TerminalSession（拿不到就只能什么都不做）
	if (m_transport) {
		if (!QMetaObject::invokeMethod(m_transport, "closeBlock", Q_ARG(QString, blockId)))
			qWarning().noquote() << "[Terminal] 扩展后端没有 closeBlock(QString) 槽，关块没能转过去：" << blockId;
		return;
	}

	const auto it = m_sessions.find(blockId);
	if (it == m_sessions.end())
		return;

	TerminalSession* session = it.value();
	m_sessions.erase(it);
	session->shutdown();
	session->deleteLater();
	qInfo().noquote() << "[Terminal] 关块 block=" << blockId;
}

// 页面刚接上 QWebChannel：叫宿主把"这个页面属于哪个块"推过去（幂等，见头文件）
void TerminalBridge::pageHello()
{
	emit pageHelloReceived();
}

void TerminalBridge::pageReady(const QString& blockId)
{
	TerminalSession* session = ensureSession(blockId);
	if (!session)
		return;
	session->pageReady();
	emitStatus(session);   // 页面据此画顶部那行提示（页面不主动问状态，避免来回异步取值）
}

void TerminalBridge::pageGone(const QString& blockId)
{
	// 后端被扩展顶替：解绑也归它（本桥没有它的会话，照原路走就是空操作、还没日志）
	if (m_transport) {
		if (!QMetaObject::invokeMethod(m_transport, "pageGone", Q_ARG(QString, blockId)))
			qWarning().noquote() << "[Terminal] 扩展后端没有 pageGone(QString) 槽，解绑没能转过去：" << blockId;
		return;
	}

	if (TerminalSession* session = findSession(blockId))
		session->pageGone();
}

void TerminalBridge::start(const QString& blockId, const QString& sessionId)
{
	if (TerminalSession* session = ensureSession(blockId))
		session->start(sessionId);
}

void TerminalBridge::write(const QString& blockId, const QString& data)
{
	if (TerminalSession* session = findSession(blockId))
		session->write(data);
}

void TerminalBridge::resize(const QString& blockId, int cols, int rows)
{
	if (TerminalSession* session = findSession(blockId))
		session->resize(cols, rows);
}

void TerminalBridge::ack(const QString& blockId)
{
	if (TerminalSession* session = findSession(blockId))
		session->ack();
}

void TerminalBridge::log(const QString& blockId, const QString& message)
{
	if (TerminalSession* session = findSession(blockId))
		session->log(message);
	else
		qInfo().noquote() << "[Terminal][page] block=" << blockId << message;
}
