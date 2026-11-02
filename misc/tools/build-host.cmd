@echo off
rem ---------------------------------------------------------------------------
rem build-host.cmd -- build the ULin Hub host (source\ULin Hub.vcxproj)
rem
rem   build-host.cmd                 Release, full build (compile + link)
rem   build-host.cmd Debug           Debug, full build
rem   build-host.cmd Release compile compile only (ClCompile) -- fast syntax check
rem
rem Why a wrapper: the build tool has to be launched by its full path from a shell
rem that the sandbox allows; keeping the invocation in one file means nobody has to
rem remember the argument set. Humans can use it too.
rem
rem NOTE: ASCII-only + CRLF on purpose -- cmd.exe reads this with the local code
rem       page (936 here), so non-ASCII comments would need per-machine encoding.
rem ---------------------------------------------------------------------------
setlocal
set "CONFIG=%~1"
if "%CONFIG%"=="" set "CONFIG=Release"
set "MODE=%~2"

set "MSBUILD=C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe"
if not exist "%MSBUILD%" (
    echo [build-host] MSBUILD not found: "%MSBUILD%"
    exit /b 1
)

set "PROJ=%~dp0..\..\source\ULin Hub.vcxproj"

if /i "%MODE%"=="compile" (
    echo [build-host] compile-only %CONFIG%
    "%MSBUILD%" "%PROJ%" /t:ClCompile /p:Configuration=%CONFIG% /p:Platform=x64 /m /v:m
) else (
    echo [build-host] full build %CONFIG%
    "%MSBUILD%" "%PROJ%" /p:Configuration=%CONFIG% /p:Platform=x64 /m /v:m
)
exit /b %ERRORLEVEL%
