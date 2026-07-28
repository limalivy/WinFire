# ============================================================================
# winFire Uninstall Script
# Run as Administrator: powershell -ExecutionPolicy Bypass -File uninstall.ps1
# ============================================================================
#Requires -RunAsAdministrator
$ErrorActionPreference = "Stop"

$InstallDir = "$env:ProgramFiles\FireIME"

Write-Host ""
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "  winFire IME Uninstaller" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

# 1. Unregister TSF
Write-Host "[1/3] Unregistering IME..." -ForegroundColor Yellow
$DllPath = "$InstallDir\fire_tsf.dll"
if (Test-Path $DllPath) {
    Push-Location $InstallDir
    try {
        & regsvr32 /s /u "$DllPath" 2>&1 | Out-Null
        if ($LASTEXITCODE -eq 0) {
            Write-Host "  [OK] IME unregistered" -ForegroundColor Green
        } else {
            & rundll32 "$DllPath",DllUnregisterServer 2>&1 | Out-Null
            Write-Host "  [OK] Attempted rundll32 unregister" -ForegroundColor Yellow
        }
    } finally {
        Pop-Location
    }
} else {
    Write-Host "  [SKIP] fire_tsf.dll not found" -ForegroundColor DarkGray
}

# 2. Remove program files
Write-Host "[2/3] Removing program files..." -ForegroundColor Yellow
if (Test-Path $InstallDir) {
    Remove-Item -Recurse -Force $InstallDir
    Write-Host ("  [OK] Removed " + $InstallDir) -ForegroundColor Green
} else {
    Write-Host "  [SKIP] Directory not found" -ForegroundColor DarkGray
}

# 3. User data
Write-Host "[3/3] User data..." -ForegroundColor Yellow
$ConfigDir = "$env:APPDATA\FireIME"
if (Test-Path $ConfigDir) {
    $answer = Read-Host "  Keep user data at $ConfigDir? [Y/n]"
    if ($answer -eq 'n' -or $answer -eq 'N') {
        Remove-Item -Recurse -Force $ConfigDir
        Write-Host "  [OK] User data removed" -ForegroundColor Green
    } else {
        Write-Host ("  [KEEP] " + $ConfigDir) -ForegroundColor Yellow
    }
}

Write-Host ""
Write-Host "  Uninstall complete!" -ForegroundColor Green
Write-Host ""
