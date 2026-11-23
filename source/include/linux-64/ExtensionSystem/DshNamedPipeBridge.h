#pragma once

// ULin Hub 客户端的命名管道桥接服务：监听 ulinhub-bridge（Windows 上是 \\.\pipe\ulinhub-bridge，Linux 上是 QLocalServer 本地套接字），
// Node/DSh server 可连接本管道，以 JSON + 换行 的协议发送工具调用请求，由客户端负责装载 DLL 并返回结果。

#include <QObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QHash>
#include <QByteArray>
#include <QJsonObject>

class DshNamedPipeBridge : public QObject
{
	Q_OBJECT

public:
	explicit DshNamedPipeBridge(QObject* parent = nullptr);

	// 启动监听；pipeName 是 QLocalServer 的服务名，重复启动会先停掉旧监听
	bool start(const QString& pipeName = QStringLiteral("ulinhub-bridge"));
	void stop();

	QString errorString() const;

	// 向某个连接回一条响应（协议：JSON + '\n'）；socket 为空或已断开时静默丢弃
	void sendResponse(QLocalSocket* socket, int id, bool ok, const QJsonObject& result, const QString& error = QString());

signals:
	// 收到一个完整的请求行（JSON 已被解析）
	void requestReceived(int id, const QString& tool, const QJsonObject& args, QLocalSocket* socket);

private slots:
	void onNewConnection();
	void onReadyRead();
	void onDisconnected();

private:
	void handleLine(QLocalSocket* socket, const QByteArray& line);

	QLocalServer* m_server = nullptr;
	QHash<QLocalSocket*, QByteArray> m_buffers;   // 每连接的行缓冲：只留没凑满一行的尾部
	QString m_pipeName;
	QString m_errorString;
};
