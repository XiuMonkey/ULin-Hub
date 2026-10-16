# VT 核心自检一键跑（cl 编译 libvterm + TerminalVt + selfcheck）—— 覆盖范围见同目录 README.txt。
#
# ⚠️ 本文件必须存成 **UTF-8 带 BOM**（与 misc\tools\*.ps1 一致）：Windows PowerShell 5.1 读无 BOM 的
#    .ps1 会按 ANSI/GBK 解，中文注释会把字符串引号吃坏、脚本直接解析失败。
#
# 用法：
#   powershell -NoProfile -ExecutionPolicy Bypass -File misc\tools\vt-selfcheck\run.ps1
# 可选参数（默认值 = 本机现用工具链）：
#   -VsMsvcDir   MSVC 工具集目录（默认 VS18 Community 的 14.51.36231）
#   -SdkDir      Windows SDK 根（默认 C:\Program Files (x86)\Windows Kits\10）
#   -SdkVersion  Windows SDK 版本（默认 10.0.26100.0）
#
# 产物全部落在 <仓库>\build\vt-selfcheck\（build/ 已被 .gitignore 忽略），不脏 source/。
# 报告写到 <仓库>\build\vt-selfcheck\selfcheck.log（UTF-8）——刻意不走控制台：Windows 控制台默认
# 是 GBK，抓 stdout 会把报告里的中文弄成乱码。
#
# 这个自检**不链 Qt**：TerminalVt 是纯 C++（输入字节、输出格子），所以只需要 MSVC 与 Windows SDK。

param(
	[string]$VsMsvcDir = 'C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\MSVC\14.51.36231',
	[string]$SdkDir = 'C:\Program Files (x86)\Windows Kits\10',
	[string]$SdkVersion = '10.0.26100.0'
)

$ErrorActionPreference = 'Stop'

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path   # misc\tools\vt-selfcheck -> 仓库根
$src = Join-Path $repoRoot 'source'
$work = Join-Path $repoRoot 'build\vt-selfcheck'
New-Item -ItemType Directory -Force $work | Out-Null

# ---- 1) 编译器环境：cl 的 INCLUDE/LIB 靠环境变量（不调 vcvarsall，免得依赖 VS 的批处理布局）----
$env:INCLUDE = "$VsMsvcDir\include;$SdkDir\Include\$SdkVersion\ucrt;$SdkDir\Include\$SdkVersion\shared;$SdkDir\Include\$SdkVersion\um;$SdkDir\Include\$SdkVersion\winrt;$SdkDir\Include\$SdkVersion\cppwinrt"
$env:LIB = "$VsMsvcDir\lib\x64;$SdkDir\Lib\$SdkVersion\ucrt\x64;$SdkDir\Lib\$SdkVersion\um\x64"
$env:PATH = "$VsMsvcDir\bin\Hostx64\x64;$SdkDir\bin\$SdkVersion\x64;$env:PATH"

# 传给 cl 的路径统一用正斜杠：路径里有空格（"ULin Hub"）时 PowerShell 会给参数加引号，
# 而"结尾是反斜杠"会被 C 运行时当成转义引号（经典坑）；正斜杠结尾没这个问题，cl 也认。
$incFwd = (Join-Path $src 'include') -replace '\\', '/'
$vtermFwd = (Join-Path $src '3rdparty\libvterm\include') -replace '\\', '/'
$workFwd = $work -replace '\\', '/'

# ---- 2) libvterm：C99，单独一轮 /TC 编成 obj。绝不能和 C++ 的 /std:c++17 /permissive- 混在一轮里编。----
Write-Host "== libvterm (C) =="
$libObjs = @()
foreach ($name in @('encoding', 'keyboard', 'mouse', 'parser', 'pen', 'screen', 'state', 'unicode', 'vterm')) {
	$obj = Join-Path $work "$name.obj"
	# /I 不能少：src/vterm_internal.h 里 include 的 "vterm.h" 在 include/ 下
	# 开关对齐 .vcxproj 里这 9 条 <ClCompile> 的元数据：/W3 + /sdl- + _CRT_SECURE_NO_WARNINGS。
	# 带 /sdl- 是关键：/sdl 会把 parser.c 的 C4996(strncpy) 与 C4703(string_start) 抬成**错误**，
	# 而 /WX- 撤不掉这个升级 —— 少了这个 -，自检会"过"，真实构建却挂。
	& cl.exe /nologo /c /TC /W3 /sdl- /MD /O2 /D_CRT_SECURE_NO_WARNINGS "/I$vtermFwd" "/Fo$obj" (Join-Path $src "3rdparty\libvterm\src\$name.c")
	if ($LASTEXITCODE -ne 0) { Write-Host "libvterm/$name.c 编译失败（exit=$LASTEXITCODE）"; exit $LASTEXITCODE }
	$libObjs += $obj
}

# ---- 3) 自检 + VT 核心（/utf-8 不能省：源码里有中文注释且没有 BOM）----
Write-Host "== selfcheck =="
& cl.exe /nologo /std:c++17 /Zc:__cplusplus /permissive- /utf-8 /EHsc /MD /O2 /W3 /sdl `
	"/I$incFwd" "/I$vtermFwd" "/Fo$workFwd/" "/Fe$workFwd/vt-selfcheck.exe" `
	(Join-Path $PSScriptRoot 'selfcheck.cpp') `
	(Join-Path $src 'src\common\terminal\TerminalVt.cpp') `
	$libObjs
if ($LASTEXITCODE -ne 0) { Write-Host "自检编译/链接失败（exit=$LASTEXITCODE）"; exit $LASTEXITCODE }

# ---- 4) 跑（报告直接落盘，UTF-8）----
Write-Host "== vt-selfcheck.exe =="
$log = Join-Path $work 'selfcheck.log'
$code = 1
Push-Location $PSScriptRoot
try {
	& (Join-Path $work 'vt-selfcheck.exe') `
		(Join-Path $PSScriptRoot 'fixture\stream.bin') `
		(Join-Path $PSScriptRoot 'fixture\expected.txt') `
		$log
	$code = $LASTEXITCODE
} finally {
	Pop-Location
}
Write-Host "报告：$log（exit=$code）"
exit $code
