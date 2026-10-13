// 见 StyleResolver.h：QML 风格包的解析与 ABI 门控。

#include "qmlbridge/StyleResolver.h"

#include "common/appearance/ThemeManager.h"

#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QUrl>

namespace
{
	// 内置包在 qrc 里的位置（见 source/qml/qml.qrc 的 prefix）
	const char kBuiltinDir[] = ":/qml/";
	const char kBuiltinUrl[] = "qrc:/qml/";
	const char kPackFileName[] = "pack.json";
}

StyleResolver& StyleResolver::instance()
{
	static StyleResolver resolver;
	return resolver;
}

StyleResolver::StyleResolver(QObject* parent)
	: QObject(parent)
{
}

void StyleResolver::init(const QString& stylesDir)
{
	m_stylesDir = stylesDir;
	ensureDefaults();
	inspectPack();
	m_urlCache.clear();
}

void StyleResolver::ensureDefaults() const
{
	// (M8) 风格包的释放已归 ThemeManager 管（它的老职责就是"释放样式资源"，以前释放 QSS，现在释放 QML）。
	// 这里只转发，规则与日志都在 ThemeManager::ensureStylePack()。
	ThemeManager::instance().ensureStylePack();
}

void StyleResolver::inspectPack()
{
	m_packPresent = false;
	m_packAccepted = true;
	m_packVersion.clear();
	m_rejectReason.clear();

	if (m_stylesDir.isEmpty())
		return;

	const QString packPath = m_stylesDir + QLatin1Char('/') + QLatin1String(kPackFileName);
	m_packPresent = QFile::exists(packPath);
	if (!m_packPresent)
		return;   // 没有磁盘包：全走内置，不算异常

	QFile file(packPath);
	if (!file.open(QIODevice::ReadOnly)) {
		m_packAccepted = false;
		m_rejectReason = qtTrId("style_pack_unreadable");
		return;
	}

	QJsonParseError parseError{};
	const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
	if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
		m_packAccepted = false;
		m_rejectReason = qtTrId("style_pack_invalid_json");
		return;
	}

	const QJsonObject object = doc.object();
	m_packVersion = object.value(QStringLiteral("packVersion")).toString();

	const int abi = object.value(QStringLiteral("uiAbi")).toInt(-1);
	if (abi != kStylePackUiAbi) {
		m_packAccepted = false;
		m_rejectReason = qtTrId("style_pack_abi_mismatch_fmt").arg(abi).arg(kStylePackUiAbi);
	}

	if (!m_packAccepted)
		qWarning().noquote() << "[Style] 风格包被拒载，整包回退内置：" << m_rejectReason;
}

QString StyleResolver::url(const QString& logicalPath) const
{
	if (logicalPath.isEmpty())
		return QString();

	// QML 侧同一个路径会反复问（每个 Loader 一次），缓存掉文件系统探测
	const auto cached = m_urlCache.constFind(logicalPath);
	if (cached != m_urlCache.constEnd())
		return cached.value();

	QString resolved = builtinUrl(logicalPath);
	if (m_packAccepted && !m_stylesDir.isEmpty()) {
		const QString candidate = m_stylesDir + QLatin1Char('/') + logicalPath;
		if (QFile::exists(candidate))
			resolved = QUrl::fromLocalFile(candidate).toString();
	}

	m_urlCache.insert(logicalPath, resolved);
	return resolved;
}

QString StyleResolver::builtinUrl(const QString& logicalPath)
{
	return QString::fromLatin1(kBuiltinUrl) + logicalPath;
}

bool StyleResolver::reset()
{
	if (m_stylesDir.isEmpty())
		return false;

	// 只删"属于包"的文件：内置树里有的那些 + pack.json。
	// 刻意不删 theme-*.json 与 *.qss —— 它们由 ThemeManager 释放，删了会让旧控件壳掉样式。
	const int prefixLength = static_cast<int>(qstrlen(kBuiltinDir));
	QDirIterator it(QString::fromLatin1(kBuiltinDir), QDir::Files, QDirIterator::Subdirectories);
	while (it.hasNext()) {
		const QString source = it.next();
		const QString target = m_stylesDir + QLatin1Char('/') + source.mid(prefixLength);
		if (QFile::exists(target) && !QFile::remove(target))
			qWarning().noquote() << "[Style] 重置时删不掉：" << target;
	}
	QFile::remove(m_stylesDir + QLatin1Char('/') + QLatin1String(kPackFileName));
	// 释放基线也清掉：否则下次启动会拿旧基线去比，用户改过的文件会被当成"内置没变"而跳过
	QFile::remove(m_stylesDir + QLatin1Char('/') + QStringLiteral("release-manifest.json"));

	ensureDefaults();
	inspectPack();
	m_urlCache.clear();
	emit packChanged();
	return true;
}

void StyleResolver::reload()
{
	inspectPack();
	m_urlCache.clear();
	emit packChanged();
}
