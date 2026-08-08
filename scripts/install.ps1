# ============================================================================
# winFire Install Script
# Run as Administrator: powershell -ExecutionPolicy Bypass -File install.ps1
# ============================================================================
#Requires -RunAsAdministrator
$ErrorActionPreference = "Stop"

$ScriptDir   = Split-Path -Parent $MyInvocation.MyCommand.Path
$RepoRoot    = Split-Path -Parent $ScriptDir
$InstallDir  = "$env:ProgramFiles\WinFire"
$ConfigDir   = "$env:APPDATA\WinFire"

# TSF DLL 版本化文件名。版本号「单一来源」为仓库根目录的 VERSION 文件；
# VERSION 不纳入 git（本地测试可频繁递增）；缺失时回退到已跟踪的基线 VERSION.default。
# winfire.iss（ISPP 读版本文件）与 Globals.h（编译期生成 Version.h）均取自同一处，
# 三者天然一致。发版/测试改版本只改 VERSION 一处（详见 AGENTS.md「版本号管理」）。
# 版本化 + 侧载可避免升级/卸载时旧同名 DLL 被宿主进程占用而无法覆盖。
$VersionFile = Join-Path $RepoRoot "VERSION"
if (-not (Test-Path $VersionFile)) { $VersionFile = Join-Path $RepoRoot "VERSION.default" }
if (-not (Test-Path $VersionFile)) { throw "Neither VERSION nor VERSION.default found under: $RepoRoot" }
$Version = (Get-Content $VersionFile -Raw).Trim()
if ([string]::IsNullOrWhiteSpace($Version)) { throw "Version file is empty: $VersionFile" }
$TsfDllInstalled = "fire_tsf_$Version.dll"

$TsfDll      = "$RepoRoot\windows\tsf\x64\Release\fire_tsf.dll"
$ConfigExe   = "$RepoRoot\windows\config\x64\Release\fire_config.exe"
# 后台查字进程（正常 IL）：供 AppContainer 沙箱进程经 IPC 查库。
$DictdExe    = "$RepoRoot\windows\dictd\x64\Release\fire_dictd.exe"
# tablebuilder：VS 多配置生成器产物在 build\Release\，单配置生成器在 build\，两处都探测。
if (Test-Path "$RepoRoot\build\Release\tablebuilder.exe") {
    $Tablebuilder = "$RepoRoot\build\Release\tablebuilder.exe"
} else {
    $Tablebuilder = "$RepoRoot\build\tablebuilder.exe"
}

Write-Host ""
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "  WinFire IME Installer" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

# ---- 0. Clean stale PendingFileRenameOperations entries ----
Write-Host "[0/5] Cleaning stale pending file operations..." -ForegroundColor Yellow
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
        Write-Host "  [OK] Removed $($pfrValue.Count - $newValue.Count) stale entry(ies)" -ForegroundColor Green
    } else {
        Write-Host "  [SKIP] No WinFire entries found" -ForegroundColor DarkGray
    }
} else {
    Write-Host "  [SKIP] PFR empty" -ForegroundColor DarkGray
}

# ---- 1. Check build artifacts ----
Write-Host "[1/5] Checking build artifacts..." -ForegroundColor Yellow

@(
    @{Path=$TsfDll;      Name="fire_tsf.dll"},
    @{Path=$ConfigExe;   Name="fire_config.exe"},
    @{Path=$DictdExe;    Name="fire_dictd.exe"}
) | ForEach-Object {
    if (-not (Test-Path $_.Path)) {
        Write-Host ("  [FAIL] Not found: " + $_.Name) -ForegroundColor Red
        Write-Host ("         " + $_.Path) -ForegroundColor Red
        exit 1
    }
    Write-Host ("  [OK]   " + $_.Name) -ForegroundColor Green
}

# ---- 2. Install program files ----
Write-Host "[2/5] Installing program files..." -ForegroundColor Yellow
$null = New-Item -ItemType Directory -Path $InstallDir -Force

# 结束 WinFire 的独立 EXE（dictd 后台 + config/tablebuilder 工具），释放映像占用，
# 使新版 EXE 立即覆盖（与 winfire.iss 的 KillUserExes 对称）。fire_tsf.dll 被宿主进程
# 加载，不在此杀宿主，其映像锁由版本化文件名 + 侧载 + 延迟删除处理。
foreach ($exe in @("fire_dictd.exe","fire_config.exe","tablebuilder.exe")) {
    & taskkill /F /IM $exe 2>&1 | Out-Null
}

