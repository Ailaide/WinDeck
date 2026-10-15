# Disable HidHide filtering to make the Neptune controller visible again
Write-Host "Attempting to disable HidHide..."
& "C:\Program Files\Nefarius\HidHideCLI\HidHideCLI.exe" --cloak-off 2>$null
if ($LASTEXITCODE -eq 0) {
    Write-Host "HidHide cloaking disabled via CLI"
} else {
    Write-Host "HidHideCLI not found. Stopping the HidHide service instead..."
    Stop-Service HidHide -ErrorAction SilentlyContinue
    Write-Host "HidHide service stopped"
}
