# ============================================================================
# WinFire 紧急清理脚本 — 清除当前系统上所有残留的输入法注册
# 必须以管理员身份运行：
#   powershell -ExecutionPolicy Bypass -File cleanup_now.ps1
# ============================================================================
#Requires -RunAsAdministrator
$ErrorActionPreference = "Stop"

Write-Host ""
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "  WinFire Cleanup" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

# WinFire 基 GUID 哨兵：CLSID 的 Data4 前 5 字节 + Data1/Data2/Data3
# {8E9F0B21-3C4D-4E5A-9B7C-1F2A-3B__-____-____}
$clsidPrefix = "8E9F0B21-3C4D-4E5A-9B7C-1F2A3B"
$installDir   = "$env:ProgramFiles\WinFire"

# ---- 1. regsvr32 /u 所有已安装的 DLL ----
Write-Host "[1/5] Unregistering TSF DLLs..." -ForegroundColor Yellow
if (Test-Path $installDir) {
    Get-ChildItem -Path $installDir -Filter "fire_tsf*.dll" -ErrorAction SilentlyContinue | ForEach-Object {
        Write-Host "  Unregister: $($_.Name)"
        $result = & regsvr32 /s /u $_.FullName 2>&1
        if ($LASTEXITCODE -ne 0) {
            Write-Host "    regsvr32 failed, trying rundll32..." -ForegroundColor DarkGray
            & rundll32 "$($_.FullName)",DllUnregisterServer 2>&1 | Out-Null
        }
    }
} else {
    Write-Host "  (install dir not found)" -ForegroundColor DarkGray
}

# ---- 2. 清理 COM CLSID 注册 (HKCR\CLSID) ----
Write-Host "[2/5] Cleaning COM CLSID keys..." -ForegroundColor Yellow
Get-ChildItem "HKLM:\SOFTWARE\Classes\CLSID" -ErrorAction SilentlyContinue | ForEach-Object {
    $name = $_.PSChildName
    if ($name -like "{$clsidPrefix*") {
        Write-Host "  Remove: HKCR\CLSID\$name"
        Remove-Item $_.PSPath -Recurse -Force
    }
}

# ---- 3. 清理 CTF TIP 注册 (HKLM) ----
Write-Host "[3/5] Cleaning CTF TIP (HKLM)..." -ForegroundColor Yellow
$tipKey = "HKLM:\SOFTWARE\Microsoft\CTF\TIP"
if (Test-Path $tipKey) {
    Get-ChildItem $tipKey -ErrorAction SilentlyContinue | ForEach-Object {
        $name = $_.PSChildName
        if ($name -like "{$clsidPrefix*") {
            Write-Host "  Remove: HKLM\CTF\TIP\$name"
            Remove-Item $_.PSPath -Recurse -Force
        }
    }
}

# ---- 4. 清理 CTF TIP 注册 (HKCU) ----
Write-Host "[4/5] Cleaning CTF TIP (HKCU)..." -ForegroundColor Yellow
$hklmTipKey = "HKCU:\SOFTWARE\Microsoft\CTF\TIP"
if (Test-Path $hklmTipKey) {
    Get-ChildItem $hklmTipKey -ErrorAction SilentlyContinue | ForEach-Object {
        $name = $_.PSChildName
        if ($name -like "{$clsidPrefix*") {
            Write-Host "  Remove: HKCU\CTF\TIP\$name"
            Remove-Item $_.PSPath -Recurse -Force
        }
    }
}

# ---- 5. 清理其他残留 ----
Write-Host "[5/5] Cleaning misc..." -ForegroundColor Yellow

# HKLM\Software\WinFire
$winfireReg = "HKLM:\Software\WinFire"
if (Test-Path $winfireReg) {
    Remove-Item $winfireReg -Recurse -Force
    Write-Host "  Remove: HKLM\Software\WinFire"
}

# 结束常驻后台查字进程（改常驻后不再空闲自退，必须主动杀以释放映像占用）。
& taskkill /F /IM fire_dictd.exe 2>&1 | Out-Null

