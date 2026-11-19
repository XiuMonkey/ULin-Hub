// Linux 没有 Windows COM/IDispatch。保持与 Windows 相同的 comcall::invoke 入口，
// 让 DllCaller 的 InterfaceType="com" 在 Linux 上返回明确错误，而不是链接失败。
#include "ExtensionSystem/ComCaller.h"

#include <QJsonObject>
#include <QString>

namespace comcall
{
	bool invoke(const QJsonObject&, const QJsonObject&, QJsonObject&, QString& error)
	{
		error = QStringLiteral("COM automation is not available on Linux");
		return false;
	}
}
