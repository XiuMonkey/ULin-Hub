# C++ 自检一键跑（moc + cl + 链接 Qt6Core）—— 覆盖范围与设计理由见同目录 README.txt。
#
# ⚠️ 本文件必须存成 **UTF-8 带 BOM**（与 misc\tools\*.ps1 一致）：Windows PowerShell 5.1 读无 BOM 的
#    .ps1 会按 ANSI/GBK 解，中文注释会把字符串引号吃坏、脚本直接解析失败。
#
# 用法（Windows PowerShell 5.1 或 PowerShell 7 都行）：
#   powershell -NoProfile -ExecutionPolicy Bypass -File misc\tools\cpp-selfcheck\run.ps1
# 可选参数（默认值 = 本机现用工具链）：
#   -QtDir       Qt 安装目录（默认 D:\Qt\6.11.2\msvc2022_64）
#   -VsMsvcDir   MSVC 工具集目录（默认 VS18 Community 的 14.51.36231）
#   -SdkDir      Windows SDK 根（默认 C:\Program Files (x86)\Windows Kits\10）
#   -SdkVersion  Windows SDK 版本（默认 10.0.26100.0）
#
# 产物全部落在 <仓库>\build\cpp-selfcheck\（build/ 已在 .gitignore 里），不脏 source/。
# 输出：逐条 PASS/FAIL + 末尾汇总；全 PASS 退出码 0，有 FAIL 退出码 1。
#
# 为什么这么跑（与 .vcxproj 无关的第二条腿）：自检要证明"搬进 common/ 的逻辑能脱离 QML 引擎单测"，
# 所以这里**不**链接整个工程，只把被测 .cpp + 生成的 moc 编成一个小 exe，链接 Qt6Core 就够。

param(
	[string]$QtDir = 'D:\Qt\6.11.2\msvc2022_64',
	[string]$VsMsvcDir = 'C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\MSVC\14.51.36231',
	[string]$SdkDir = 'C:\Program Files (x86)\Windows Kits\10',
	[string]$SdkVersion = '10.0.26100.0'
)

$ErrorActionPreference = 'Stop'

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path   # misc\tools\cpp-selfcheck -> 仓库根
$inc = Join-Path $repoRoot 'source\include'
$work = Join-Path $repoRoot 'build\cpp-selfcheck'
New-Item -ItemType Directory -Force $work | Out-Null

# ---- 1) 编译器环境：cl 的 INCLUDE/LIB 靠环境变量（不调 vcvarsall，免得依赖 VS 的批处理布局）----
$env:INCLUDE = "$VsMsvcDir\include;$SdkDir\Include\$SdkVersion\ucrt;$SdkDir\Include\$SdkVersion\shared;$SdkDir\Include\$SdkVersion\um;$SdkDir\Include\$SdkVersion\winrt;$SdkDir\Include\$SdkVersion\cppwinrt"
$env:LIB = "$VsMsvcDir\lib\x64;$SdkDir\Lib\$SdkVersion\ucrt\x64;$SdkDir\Lib\$SdkVersion\um\x64"
$env:PATH = "$VsMsvcDir\bin\Hostx64\x64;$SdkDir\bin\$SdkVersion\x64;$QtDir\bin;$env:PATH"

$moc = Join-Path $QtDir 'bin\moc.exe'

# 传给 cl 的路径统一用正斜杠：路径里有空格（"ULin Hub"）时 PowerShell 会给参数加引号，
# 而"结尾是反斜杠"会被 C 运行时当成转义引号（经典坑）；正斜杠结尾没这个问题，cl 也认。
$incFwd = $inc -replace '\\', '/'
$workFwd = $work -replace '\\', '/'
$qtFwd = $QtDir -replace '\\', '/'

# ---- 2) moc：两个被测类都有 Q_OBJECT（QML 单例/类型注册要元对象）----
Write-Host "== moc =="
& $moc (Join-Path $inc 'common\shell\PanelDockModel.h') -o (Join-Path $work 'moc_PanelDockModel.cpp')
if ($LASTEXITCODE -ne 0) { Write-Host "moc 失败：PanelDockModel.h（exit=$LASTEXITCODE）"; exit $LASTEXITCODE }
& $moc (Join-Path $inc 'qmlbridge\PanelBarLayout.h') -o (Join-Path $work 'moc_PanelBarLayout.cpp')
if ($LASTEXITCODE -ne 0) { Write-Host "moc 失败：PanelBarLayout.h（exit=$LASTEXITCODE）"; exit $LASTEXITCODE }

# ---- 3) cl：自检 + 被测 .cpp + 生成的 moc（/utf-8 必须留着：源文件里有中文注释且无 BOM）----
Write-Host "== cl =="
& cl.exe /nologo /std:c++17 /Zc:__cplusplus /permissive- /utf-8 /EHsc /MD /O2 `
	"/I$incFwd" "/I$qtFwd/include" "/I$qtFwd/include/QtCore" `
	"/Fo$workFwd/" "/Fe$workFwd/selfcheck.exe" `
	(Join-Path $PSScriptRoot 'selfcheck.cpp') `
	(Join-Path $repoRoot 'source\src\common\util\UrlInput.cpp') `
	(Join-Path $repoRoot 'source\src\common\shell\PanelDockModel.cpp') `
	(Join-Path $repoRoot 'source\src\common\layout\PanelBarMetrics.cpp') `
	(Join-Path $repoRoot 'source\src\qmlbridge\PanelBarLayout.cpp') `
	(Join-Path $work 'moc_PanelDockModel.cpp') `
	(Join-Path $work 'moc_PanelBarLayout.cpp') `
	/link "/LIBPATH:$qtFwd/lib" Qt6Core.lib
if ($LASTEXITCODE -ne 0) { Write-Host "cl 编译/链接失败（exit=$LASTEXITCODE）"; exit $LASTEXITCODE }

# ---- 4) 跑（PATH 里已带 Qt 的 bin，Qt6Core.dll 能找到）----
Write-Host "== selfcheck.exe =="
& (Join-Path $work 'selfcheck.exe')
exit $LASTEXITCODE
