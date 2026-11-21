#pragma once
// QML 暴露面：组合 common 服务，不继承、不持有 Hub。
#include "qmlbridge/BridgeCommon.h"

class CommandBridge : public QObject
{
	Q_OBJECT
	Q_PROPERTY(QVariantList items READ items NOTIFY itemsChanged)
	Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
	Q_PROPERTY(bool available READ available NOTIFY itemsChanged)

public:
	explicit CommandBridge(CommandController* controller, QObject* parent = nullptr);

	QVariantList items() const { return m_controller->items(); }
	bool busy() const { return m_controller->busy(); }
	bool available() const { return m_controller->available(); }

	Q_INVOKABLE QVariantList matching(const QString& text) const
	{
		return m_controller->matching(text);
	}
	Q_INVOKABLE void refresh(const QString& sessionId) { m_controller->refresh(sessionId); }
	Q_INVOKABLE void execute(const QString& sessionId, const QString& line)
	{
		m_controller->execute(sessionId, line);
	}

signals:
	void itemsChanged();
	void busyChanged();

private:
	CommandController* m_controller = nullptr;
};