# 反注册并清理旧版本 DLL（若存在），随后写入版本化文件名的新 DLL。
# 旧 DLL 若仍被宿主进程占用而删不掉，标记为重启后删除，不阻塞安装。
Get-ChildItem -Path $InstallDir -Filter "fire_tsf_*.dll" -ErrorAction SilentlyContinue | ForEach-Object {
    if ($_.Name -ne $TsfDllInstalled) {
        & regsvr32 /s /u $_.FullName 2>&1 | Out-Null
        try {
            Remove-Item $_.FullName -Force -ErrorAction Stop
        } catch {
            # 仍被占用：标记重启后删除
            $sig = '[DllImport("kernel32.dll", CharSet=CharSet.Unicode)] public static extern bool MoveFileEx(string a, string b, int f);'
            $mf = Add-Type -MemberDefinition $sig -Name MoveFileExNative -Namespace WinFireInstall -PassThru
            $mf::MoveFileEx($_.FullName, $null, 4) | Out-Null   # 4 = MOVEFILE_DELAY_UNTIL_REBOOT
            Write-Host ("  [DEFER] " + $_.Name + " in use, will delete on reboot") -ForegroundColor DarkGray
        }
    }
}

Copy-Item $TsfDll      "$InstallDir\$TsfDllInstalled" -Force
Copy-Item $ConfigExe   "$InstallDir\fire_config.exe" -Force
Copy-Item $DictdExe    "$InstallDir\fire_dictd.exe" -Force
Write-Host ("  [OK]   " + $InstallDir) -ForegroundColor Green

# 授予 ALL APPLICATION PACKAGES（AppContainer，SID S-1-15-2-1）对程序目录的
# 读取+执行权限，使 SearchHost.exe / UWP 等沙箱进程能加载 fire_tsf.dll 并读 tables。
& icacls "$InstallDir" /grant "*S-1-15-2-1:(OI)(CI)(RX)" /T /C /Q 2>&1 | Out-Null
Write-Host "  [OK]   Granted AppContainer ACL on program dir" -ForegroundColor Green

# ---- 3. User data directory + config ----
Write-Host "[3/5] Creating user data..." -ForegroundColor Yellow
$null = New-Item -ItemType Directory -Path $ConfigDir -Force

$ConfigFile = "$ConfigDir\config.json"
if (-not (Test-Path $ConfigFile)) {
    # 默认 config.json：camelCase 键 + 整数枚举（与 windows/config/ConfigStore.cpp::LoadFromString
    # 一致），码表路径用 {APP} 占位符并在此展开为真实程序安装目录（与 winfire.iss
    # WriteDefaultConfigIfMissing 行为一致），使词库管理「已选」装完即正确显示 wb_table.txt /
    # py_table.txt。内联 heredoc（自包含，不依赖 staging 产物），须与 build_installer.ps1 /
    # dev_reload.ps1 保持同步。
    $config = @"
{
  "zKeyQuery": true,
  "showCodeInWindow": true,
  "wubiCodeTip": true,
  "enableWordInput": true,
  "enableDynamicFrequency": false,
  "candidateCount": 5,
  "codeMode": 2,
  "punctuationMode": 1,
  "toggleInputModeKey": 0,
  "enableStatistics": false,
  "wbTablePath": "{APP}\\tables\\wb_table.txt",
  "pyTablePath": "{APP}\\tables\\py_table.txt",
  "theme": {
    "schemaVersion": 2,
    "id": "default",
    "name": "默认",
    "author": "微火输入法",
    "darkModePreference": 0,
    "light": {
      "windowBackgroundColor": "#FFFFFF",
      "windowPaddingTop": 0,
      "windowPaddingBottom": 0,
      "windowPaddingLeft": 0,
      "windowPaddingRight": 0,
      "windowBorderRadius": 6,
      "enableLiquidGlass": true,
      "originCodeColor": "#4D4D4D",
      "originCandidatesSpace": 0,
      "originPaddingTop": 0,
      "originPaddingLeft": 0,
      "originPaddingRight": 0,
      "originPaddingBottom": 0,
      "candidateSpace": 0,
      "candidateIndexColor": "#1A1A1A",
      "candidateTextColor": "#1A1A1A",
      "candidateCodeColor": "#4D4D4DCC",
      "candidateRadius": 0,
      "candidatePaddingTop": 2,
      "candidatePaddingLeft": 2,
      "candidatePaddingRight": 2,
      "candidatePaddingBottom": 2,
      "selectedIndexColor": "#DC143C",
      "selectedTextColor": "#DC143C",
      "selectedCodeColor": "#DC143CCC",
      "selectedBackgroundColor": "#0000000F",
      "pageIndicatorColor": "#DC143C",
      "pageIndicatorDisabledColor": "#DC143C66",
      "fontName": "system",
      "fontSize": 20,
      "indexFontSize": 20,
      "codeFontSize": 20
    },
    "dark": {
      "windowBackgroundColor": "#000000",
      "windowPaddingTop": 0,
      "windowPaddingBottom": 0,
      "windowPaddingLeft": 0,
      "windowPaddingRight": 0,
      "windowBorderRadius": 6,
      "enableLiquidGlass": true,
      "originCodeColor": "#FFFFFF",
      "originCandidatesSpace": 0,
      "originPaddingTop": 0,
      "originPaddingLeft": 0,
      "originPaddingRight": 0,
      "originPaddingBottom": 0,
      "candidateSpace": 0,
      "candidateIndexColor": "#E6E6E6",
      "candidateTextColor": "#E6E6E6",
      "candidateCodeColor": "#B3B3B3CC",
      "candidateRadius": 0,
      "candidatePaddingTop": 2,
      "candidatePaddingLeft": 2,
      "candidatePaddingRight": 2,
      "candidatePaddingBottom": 2,
      "selectedIndexColor": "#DC143C",
      "selectedTextColor": "#DC143C",
      "selectedCodeColor": "#DC143CCC",
      "selectedBackgroundColor": "#FFFFFF14",
      "pageIndicatorColor": "#DC143C",
      "pageIndicatorDisabledColor": "#DC143C66",
      "fontName": "system",
      "fontSize": 20,
      "indexFontSize": 20,
      "codeFontSize": 20
    }
  }
}
"@
    $config = $config.Replace('{APP}', $InstallDir)
    [System.IO.File]::WriteAllText($ConfigFile, $config, [System.Text.UTF8Encoding]::new($false))
    Write-Host "  [OK]   config.json" -ForegroundColor Green
} else {
    Write-Host "  [SKIP] config.json already exists" -ForegroundColor DarkGray
}
Write-Host ("  [OK]   " + $ConfigDir) -ForegroundColor Green

