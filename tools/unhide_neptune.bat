@echo off
echo Disabling HidHide cloaking...
"C:\Program Files\Nefarius\HidHideCLI\HidHideCLI.exe" --cloak-off 2>nul
if %errorlevel%==0 (
    echo HidHide cloaking disabled.
) else (
    echo HidHideCLI not found, stopping HidHide service...
    net stop HidHide
)
echo.
echo Neptune controller should be visible again.
pause
