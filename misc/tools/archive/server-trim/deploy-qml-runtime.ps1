<#
  run-qml-shell.ps1 —— 开发机上启动 QML 壳（ULin Hub.exe）。

  为什么需要它：QML 壳要 Qt 的 QML 模块（<Qt>\qml 下的 QtQuick 等），而本工程的运行库走
  「私有程序集」部署（exe 同目录 dependence\，见 source/dependence.manifest），exe 同目录并没有
  qml\ 目录。发行版需要把那份目录部署进去（windeployqt --qmldir 或手工拷贝 —— 见
  docs/ui-qml-migration-plan.md §7-R9）；开发期用本脚本把 QML_IMPORT_PATH 指到 Qt 安装目录即可。

  用法（仓库根目录）：
    .\misc\tools\run-qml-shell.ps1                  # 默认重放风格包后启动 QML 壳
    .\misc\tools\run-qml-shell.ps1 -KeepPack        # 保留 <exe>\styles（保住本地改过的 theme/metrics）
    .\misc\tools\run-qml-shell.ps1 -Widgets         # 反过来启动旧控件壳（对照用）
    .\misc\tools\run-qml-shell.ps1 -DryRun          # 只打印将要执行的命令
#>
[CmdletBinding()]
param(
	[ValidateSet('Debug', 'Release')]
	[string]$Config = 'Release',

	# 走旧控件壳（对照/回退用）
	[switch]$Widgets,

	# 保留 <exe>\styles：默认是**删掉重放**，因为磁盘包会遮蔽内置包 —— 改了 source/qml 下的 QML
	# 而不重放，等于改动没生效（这个坑踩过一次）。只有想保住本地 theme-*.json / metrics.json 改动时才加。
	[switch]$KeepPack,

	# 不启动，只打印
	[switch]$DryRun
)

$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$exe = Join-Path $root "x64\$Config\ULin Hub.exe"
if (-not (Test-Path -LiteralPath $exe)) { throw "找不到 exe：$exe（先在 VS 或 CMake 里构建 $Config）" }

# Qt 安装目录：优先 QTDIR（与 release-translations.ps1 / CMakePresets 的约定一致）
$qtDir = $env:QTDIR
if (-not $qtDir) {
	$qtDir = 'D:\Qt\6.11.2\msvc2022_64'
	Write-Host "[warn] 未设置 QTDIR，回退到 $qtDir"
}
$qmlDir = Join-Path $qtDir 'qml'
if (-not (Test-Path -LiteralPath $qmlDir)) { throw "找不到 Qt 的 qml 目录：$qmlDir（检查 QTDIR）" }

$arguments = @()
if (-not $Widgets) { $arguments += '--qml' }

Write-Host "exe       : $exe"
Write-Host "参数      : $($arguments -join ' ')"
Write-Host "QML 模块  : $qmlDir"

if ($DryRun) { return }

if (-not $KeepPack) {
	$stylesDir = Join-Path (Split-Path -Parent $exe) 'styles'
	if (Test-Path -LiteralPath $stylesDir) {
		Remove-Item -LiteralPath $stylesDir -Recurse -Force
		Write-Host "已删除旧风格包：$stylesDir（下次启动会重新释放内置包；要保包请加 -KeepPack）"
	}
}

# 只影响本进程环境：QML_IMPORT_PATH 是 Qt 6 的 QML 模块搜索路径
$env:QML_IMPORT_PATH = $qmlDir
Start-Process -FilePath $exe -ArgumentList $arguments -WorkingDirectory (Split-Path -Parent $exe) | Out-Null
Write-Host '已启动（日志见 x64\<Config>\logs\）。'