# 清理 dictd 自启动项（install.ps1 写 HKLM\Run；Inno 安装包写 HKCU\Run；两处都清保证干净）。
foreach ($hive in @("HKLM:\Software\Microsoft\Windows\CurrentVersion\Run",
                    "HKCU:\Software\Microsoft\Windows\CurrentVersion\Run")) {
    if (Get-ItemProperty -Path $hive -Name "WinFireDictd" -ErrorAction SilentlyContinue) {
        Remove-ItemProperty -Path $hive -Name "WinFireDictd" -Force
        Write-Host "  Remove: $hive\WinFireDictd"
    }
}

# Program Files 中的 DLL 文件
if (Test-Path $installDir) {
    Get-ChildItem -Path $installDir -Filter "fire_tsf*.dll" -ErrorAction SilentlyContinue | ForEach-Object {
        try {
            Remove-Item $_.FullName -Force -ErrorAction Stop
            Write-Host "  Deleted: $($_.Name)"
        } catch {
            # 被占用 → 标记重启删除
            $sig = '[DllImport("kernel32.dll", CharSet=CharSet.Unicode)] public static extern bool MoveFileEx(string a, string b, int f);'
            $mf = Add-Type -MemberDefinition $sig -Name MoveFileExNative -Namespace WinFireCleanup -PassThru
            $mf::MoveFileEx($_.FullName, $null, 4) | Out-Null
            Write-Host "  Defer reboot: $($_.Name)" -ForegroundColor DarkGray
        }
    }
    # 删除空目录（被 defer 的文件仍占位，目录非空跳过不报错）
    $remaining = Get-ChildItem $installDir -ErrorAction SilentlyContinue
    if (-not $remaining) {
        Remove-Item $installDir -Force -ErrorAction SilentlyContinue
        if (Test-Path $installDir) {
            Write-Host "  (directory kept, files pending reboot)" -ForegroundColor DarkGray
        } else {
            Write-Host "  Removed: $installDir"
        }
    }
}

Write-Host ""
Write-Host "========================================" -ForegroundColor Green
Write-Host "  Cleanup complete!" -ForegroundColor Green
Write-Host "========================================" -ForegroundColor Green
Write-Host ""

# ---- 清理 PendingFileRenameOperations 残留 ----
# DeleteOrDeferDll 的 MoveFileEx 会在 PFR 中写入重启删除条目，
# 文件若已被其他方式删除则该条目永远残留，阻塞后续安装。
$pfrKey = "HKLM:\SYSTEM\CurrentControlSet\Control\Session Manager"
$pfrValue = (Get-ItemProperty $pfrKey -EA SilentlyContinue).PendingFileRenameOperations
if ($pfrValue) {
    $newValue = @(); $skip = $false
    foreach ($entry in $pfrValue) {
        if ($entry -match "WinFire") { $skip = $true; continue }
        if ($skip) { $skip = $false; continue }
        $newValue += $entry
    }
    if ($newValue.Count -ne $pfrValue.Count) {
        Set-ItemProperty $pfrKey -Name PendingFileRenameOperations -Value $newValue
        Write-Host "  [CLEAN] PendingFileRenameOperations" -ForegroundColor Green
    }
}

# ---- 验证 ----
Write-Host "Verification..." -ForegroundColor Yellow
$left = @()
Get-ChildItem "HKLM:\SOFTWARE\Classes\CLSID" -ErrorAction SilentlyContinue | ForEach-Object {
    if ($_.PSChildName -like "{$clsidPrefix*") { $left += "HKCR\$($_.PSChildName)" }
}
Get-ChildItem "HKLM:\SOFTWARE\Microsoft\CTF\TIP" -ErrorAction SilentlyContinue | ForEach-Object {
    if ($_.PSChildName -like "{$clsidPrefix*") { $left += "CTF\TIP\$($_.PSChildName)" }
}
if ($left.Count -eq 0) {
    Write-Host "  All clean!" -ForegroundColor Green
} else {
    Write-Host "  Still remaining:" -ForegroundColor Red
    $left | ForEach-Object { Write-Host "    $_" -ForegroundColor Red }
}
