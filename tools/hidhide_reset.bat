@echo off
echo === HidHide Full Reset ===
echo.
echo 1. Disabling cloaking...
"C:\Program Files\Nefarius\HidHideCLI\HidHideCLI.exe" --cloak-off 2>nul

echo 2. Clearing blacklist...
"C:\Program Files\Nefarius\HidHideCLI\HidHideCLI.exe" --dev-clear 2>nul

echo 3. Clearing whitelist...
"C:\Program Files\Nefarius\HidHideCLI\HidHideCLI.exe" --app-clear 2>nul

echo 4. Listing current state...
"C:\Program Files\Nefarius\HidHideCLI\HidHideCLI.exe" --cloak-state 2>nul
echo.

echo If CLI not found, uninstalling HidHide driver...
"C:\Program Files\Nefarius\HidHide\nefconw.exe" --remove-device-node --hardware-id root\HidHide --class-guid {e0cbf06c-cd8b-4647-bb8a-263b43f0f974} 2>nul

echo.
echo Done. Neptune should be visible now.
pause
