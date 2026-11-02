#include "common/files/OfficeConverter.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStringList>

namespace
{
QString psQuote(const QString& value)
{
	QString escaped = value;
	escaped.replace(QLatin1Char('\''), QStringLiteral("''"));
	return QLatin1Char('\'') + escaped + QLatin1Char('\'');
}
QString friendlyComError(const QString& raw)
{
	const QString lower = raw.toLower();
	if (lower.contains(QStringLiteral("clsid"))
		|| lower.contains(QStringLiteral("class not registered"))
		|| lower.contains(QStringLiteral("80040154"))
		|| lower.contains(QStringLiteral("8002802b"))
		|| lower.contains(QStringLiteral("elementnotfound"))
		|| lower.contains(QStringLiteral("0x800"))
		|| raw.isEmpty())
		return QStringLiteral("未检测到可用的 Office/WPS。安装 LibreOffice 并启用 libreoffice-office 扩展后可显示原版排版");
	return raw.left(160);
}
} // namespace

namespace OfficeConverter
{
bool isOfficeDocument(const QString& path)
{
	const QString suffix = QFileInfo(path).suffix().toLower();
	return suffix == QLatin1String("docx") || suffix == QLatin1String("doc")
		|| suffix == QLatin1String("xlsx") || suffix == QLatin1String("xls")
		|| suffix == QLatin1String("pptx") || suffix == QLatin1String("ppt");
}

static QString makeScript(const QString& sourcePath, const QString& pdfPath)
{
	return QStringLiteral(R"PS(
$ErrorActionPreference = 'Stop'
$src = %1
$dst = %2
$ext = [IO.Path]::GetExtension($src).ToLowerInvariant()
$lastError = 'No usable Office/WPS COM automation'

function Close-App($app) {
    if ($null -ne $app) {
        try { $app.Quit() } catch { }
    }
}

try {
    if ($ext -in @('.docx', '.doc', '.rtf', '.odt')) {
        foreach ($prog in @('Word.Application', 'Kwps.Application')) {
            $app = $null
            try {
                $app = New-Object -ComObject $prog
                $app.Visible = $false
                $app.DisplayAlerts = 0
                $doc = $app.Documents.Open($src, $false, $true)
                $doc.ExportAsFixedFormat($dst, 17)
                $doc.Close($false)
                Close-App $app
                exit 0
            } catch {
                $lastError = $_.Exception.Message
                Close-App $app
            }
        }
    }
    elseif ($ext -in @('.xlsx', '.xls', '.ods')) {
        foreach ($prog in @('Excel.Application', 'Ket.Application')) {
            $app = $null
            try {
                $app = New-Object -ComObject $prog
                $app.Visible = $false
                $app.DisplayAlerts = $false
                $wb = $app.Workbooks.Open($src, 0, $true)
                $wb.ExportAsFixedFormat(0, $dst)
                $wb.Close($false)
                Close-App $app
                exit 0
            } catch {
                $lastError = $_.Exception.Message
                Close-App $app
            }
        }
    }
    elseif ($ext -in @('.pptx', '.ppt', '.odp')) {
        foreach ($prog in @('PowerPoint.Application', 'Kwpp.Application')) {
            $app = $null
            try {
                $app = New-Object -ComObject $prog
                $pres = $app.Presentations.Open($src, $true, $false, $false)
                $pres.SaveAs($dst, 32)
                $pres.Close()
                Close-App $app
                exit 0
            } catch {
                $lastError = $_.Exception.Message
                Close-App $app
            }
        }
    }
    else {
        $lastError = 'Unsupported Office extension'
    }
} catch {
    $lastError = $_.Exception.Message
}

[Console]::Error.WriteLine($lastError)
exit 1
)PS").arg(
		psQuote(QDir::toNativeSeparators(sourcePath)),
		psQuote(QDir::toNativeSeparators(pdfPath)));
}

bool convertToPdf(const QString& sourcePath, const QString& pdfPath, QString* error)
{
	if (error)
		error->clear();
	if (!QFileInfo::exists(sourcePath)) {
		if (error)
			*error = QStringLiteral("Office conversion failed");
		return false;
	}

	const QString script = makeScript(sourcePath, pdfPath);

	QProcess proc;
	proc.setProgram(QStringLiteral("powershell.exe"));
	proc.setArguments({ QStringLiteral("-NoProfile"), QStringLiteral("-NonInteractive"),
		QStringLiteral("-ExecutionPolicy"), QStringLiteral("Bypass"),
		QStringLiteral("-Command"), script });
	proc.start();
	if (!proc.waitForStarted(5000)) {
		if (error)
			*error = QStringLiteral("Office conversion failed");
		return false;
	}
	if (!proc.waitForFinished(30000)) {
		proc.kill();
		if (error)
			*error = QStringLiteral("Office conversion failed");
		return false;
	}
	if (proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0) {
		QString detail = QString::fromLocal8Bit(proc.readAllStandardError()).trimmed();
		if (detail.isEmpty())
			detail = QString::fromLocal8Bit(proc.readAllStandardOutput()).trimmed();
		if (error)
			*error = friendlyComError(detail);
		return false;
	}
	if (!QFileInfo::exists(pdfPath) || QFileInfo(pdfPath).size() == 0) {
		if (error)
			*error = QStringLiteral("Office conversion failed");
		return false;
	}
	return true;
}
QString friendlyError(const QString& raw)
{
	return friendlyComError(raw);
}

QProcess* startConversion(const QString& sourcePath, const QString& pdfPath, QObject* parent)
{
	QProcess* proc = new QProcess(parent);
	proc->setProgram(QStringLiteral("powershell.exe"));
	proc->setArguments({ QStringLiteral("-NoProfile"), QStringLiteral("-NonInteractive"),
		QStringLiteral("-ExecutionPolicy"), QStringLiteral("Bypass"),
		QStringLiteral("-Command"), makeScript(sourcePath, pdfPath) });
	proc->start();
	return proc;
}

} // namespace OfficeConverter