# 将用户数据目录的绝对路径写入注册表（Fix B）。
# TIP DLL 可能被加载到 SearchHost.exe 等 SYSTEM/AppContainer 进程中，
# 此时 CSIDL_APPDATA 指向系统目录；注册表固化路径是唯一可靠的定位方式。
$regPath = "HKLM:\Software\WinFire"
New-Item -Path $regPath -Force | Out-Null
New-ItemProperty -Path $regPath -Name "UserDataDir" -Value $ConfigDir -PropertyType String -Force

# ---- 4. Create dictionary database ----
Write-Host "[4/5] Creating dictionary..." -ForegroundColor Yellow

$DictDb = "$ConfigDir\wb_py_dict.sqlite"
$StatsDb= "$ConfigDir\statistics.sqlite"
$TmpDir = "$env:TEMP\fire_install"
$null = New-Item -ItemType Directory -Path $TmpDir -Force

# 项目内置码表（来源：Fire 项目 Resources/，86 版五笔 + 98 版五笔 + 拼音）
$BuiltinWbTable   = "$RepoRoot\resources\wb_table.txt"
$BuiltinWb98Table = "$RepoRoot\resources\wb_98_table.txt"
$BuiltinPyTable   = "$RepoRoot\resources\py_table.txt"

function Create-DictDb {
    if (Test-Path $DictDb) {
        Write-Host "  [SKIP] wb_py_dict.sqlite already exists" -ForegroundColor DarkGray
        return
    }

    if (-not (Test-Path $Tablebuilder)) {
        Write-Host "  [WARN] tablebuilder.exe not found, skip dict creation" -ForegroundColor Yellow
        return
    }

    # 优先使用项目内置的完整码表；若不存在则回退到最小测试词库
    $useBuiltinWb = Test-Path $BuiltinWbTable
    $useBuiltinPy = Test-Path $BuiltinPyTable

    if ($useBuiltinWb) {
        $wbPath = $BuiltinWbTable
        Write-Host "  Using builtin wubi table (86): $wbPath" -ForegroundColor DarkGray
    } else {
        Write-Host "  [WARN] builtin wb_table.txt not found, using minimal test data" -ForegroundColor Yellow
        $wbLines = @(
            "aaaa gong",
            "kkkk kou",
            "tttt he",
            "wwww ren",
            "jjjj ri",
            "eeee yue",
            "bbbb zi",
            "vvvv nv",
            "cccc you",
            "an an",
            "wgkr wubi",
            "rq de",
            "wq ni",
            "wb ta",
            "qn wo",
            "yn shi",
            "ce neng",
            "gc dao"
        )
        $wbPath = "$TmpDir\wb_dict.txt"
        [System.IO.File]::WriteAllLines($wbPath, $wbLines, [System.Text.UTF8Encoding]::new($false))
    }

    if ($useBuiltinPy) {
        $pyPath = $BuiltinPyTable
        Write-Host "  Using builtin pinyin table: $pyPath" -ForegroundColor DarkGray
    } else {
        Write-Host "  [WARN] builtin py_table.txt not found, using minimal test data" -ForegroundColor Yellow
        $pyLines = @(
            "an an",
            "de de",
            "ni ni",
            "ta ta",
            "wo wo",
            "shi shi",
            "neng neng",
            "dao dao"
        )
        $pyPath = "$TmpDir\py_dict.txt"
        [System.IO.File]::WriteAllLines($pyPath, $pyLines, [System.Text.UTF8Encoding]::new($false))
    }

    Write-Host "  Creating wb_dict (this may take a moment)..." -ForegroundColor DarkGray
    & $Tablebuilder --create-dict $wbPath wb_dict $DictDb 2>&1 | ForEach-Object { Write-Host ("    " + $_) -ForegroundColor DarkGray }
    if ($LASTEXITCODE -ne 0) {
        Write-Host "  [FAIL] wb_dict creation failed" -ForegroundColor Red
        return
    }

    Write-Host "  Creating py_dict (this may take a moment)..." -ForegroundColor DarkGray
    & $Tablebuilder --create-dict $pyPath py_dict $DictDb 2>&1 | ForEach-Object { Write-Host ("    " + $_) -ForegroundColor DarkGray }
    if ($LASTEXITCODE -ne 0) {
        Write-Host "  [FAIL] py_dict creation failed" -ForegroundColor Red
        return
    }

    Write-Host "  Combining wb_py_dict..." -ForegroundColor DarkGray
    & $Tablebuilder --combine-dict $DictDb 2>&1 | ForEach-Object { Write-Host ("    " + $_) -ForegroundColor DarkGray }
    Write-Host "  [OK]   wb_py_dict.sqlite" -ForegroundColor Green
}

