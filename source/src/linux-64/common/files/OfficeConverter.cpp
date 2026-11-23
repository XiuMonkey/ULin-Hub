#include "common/files/OfficeConverter.h"

#include <QFileInfo>
#include <QProcess>

namespace OfficeConverter
{

static QString soffice()
{
	// 允许用环境变量换 soffice 路径：非标准安装、AppImage、测试机都用得上
	const QString fromEnv = QString::fromLocal8Bit(qgetenv("ULINHUB_SOFFICE"));
	return fromEnv.isEmpty() ? QStringLiteral("soffice") : fromEnv;
}

static QStringList conversionArgs(const QString& sourcePath, const QString& pdfPath)
{
	// soffice 只在 --outdir 里生成与源文件同名的 PDF，所以 pdfPath 的目录就是输出目录
	return { QStringLiteral("--headless"), QStringLiteral("--convert-to"), QStringLiteral("pdf"),
		QStringLiteral("--outdir"), QFileInfo(pdfPath).absolutePath(), sourcePath };
}

bool isOfficeDocument(const QString& path)
{
	// 只看后缀；老式 doc / xls / ppt 也算，具体能不能转由 soffice 决定
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

	// 同步路径：最坏阻塞 5s 启动 + 30s 转换，调用方必须能接受这段卡顿
	QProcess proc;
	proc.setProgram(soffice());
	proc.setArguments(conversionArgs(sourcePath, pdfPath));
	proc.start();
	if (!proc.waitForStarted(5000) || !proc.waitForFinished(30000)) {
		if (error)
			*error = QStringLiteral("soffice 未安装或转换超时");
		return false;
	}
	// 只在正常退出且退出码为 0 时算成功；PDF 是否真的生成由调用方查文件
	return proc.exitStatus() == QProcess::NormalExit && proc.exitCode() == 0;
}

QProcess* startConversion(const QString& sourcePath, const QString& pdfPath, QObject* parent)
{
	// 进程挂在 parent 上：调用方接 finished 后 deleteLater，parent 析构时也会一并回收
	auto* proc = new QProcess(parent);
	proc->setProgram(soffice());
	proc->setArguments(conversionArgs(sourcePath, pdfPath));
	proc->start();
	return proc;
}

QString friendlyError(const QString& raw)
{
	// raw 一般是 soffice 的 stderr 原文；空的话说明根本没起来，退回一句安装提示
	return raw.isEmpty()
		? QStringLiteral("文档转换失败（需要安装 LibreOffice）")
		: raw;
}

} // namespace OfficeConverter
