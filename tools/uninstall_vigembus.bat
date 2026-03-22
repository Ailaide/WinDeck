@echo off
echo Uninstalling ViGEmBus driver...
echo.

:: Try standard uninstall via registry
for /f "tokens=*" %%i in ('reg query "HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall" /s /f "ViGEm" 2^>nul ^| findstr "UninstallString"') do (
    echo Found: %%i
    for /f "tokens=2*" %%a in ("%%i") do (
        echo Running: %%b
        %%b
    )
)

:: Also try via pnputil (driver store)
echo.
echo Searching for ViGEmBus in driver store...
for /f "tokens=1" %%d in ('pnputil /enum-drivers ^| findstr /B "Published Name" ^| findstr /i "oem"') do (
    pnputil /enum-drivers | findstr /A /C:"%%d" /C:"ViGEm" >nul 2>&1
)

pnputil /enum-drivers | findstr -i "vigem nefarius" >nul 2>&1
if %errorlevel%==0 (
    echo Found ViGEmBus in driver store, removing...
    for /f "tokens=3" %%d in ('pnputil /enum-drivers ^| findstr /B /C:"Published Name"') do (
        pnputil /enum-drivers | findstr /C:"%%d" | findstr /i "vigem" >nul 2>&1
        if not errorlevel 1 (
            echo Removing driver: %%d
            pnputil /delete-driver %%d /uninstall /force
        )
    )
) else (
    echo ViGEmBus not found in driver store
)

:: Delete downloaded installer from temp
del /f /q "%TEMP%\ViGEmBus_Setup.exe" 2>nul

echo.
echo Done. You may need to reboot.
pause