function Create-StatsDb {
    if (Test-Path $StatsDb) {
        Write-Host "  [SKIP] statistics.sqlite already exists" -ForegroundColor DarkGray
        return
    }
    $null = New-Item -Path $StatsDb -ItemType File -Force
    Write-Host "  [OK]   statistics.sqlite (auto-init at runtime)" -ForegroundColor Green
}

Create-DictDb
Create-StatsDb

# ---- 5. Register TSF ----
Write-Host "[5/5] Registering IME..." -ForegroundColor Yellow

$DllPath = "$InstallDir\$TsfDllInstalled"
Push-Location $InstallDir
try {
    $result = & regsvr32 /s "$DllPath" 2>&1
    if ($LASTEXITCODE -eq 0) {
        Write-Host "  [OK]   IME registered" -ForegroundColor Green
    } else {
        Write-Host ("  [WARN] regsvr32 exit code: " + $LASTEXITCODE) -ForegroundColor Yellow
        Write-Host "  Trying rundll32 fallback..." -ForegroundColor Yellow
        & rundll32 "$DllPath",DllRegisterServer 2>&1 | Out-Null
    }
} finally {
    Pop-Location
}

# 写入 HKLM\Run 自启动键：系统重启后 dictd 随任意用户登录自启，沙箱进程首次输入即可出候选。
# 用 HKLM（而非 HKCU）是因为本脚本以 #Requires -RunAsAdministrator 运行，提权上下文里 HKCU
# 指向管理员账户而非实际登录用户；HKLM\Run 对所有用户生效，避开此陷阱。Inno 安装包则用 HKCU\Run。
$runKey = "HKLM:\Software\Microsoft\Windows\CurrentVersion\Run"
if (-not (Test-Path $runKey)) { New-Item -Path $runKey -Force | Out-Null }
New-ItemProperty -Path $runKey -Name "WinFireDictd" -Value "$InstallDir\fire_dictd.exe" -PropertyType String -Force | Out-Null
Write-Host "  [OK]   Added HKLM\Run autostart for fire_dictd.exe" -ForegroundColor Green

# 立即拉起后台查字进程（正常 IL），使沙箱进程首次输入即可出候选，
# 无需等 DLL 端按需拉起（AppContainer 进程通常无权 CreateProcess）。
Start-Process -FilePath "$InstallDir\fire_dictd.exe" -WindowStyle Hidden
Write-Host "  [OK]   Started fire_dictd.exe backend" -ForegroundColor Green

# ---- Done ----
Write-Host ""
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "  Installation Complete!" -ForegroundColor Green
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""
Write-Host ("  Program : " + $InstallDir) -ForegroundColor White
Write-Host ("  Config  : " + $ConfigDir) -ForegroundColor White
Write-Host ""
Write-Host "  Next steps:" -ForegroundColor Cyan
Write-Host "  1. Settings -> Language -> Chinese -> Add Keyboard -> wei huo wu bi" -ForegroundColor White
Write-Host ("  2. Run " + $InstallDir + "\fire_config.exe to configure") -ForegroundColor White
Write-Host ""
Write-Host ("  Uninstall: " + $ScriptDir + "\uninstall.ps1") -ForegroundColor DarkGray
Write-Host ""

# Cleanup
Remove-Item -Recurse -Force $TmpDir -ErrorAction SilentlyContinue
