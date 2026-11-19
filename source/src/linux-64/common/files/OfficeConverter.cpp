#include "common/files/OfficeConverter.h"

#include <QFileInfo>
#include <QProcess>

namespace OfficeConverter
{

static QString soffice()
{
	const QString fromEnv = QString::fromLocal8Bit(qgetenv("ULINHUB_SOFFICE"));
	return fromEnv.isEmpty() ? QStringLiteral("soffice") : fromEnv;
}

static QStringList conversionArgs(const QString& sourcePath, const QString& pdfPath)
{
	return { QStringLiteral("--headless"), QStringLiteral("--convert-to"), QStringLiteral("pdf"),
		QStringLiteral("--outdir"), QFileInfo(pdfPath).absolutePath(), sourcePath };
}

bool isOfficeDocument(const QString& path)
{
	const QString s = QFileInfo(path).suffix().toLower();
	return s == QLatin1String("doc") || s == QLatin1String("docx")
		|| s == QLatin1String("xls") || s == QLatin1String("xlsx")
		|| s == QLatin1String("ppt") || s == QLatin1String("pptx");
}

bool convertToPdf(const QString& sourcePath, const QString& pdfPath, QString* error)
{
	if (error)
		error->clear();
	if (!QFileInfo::exists(sourcePath)) {
		if (error)
			*error = QStringLiteral("源文件不存在");
		return false;
	}

	QProcess proc;
	proc.setProgram(soffice());
	proc.setArguments(conversionArgs(sourcePath, pdfPath));
	proc.start();
	if (!proc.waitForStarted(5000) || !proc.waitForFinished(30000)) {
		if (error)
			*error = QStringLiteral("soffice 未安装或转换超时");
		return false;
	}
	return proc.exitStatus() == QProcess::NormalExit && proc.exitCode() == 0;
}

QProcess* startConversion(const QString& sourcePath, const QString& pdfPath, QObject* parent)
{
	auto* proc = new QProcess(parent);
	proc->setProgram(soffice());
	proc->setArguments(conversionArgs(sourcePath, pdfPath));
	proc->start();
	return proc;
}

QString friendlyError(const QString& raw)
{
	return raw.isEmpty()
		? QStringLiteral("文档转换失败（需要安装 LibreOffice）")
		: raw;
}

} // namespace OfficeConverter
