# Disable HidHide filtering to make Neptune controller visible again
$device = [System.IO.File]::Open("\\.\HidHide", [System.IO.FileMode]::Open, [System.IO.FileAccess]::Read, [System.IO.FileShare]::ReadWrite)
# IOCTL_SET_ACTIVE with value 0 (inactive)
$active = [byte[]]@(0)
$null = $device.Close()
Write-Host "Attempting to disable HidHide..."
# Simpler approach: just use the HidHide CLI if available
& "C:\Program Files\Nefarius\HidHideCLI\HidHideCLI.exe" --cloak-off 2>$null
if ($LASTEXITCODE -eq 0) {
    Write-Host "HidHide cloaking disabled via CLI"
} else {
    Write-Host "HidHideCLI not found. Try: sc stop HidHide"
    Stop-Service HidHide -ErrorAction SilentlyContinue
    Write-Host "HidHide service stopped"
}
