# ============================================================================
# winFire Uninstall Script
# Run as Administrator: powershell -ExecutionPolicy Bypass -File uninstall.ps1
# ============================================================================
#Requires -RunAsAdministrator
$ErrorActionPreference = "Stop"

$InstallDir = "$env:ProgramFiles\WinFire"

Write-Host ""
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "  WinFire IME Uninstaller" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

# 1. Unregister TSF
Write-Host "[1/3] Unregistering IME..." -ForegroundColor Yellow
# 反注册所有版本化 DLL（fire_tsf_*.dll）；兼容旧的固定名 fire_tsf.dll。
$Dlls = @()
$Dlls += Get-ChildItem -Path $InstallDir -Filter "fire_tsf_*.dll" -ErrorAction SilentlyContinue | ForEach-Object { $_.FullName }
if (Test-Path "$InstallDir\fire_tsf.dll") { $Dlls += "$InstallDir\fire_tsf.dll" }

if ($Dlls.Count -gt 0) {
    Push-Location $InstallDir
    try {
        foreach ($d in $Dlls) {
            & regsvr32 /s /u "$d" 2>&1 | Out-Null
            if ($LASTEXITCODE -ne 0) {
                & rundll32 "$d",DllUnregisterServer 2>&1 | Out-Null
            }
            Write-Host ("  [OK] Unregistered " + (Split-Path $d -Leaf)) -ForegroundColor Green
        }
    } finally {
        Pop-Location
    }
} else {
    Write-Host "  [SKIP] no fire_tsf DLL found" -ForegroundColor DarkGray
}

# 2. Remove registry entries
Write-Host "[2/4] Removing registry entries..." -ForegroundColor Yellow
$regPath = "HKLM:\Software\WinFire"
if (Test-Path $regPath) {
    Remove-Item -Path $regPath -Recurse -Force
    Write-Host "  [OK] Removed HKLM\Software\WinFire" -ForegroundColor Green
} else {
    Write-Host "  [SKIP] Registry key not found" -ForegroundColor DarkGray
}

# 移除 dictd 自启动项（install.ps1 写 HKLM\Run；Inno 安装包写 HKCU\Run；两处都清保证干净）。
foreach ($hive in @("HKLM:\Software\Microsoft\Windows\CurrentVersion\Run",
                    "HKCU:\Software\Microsoft\Windows\CurrentVersion\Run")) {
    if (Get-ItemProperty -Path $hive -Name "WinFireDictd" -ErrorAction SilentlyContinue) {
        Remove-ItemProperty -Path $hive -Name "WinFireDictd" -Force
        Write-Host "  [OK] Removed $hive\WinFireDictd" -ForegroundColor Green
    }
}

# 清理用户级 SortOrder\AssemblyItem 中的 WinFire 残留条目。
# regsvr32 /u 调用 DllUnregisterServer 会清理这些，但如果 DLL 已被删除则无法调用，
# 此处兜底直接扫描注册表删除，防止残留「不可用的输入法」。
$clsidPrefix = "8E9F0B21-3C4D-4E5A-9B7C-1F2A3B"
$sortOrderBase = "HKCU:\Software\Microsoft\CTF\SortOrder\AssemblyItem"
if (Test-Path $sortOrderBase) {
    $cleaned = $false
    Get-ChildItem $sortOrderBase -ErrorAction SilentlyContinue | ForEach-Object {
        $langKey = $_
        Get-ChildItem $langKey.PSPath -ErrorAction SilentlyContinue | ForEach-Object {
            Get-ChildItem $_.PSPath -ErrorAction SilentlyContinue | ForEach-Object {
                $entry = Get-ItemProperty $_.PSPath -ErrorAction SilentlyContinue
                if ($entry.CLSID -and $entry.CLSID -like "{$clsidPrefix*") {
                    Remove-Item $_.PSPath -Force -ErrorAction SilentlyContinue
                    $cleaned = $true
                }
            }
        }
    }
    if ($cleaned) {
        Write-Host "  [OK] Removed SortOrder\AssemblyItem WinFire entries" -ForegroundColor Green
    }
}

# 清理 HKCU\Control Panel\International\User Profile 中的 WinFire 输入法条目。
# Get-WinUserLanguageList 与「替代默认输入法」下拉均从此处读，ILOT_UNINSTALL 不可靠，
# 直接扫描删除，防止残留「不可用的输入法」。
$userProfileBase = "HKCU:\Control Panel\International\User Profile"
if (Test-Path $userProfileBase) {
    $cleanedUp = $false
    Get-ChildItem $userProfileBase -ErrorAction SilentlyContinue | ForEach-Object {
        $langKey = $_
        $key = Get-Item $langKey.PSPath -ErrorAction SilentlyContinue
        if ($key) {
            $toRemove = @()
            foreach ($valName in $key.Property) {
                if ($valName -like "*{8E9F0B21-3C4D-4E5A-9B7C-1F2A3B*") {
                    $toRemove += $valName
                }
            }
            foreach ($name in $toRemove) {
                Remove-ItemProperty -Path $langKey.PSPath -Name $name -Force -ErrorAction SilentlyContinue
                $cleanedUp = $true
            }
        }
    }
    if ($cleanedUp) {
        Write-Host "  [OK] Removed User Profile WinFire entries" -ForegroundColor Green
    }
}

# 3. Remove program files
Write-Host "[3/4] Removing program files..." -ForegroundColor Yellow

# 结束常驻的后台查字进程（改常驻后不再空闲自退，必须主动杀以释放映像占用）。
& taskkill /F /IM fire_dictd.exe 2>&1 | Out-Null
if (Test-Path $InstallDir) {
    # 先处理可能仍被宿主进程占用的 DLL：删不掉则标记重启后删除，避免整体删除失败。
    $sig = '[DllImport("kernel32.dll", CharSet=CharSet.Unicode)] public static extern bool MoveFileEx(string a, string b, int f);'
    $mf = Add-Type -MemberDefinition $sig -Name MoveFileExNative -Namespace WinFireUninstall -PassThru
    Get-ChildItem -Path $InstallDir -Filter "fire_tsf*.dll" -ErrorAction SilentlyContinue | ForEach-Object {
        try {
            Remove-Item $_.FullName -Force -ErrorAction Stop
        } catch {
            $mf::MoveFileEx($_.FullName, $null, 4) | Out-Null   # 4 = MOVEFILE_DELAY_UNTIL_REBOOT
            Write-Host ("  [DEFER] " + $_.Name + " in use, will delete on reboot") -ForegroundColor DarkGray
        }
    }
    Remove-Item -Recurse -Force $InstallDir -ErrorAction SilentlyContinue
    if (Test-Path $InstallDir) {
        Write-Host ("  [PARTIAL] some files in use; remaining will be removed on reboot") -ForegroundColor Yellow
    } else {
        Write-Host ("  [OK] Removed " + $InstallDir) -ForegroundColor Green
    }
} else {
    Write-Host "  [SKIP] Directory not found" -ForegroundColor DarkGray
}

# 4. User data
Write-Host "[4/4] User data..." -ForegroundColor Yellow
$ConfigDir = "$env:APPDATA\WinFire"
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
