@echo off
setlocal EnableExtensions

set "PAUSE_ON_EXIT=1"
if /I "%~1"=="-nopause" set "PAUSE_ON_EXIT=0"

for %%I in ("%~dp0..") do set "REPO_ROOT=%%~fI"
set "EXIT_CODE=1"

pushd "%REPO_ROOT%" >nul
if errorlevel 1 (
    echo Failed to open repo root: "%REPO_ROOT%"
    goto :finish
)

set "CMAKE_EXE=C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
set "MSBUILD_EXE=C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe"

if not exist "%CMAKE_EXE%" set "CMAKE_EXE=cmake"
if not exist "%MSBUILD_EXE%" set "MSBUILD_EXE=msbuild"

echo [1/3] Configure Release...
"%CMAKE_EXE%" -S . -B build/release
if errorlevel 1 (
    echo Configure failed.
    goto :finish
)

echo [2/3] Build and auto-deploy windeck...
"%MSBUILD_EXE%" build\release\WinDeck.sln /t:windeck /p:Configuration=Release /p:Platform=x64 /m /nologo /verbosity:minimal
if errorlevel 1 (
    echo Build failed.
    goto :finish
)

set "LOCAL_EXE=%REPO_ROOT%\build\release\Release\windeck.exe"
set "REMOTE_EXE=\\STEAMDECK\Shared\windeck.exe"

if not exist "%LOCAL_EXE%" (
    echo Local build output not found: "%LOCAL_EXE%"
    goto :finish
)

if not exist "%REMOTE_EXE%" (
    echo Remote deploy output not found: "%REMOTE_EXE%"
    goto :finish
)

echo [3/3] Verify deployed binary...
powershell -NoProfile -ExecutionPolicy Bypass -Command ^
    "$local = $env:LOCAL_EXE;" ^
    "$remote = $env:REMOTE_EXE;" ^
    "$l = Get-Item -LiteralPath $local;" ^
    "$r = Get-Item -LiteralPath $remote;" ^
    "$lh = (Get-FileHash -LiteralPath $local -Algorithm SHA256).Hash;" ^
    "$rh = (Get-FileHash -LiteralPath $remote -Algorithm SHA256).Hash;" ^
    "Write-Host ('LOCAL :  ' + $l.Length + ' bytes, ' + $l.LastWriteTime.ToString('yyyy-MM-dd HH:mm:ss'));" ^
    "Write-Host ('REMOTE:  ' + $r.Length + ' bytes, ' + $r.LastWriteTime.ToString('yyyy-MM-dd HH:mm:ss'));" ^
    "Write-Host ('SHA256 LOCAL :  ' + $lh);" ^
    "Write-Host ('SHA256 REMOTE:  ' + $rh);" ^
    "if ($lh -ne $rh) { exit 2 }"

if errorlevel 2 (
    echo Deploy verification failed: hash mismatch.
    goto :finish
)

if errorlevel 1 (
    echo Deploy verification failed.
    goto :finish
)

echo Deploy completed successfully.
set "EXIT_CODE=0"

:finish
if defined REPO_ROOT (
    popd >nul
)
if "%PAUSE_ON_EXIT%"=="1" pause
exit /b %EXIT_CODE%
