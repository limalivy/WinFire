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
Write-Host "[1/7] Unregistering TSF DLLs..." -ForegroundColor Yellow
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
Write-Host "[2/7] Cleaning COM CLSID keys..." -ForegroundColor Yellow
Get-ChildItem "HKLM:\SOFTWARE\Classes\CLSID" -ErrorAction SilentlyContinue | ForEach-Object {
    $name = $_.PSChildName
    if ($name -like "{$clsidPrefix*") {
        Write-Host "  Remove: HKCR\CLSID\$name"
        Remove-Item $_.PSPath -Recurse -Force
    }
}

# ---- 3. 清理 CTF TIP 注册 (HKLM) ----
Write-Host "[3/7] Cleaning CTF TIP (HKLM)..." -ForegroundColor Yellow
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
Write-Host "[4/7] Cleaning CTF TIP (HKCU)..." -ForegroundColor Yellow
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

# ---- 5. 清理用户级 SortOrder\AssemblyItem 残留（「不可用的输入法」根因） ----
# InstallLayoutOrTip(ILOT_UNINSTALL) 不可靠，SortOrder\AssemblyItem\<langid>\<profile>\<index>
# 中的 WinFire 条目不会随之删除，导致系统设置输入法列表残留「不可用的输入法」。
# 此处直接扫描 HKCU 注册表，按 CLSID 值匹配 WinFire 基 GUID 模式后删除。
Write-Host "[5/7] Cleaning SortOrder\AssemblyItem (HKCU)..." -ForegroundColor Yellow
$sortOrderBase = "HKCU:\Software\Microsoft\CTF\SortOrder\AssemblyItem"
if (Test-Path $sortOrderBase) {
    Get-ChildItem $sortOrderBase -ErrorAction SilentlyContinue | ForEach-Object {
        $langKey = $_
        Get-ChildItem $langKey.PSPath -ErrorAction SilentlyContinue | ForEach-Object {
            $profKey = $_
            Get-ChildItem $profKey.PSPath -ErrorAction SilentlyContinue | ForEach-Object {
                $entry = Get-ItemProperty $_.PSPath -ErrorAction SilentlyContinue
                if ($entry.CLSID -and $entry.CLSID -like "{$clsidPrefix*") {
                    $relPath = $langKey.PSChildName + "\" + $profKey.PSChildName + "\" + $_.PSChildName
                    Write-Host "  Remove: HKCU\CTF\SortOrder\AssemblyItem\$relPath"
                    Remove-Item $_.PSPath -Force -ErrorAction SilentlyContinue
                }
            }
        }
    }
} else {
    Write-Host "  (SortOrder\AssemblyItem not found)" -ForegroundColor DarkGray
}

# ---- 6. 清理用户级 User Profile 输入法列表（Get-WinUserLanguageList 数据源） ----
# HKCU\Control Panel\International\User Profile\<langid> 下的值名为
# "<langid>:{CLSID}{Profile}"，是 Get-WinUserLanguageList 与系统设置
# 「替代默认输入法」下拉的数据源。InstallLayoutOrTip(ILOT_UNINSTALL) 在部分
# Windows 版本上无法移除该处的 WinFire 值，导致卸载后仍残留「不可用的输入法」。
Write-Host "[6/7] Cleaning User Profile input methods (HKCU)..." -ForegroundColor Yellow
$userProfileBase = "HKCU:\Control Panel\International\User Profile"
if (Test-Path $userProfileBase) {
    Get-ChildItem $userProfileBase -ErrorAction SilentlyContinue | ForEach-Object {
        $langKey = $_
        # Get-Item 返回 RegistryKey，其 .Property 数组只含真实注册表值名
        # （不含 PSPath/PSChildName 等 PSObject 元数据，避免误匹配）。
        $key = Get-Item $langKey.PSPath -ErrorAction SilentlyContinue
        if ($key) {
            $toRemove = @()
            foreach ($valName in $key.Property) {
                if ($valName -like "*{8E9F0B21-3C4D-4E5A-9B7C-1F2A3B*") {
                    $toRemove += $valName
                }
            }
            foreach ($name in $toRemove) {
                Write-Host "  Remove: $($langKey.PSChildName)\$name"
                Remove-ItemProperty -Path $langKey.PSPath -Name $name -Force -ErrorAction SilentlyContinue
            }
        }
    }
} else {
    Write-Host "  (User Profile not found)" -ForegroundColor DarkGray
}

# ---- 7. 清理其他残留 ----
Write-Host "[7/7] Cleaning misc..." -ForegroundColor Yellow

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
# 检查 SortOrder\AssemblyItem 残留
$sortOrderBase = "HKCU:\Software\Microsoft\CTF\SortOrder\AssemblyItem"
if (Test-Path $sortOrderBase) {
    Get-ChildItem $sortOrderBase -ErrorAction SilentlyContinue | ForEach-Object {
        $langKey = $_
        Get-ChildItem $langKey.PSPath -ErrorAction SilentlyContinue | ForEach-Object {
            Get-ChildItem $_.PSPath -ErrorAction SilentlyContinue | ForEach-Object {
                $entry = Get-ItemProperty $_.PSPath -ErrorAction SilentlyContinue
                if ($entry.CLSID -and $entry.CLSID -like "{$clsidPrefix*") {
                    $left += "SortOrder\$($langKey.PSChildName)\$($_.PSChildName)"
                }
            }
        }
    }
}
# 检查 User Profile 输入法列表残留
$userProfileBase = "HKCU:\Control Panel\International\User Profile"
if (Test-Path $userProfileBase) {
    Get-ChildItem $userProfileBase -ErrorAction SilentlyContinue | ForEach-Object {
        $langKey = $_
        $key = Get-Item $langKey.PSPath -ErrorAction SilentlyContinue
        if ($key) {
            foreach ($valName in $key.Property) {
                if ($valName -like "*{8E9F0B21-3C4D-4E5A-9B7C-1F2A3B*") {
                    $left += "UserProfile\$($langKey.PSChildName)\$valName"
                }
            }
        }
    }
}
if ($left.Count -eq 0) {
    Write-Host "  All clean!" -ForegroundColor Green
} else {
    Write-Host "  Still remaining:" -ForegroundColor Red
    $left | ForEach-Object { Write-Host "    $_" -ForegroundColor Red }
}
