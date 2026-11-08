#pragma once

// 多语言宿主接口。由 TranslationManager 实现（登记名 kTranslationNotifier）。

#include <QString>
#include <QStringList>
#include <qplugin.h>

class VirtualTranslation
{
public:
	virtual ~VirtualTranslation() = default;

	// 空串 = 跟随系统。
	virtual QString ExternalLanguage() = 0;
	virtual QStringList ExternalLanguageCodes() = 0;
	virtual QString ExternalLanguageName(const QString& code) = 0;

	// 立即切换并写设置；缺 .qm 时返回 false。
	virtual bool ExternalSetLanguage(const QString& code) = 0;
};
Q_DECLARE_INTERFACE(VirtualTranslation, "com.ulinhub.VirtualTranslation/1.0")
