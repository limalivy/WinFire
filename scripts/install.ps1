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

$TsfDll      = "$RepoRoot\windows\tsf\x64\Release\fire_tsf.dll"
$ConfigExe   = "$RepoRoot\windows\config\x64\Release\fire_config.exe"
$Tablebuilder= "$RepoRoot\build\tablebuilder.exe"

Write-Host ""
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "  WinFire IME Installer" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

# ---- 1. Check build artifacts ----
Write-Host "[1/5] Checking build artifacts..." -ForegroundColor Yellow

@(
    @{Path=$TsfDll;      Name="fire_tsf.dll"},
    @{Path=$ConfigExe;   Name="fire_config.exe"}
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
Copy-Item $TsfDll      "$InstallDir\fire_tsf.dll"   -Force
Copy-Item $ConfigExe   "$InstallDir\fire_config.exe" -Force
Write-Host ("  [OK]   " + $InstallDir) -ForegroundColor Green

# ---- 3. User data directory + config ----
Write-Host "[3/5] Creating user data..." -ForegroundColor Yellow
$null = New-Item -ItemType Directory -Path $ConfigDir -Force

$ConfigFile = "$ConfigDir\config.json"
if (-not (Test-Path $ConfigFile)) {
    $config = @"
{
  "candidate_count": 5,
  "code_mode": "wubiPinyin",
  "punctuation_mode": "zhHans",
  "enable_word_input": true,
  "enable_dynamic_frequency": false,
  "show_code_in_window": true,
  "wubi_code_tip": true,
  "z_key_query": true,
  "enable_statistics": false,
  "toggle_input_mode_key": "shift"
}
"@
    [System.IO.File]::WriteAllText($ConfigFile, $config, [System.Text.UTF8Encoding]::new($false))
    Write-Host "  [OK]   config.json" -ForegroundColor Green
} else {
    Write-Host "  [SKIP] config.json already exists" -ForegroundColor DarkGray
}
Write-Host ("  [OK]   " + $ConfigDir) -ForegroundColor Green

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

$DllPath = "$InstallDir\fire_tsf.dll"
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
