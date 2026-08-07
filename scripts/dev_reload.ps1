# ============================================================================
# winFire Dev Reload Script
#
# Cycle: Build TSF DLL -> Deploy to Program Files -> Refresh TSF host
# No reboot/logout/version-bump needed.
#
# Usage:
#   powershell -ExecutionPolicy Bypass -File dev_reload.ps1
#   powershell -ExecutionPolicy Bypass -File dev_reload.ps1 -SkipBuild
# ============================================================================
#Requires -RunAsAdministrator
[CmdletBinding()]
param(
    # Skip all builds, use existing binaries
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RepoRoot  = Split-Path -Parent $ScriptDir

$VersionFile = Join-Path $RepoRoot "VERSION"
if (-not (Test-Path $VersionFile)) { $VersionFile = Join-Path $RepoRoot "VERSION.default" }
$Version = (Get-Content $VersionFile -Raw).Trim()

$InstallDir   = "$env:ProgramFiles\WinFire"
$TsfDllSrc    = "$RepoRoot\windows\tsf\x64\Debug\fire_tsf.dll"
$TsfDllDst    = "$InstallDir\fire_tsf_DEV.dll"
$ConfigExeSrc = "$RepoRoot\windows\config\x64\Debug\fire_config.exe"
$ConfigExeDst = "$InstallDir\fire_config.exe"
$DictdExeSrc  = "$RepoRoot\windows\dictd\x64\Debug\fire_dictd.exe"
$DictdExeDst  = "$InstallDir\fire_dictd.exe"
# tablebuilder.exe: CMake builds it under build\Debug\ or build\
if (Test-Path "$RepoRoot\build\Debug\tablebuilder.exe") {
    $TablebuilderSrc = "$RepoRoot\build\Debug\tablebuilder.exe"
} else {
    $TablebuilderSrc = "$RepoRoot\build\tablebuilder.exe"
}
$TablebuilderDst = "$InstallDir\tablebuilder.exe"

# Locate MSBuild via vswhere
function Find-MSBuild {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswhere)) { throw "vswhere not found. Install Visual Studio 2022." }
    $msbuild = & $vswhere -latest -requires Microsoft.Component.MSBuild -find "MSBuild\**\Bin\MSBuild.exe" | Select-Object -First 1
    if (-not $msbuild) { throw "MSBuild not found." }
    return $msbuild
}

# Step counter for progress display
$step = 0
function Step($msg) {
    $global:step++
    Write-Host "[$step/6] $msg" -ForegroundColor Yellow
}

Write-Host ""
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "  WinFire Dev Reload" -ForegroundColor Cyan
Write-Host "  Version: $Version" -ForegroundColor DarkGray
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

# ---- 1. Build ----
if (-not $SkipBuild) {
    Step "Building fire_tsf.dll..."
    $msbuild = Find-MSBuild
    & $msbuild "$RepoRoot\windows\tsf\fire_tsf.vcxproj" /p:Configuration=Debug /p:Platform=x64 /t:Build /v:minimal
    if ($LASTEXITCODE -ne 0) { throw "Build failed" }
    Write-Host "  [OK] fire_tsf.dll (Debug)" -ForegroundColor Green

    & $msbuild "$RepoRoot\windows\config\fire_config.vcxproj" /p:Configuration=Debug /p:Platform=x64 /t:Build /v:minimal
    if ($LASTEXITCODE -ne 0) { throw "Config build failed" }
    Write-Host "  [OK] fire_config.exe (Debug)" -ForegroundColor Green

    & $msbuild "$RepoRoot\windows\dictd\fire_dictd.vcxproj" /p:Configuration=Debug /p:Platform=x64 /t:Build /v:minimal
    if ($LASTEXITCODE -ne 0) { throw "Dictd build failed" }
    Write-Host "  [OK] fire_dictd.exe (Debug)" -ForegroundColor Green

    $cmakeBuildDir = "$RepoRoot\build"
    if (-not (Test-Path "$cmakeBuildDir\CMakeCache.txt")) {
        & cmake -S $RepoRoot -B $cmakeBuildDir -DBUILD_CORE=ON -DBUILD_TESTS=OFF -DBUILD_TABLEBUILDER=ON 2>&1 | Out-Null
        if ($LASTEXITCODE -ne 0) { throw "CMake configure failed" }
    }
    & cmake --build $cmakeBuildDir --config Debug --target tablebuilder 2>&1 | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "tablebuilder build failed" }
    if (Test-Path "$cmakeBuildDir\Debug\tablebuilder.exe") {
        $script:TablebuilderSrc = "$cmakeBuildDir\Debug\tablebuilder.exe"
    } else {
        $script:TablebuilderSrc = "$cmakeBuildDir\tablebuilder.exe"
    }
    Write-Host "  [OK] tablebuilder.exe (Debug)" -ForegroundColor Green
} else {
    Step "Skipping build..."
    if (-not (Test-Path $TsfDllSrc)) { throw "fire_tsf.dll not found at $TsfDllSrc" }
}

# ---- 2. Kill ctfmon to release DLL file locks ----
Step "Releasing TSF host locks..."
# 结束常驻的后台查字进程，释放 fire_dictd.exe 映像占用以便覆盖新构建。
$dictdKilled = $false
Get-Process -Name "fire_dictd" -ErrorAction SilentlyContinue | ForEach-Object {
    Stop-Process -Id $_.Id -Force
    $dictdKilled = $true
}
if ($dictdKilled) {
    Write-Host "  [OK] fire_dictd.exe stopped" -ForegroundColor Green
} else {
    Write-Host "  (fire_dictd not running)" -ForegroundColor DarkGray
}
$ctfmonKilled = $false
Get-Process -Name "ctfmon" -ErrorAction SilentlyContinue | ForEach-Object {
    Stop-Process -Id $_.Id -Force
    $ctfmonKilled = $true
}
if ($ctfmonKilled) {
    Write-Host "  [OK] ctfmon.exe stopped (auto-restarts)" -ForegroundColor Green
} else {
    Write-Host "  (ctfmon not running)" -ForegroundColor DarkGray
}
Start-Sleep -Milliseconds 800

# ---- 3. Deploy ----
Step "Deploying..."
$null = New-Item -ItemType Directory -Path $InstallDir -Force

# Unregister any existing DEV DLL before copy (releases COM lock on file)
Get-ChildItem -Path $InstallDir -Filter "fire_tsf_DEV*.dll" -ErrorAction SilentlyContinue | ForEach-Object {
    & regsvr32 /s /u $_.FullName 2>&1 | Out-Null
}

# Try writing to DEV.dll first; if locked (other TSF hosts still hold it),
# fall back to a unique DEV_N.dll filename.
$copied = $false
$tries  = 0
while (-not $copied -and $tries -lt 10) {
    if ($tries -eq 0) {
        $target = "$InstallDir\fire_tsf_DEV.dll"
    } else {
        $target = "$InstallDir\fire_tsf_DEV_$tries.dll"
    }
    try {
        Copy-Item $TsfDllSrc $target -Force -ErrorAction Stop
        $copied = $true
        $TsfDllDst = $target  # update dest for registration
    } catch {
        $tries++
    }
}
if (-not $copied) {
    throw "Cannot write DLL to $InstallDir (all filenames locked)"
}
$sizeKB = [math]::Round((Get-Item $TsfDllDst).Length / 1KB)
Write-Host "  [OK] $($(Split-Path $TsfDllDst -Leaf)) (${sizeKB}KB)" -ForegroundColor Green

# Unregister old DEV_*.dll files (from previous dev sessions) to avoid
# accumulating stale registrations. Keep only the one we just deployed.
Get-ChildItem -Path $InstallDir -Filter "fire_tsf_DEV*.dll" -ErrorAction SilentlyContinue | ForEach-Object {
    if ($_.FullName -ne $TsfDllDst) {
        & regsvr32 /s /u $_.FullName 2>&1 | Out-Null
        try { Remove-Item $_.FullName -Force -ErrorAction Stop } catch {}
    }
}

# Also unregister any versioned DLL from the installer, so only DEV is active.
Get-ChildItem -Path $InstallDir -Filter "fire_tsf_0.*.dll" -ErrorAction SilentlyContinue | ForEach-Object {
    & regsvr32 /s /u $_.FullName 2>&1 | Out-Null
}

Copy-Item $ConfigExeSrc $ConfigExeDst -Force -ErrorAction SilentlyContinue
Copy-Item $TablebuilderSrc $TablebuilderDst -Force

# fire_dictd.exe: a still-loaded TSF host (Word/Chrome/explorer, not killed in
# step 2) may have respawned it via CreateProcessW since the kill, and Windows
# holds an image lock on a running EXE. Re-kill + retry the copy so we don't
# silently ship a stale binary (the old -ErrorAction SilentlyContinue swallowed
# this failure).
$dictdDeployed = $false
for ($i = 0; $i -lt 10; $i++) {
    Get-Process -Name "fire_dictd" -ErrorAction SilentlyContinue | ForEach-Object {
        Stop-Process -Id $_.Id -Force -ErrorAction SilentlyContinue
    }
    try {
        Copy-Item $DictdExeSrc $DictdExeDst -Force -ErrorAction Stop
        $dictdDeployed = $true
        break
    } catch {
        Start-Sleep -Milliseconds 300
    }
}
if ($dictdDeployed) {
    Write-Host "  [OK] fire_config.exe + tablebuilder.exe + fire_dictd.exe" -ForegroundColor Green
} else {
    Write-Host "  [WARN] fire_dictd.exe copy locked after retries; keeping existing" -ForegroundColor Yellow
    Write-Host "         (other tools deployed OK)" -ForegroundColor Yellow
}

# 授予 AppContainer（ALL APPLICATION PACKAGES，SID S-1-15-2-1）读取+执行权限，
# 使 SearchHost.exe / UWP 等沙箱进程能加载 DLL 并读 tables（与安装包一致）。
& icacls "$InstallDir" /grant "*S-1-15-2-1:(OI)(CI)(RX)" /T /C /Q 2>&1 | Out-Null
Write-Host "  [OK] AppContainer ACL granted" -ForegroundColor Green

# ---- 4. Init user data ----
Step "Checking user data..."
$ConfigDir   = "$env:APPDATA\WinFire"
$ConfigFile   = "$ConfigDir\config.json"
$DictDb       = "$ConfigDir\wb_py_dict.sqlite"
$StatsDb      = "$ConfigDir\statistics.sqlite"
$StagingConf  = "$RepoRoot\installer\staging\config.json"
$null = New-Item -ItemType Directory -Path $ConfigDir -Force

# config.json
if (-not (Test-Path $ConfigFile)) {
    if (Test-Path $StagingConf) {
        Copy-Item $StagingConf $ConfigFile -Force
        Write-Host "  [OK] config.json (from staging)" -ForegroundColor Green
    } else {
        $defaultConfig = @"
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
        [System.IO.File]::WriteAllText($ConfigFile, $defaultConfig, [System.Text.UTF8Encoding]::new($false))
        Write-Host "  [OK] config.json (defaults)" -ForegroundColor Green
    }
} else {
    Write-Host "  [SKIP] config.json exists" -ForegroundColor DarkGray
}

# wb_py_dict.sqlite：用刚部署的 tablebuilder.exe + 项目码表现场生成（与 winfire.iss
# BuildDictIfMissing / install.ps1 Create-DictDb 三步一致：wb → py → combine）。
# staging 不再预构建词库，开发机上现场构建约 1 秒。
if (-not (Test-Path $DictDb)) {
    $wbTable = "$RepoRoot\resources\wb_table.txt"
    $pyTable = "$RepoRoot\resources\py_table.txt"
    if (-not (Test-Path $TablebuilderDst) -or -not (Test-Path $wbTable)) {
        Write-Host "  [WARN] tablebuilder.exe or wb_table.txt not found; cannot build dict" -ForegroundColor Yellow
        Write-Host "         copy a wb_py_dict.sqlite to $ConfigDir" -ForegroundColor Yellow
    } else {
        Write-Host "  Building wb_py_dict.sqlite (tablebuilder + tables)..." -ForegroundColor DarkGray
        & $TablebuilderDst --create-dict $wbTable wb_dict $DictDb 2>&1 | Out-Null
        if ($LASTEXITCODE -ne 0) {
            Write-Host "  [WARN] wb_dict creation failed; copy a wb_py_dict.sqlite to $ConfigDir" -ForegroundColor Yellow
        } else {
            & $TablebuilderDst --create-dict $pyTable py_dict $DictDb 2>&1 | Out-Null
            if ($LASTEXITCODE -ne 0) {
                Write-Host "  [WARN] py_dict creation failed; copy a wb_py_dict.sqlite to $ConfigDir" -ForegroundColor Yellow
            } else {
                & $TablebuilderDst --combine-dict $DictDb 2>&1 | Out-Null
                $sizeMb = [math]::Round((Get-Item $DictDb).Length / 1MB, 1)
                Write-Host "  [OK] wb_py_dict.sqlite (${sizeMb}MB, built from tables)" -ForegroundColor Green
            }
        }
    }
} else {
    Write-Host "  [SKIP] wb_py_dict.sqlite exists" -ForegroundColor DarkGray
}

# statistics.sqlite
if (-not (Test-Path $StatsDb)) {
    $null = New-Item -Path $StatsDb -ItemType File -Force
    Write-Host "  [OK] statistics.sqlite (empty)" -ForegroundColor Green
} else {
    Write-Host "  [SKIP] statistics.sqlite exists" -ForegroundColor DarkGray
}

# Registry key: absolute user data path (Fix B)
$regPath = "HKLM:\Software\WinFire"
New-Item -Path $regPath -Force | Out-Null
New-ItemProperty -Path $regPath -Name "UserDataDir" -Value $ConfigDir -PropertyType String -Force

# ---- 5. Register ----
Step "Registering TSF..."
$result = & regsvr32 /s $TsfDllDst 2>&1
if ($LASTEXITCODE -eq 0) {
    Write-Host "  [OK] TIP registered" -ForegroundColor Green
} else {
    Write-Host "  [WARN] regsvr32 exit code: $LASTEXITCODE" -ForegroundColor Yellow
    Write-Host "  Trying rundll32 fallback..." -ForegroundColor DarkGray
    & rundll32 "$TsfDllDst",DllRegisterServer 2>&1 | Out-Null
    Write-Host "  [OK] rundll32 fallback done" -ForegroundColor Green
}

# 拉起后台查字进程（正常 IL），供沙箱进程经 IPC 查库。
# Windows Defender 常对刚拷贝的 fire_dictd.exe 做短暂独占扫描，导致首启动
# ERROR_SHARING_VIOLATION（"being used by another process"）；重试几次即可。
# 注意：即使这里启动失败也无妨——DLL 端 DictIpcProxy 首次连不上时会自行
# CreateProcessW 拉起同目录 fire_dictd.exe（非沙箱场景兜底，§4.8）。
# dev_reload 不写自启注册项（避免污染开发环境）；正式自启由 install.ps1
# (HKLM\Run) / winfire.iss (HKCU\Run) 写入。本脚本拉起的实例在本次会话常驻
# （dictd 不再空闲退出），重启后由正式安装的 Run 键接管。
$dictdStarted = $false
for ($i = 0; $i -lt 10; $i++) {
    try {
        $null = Start-Process -FilePath $DictdExeDst -WindowStyle Hidden -ErrorAction Stop
        $dictdStarted = $true
        break
    } catch {
        Start-Sleep -Milliseconds 300
    }
}
if ($dictdStarted) {
    Write-Host "  [OK] fire_dictd.exe backend started" -ForegroundColor Green
} else {
    Write-Host "  [WARN] fire_dictd.exe launch failed (Defender/lock); DLL will auto-spawn on first IPC" -ForegroundColor Yellow
}

# ---- 6. Wait for ctfmon to restart ----
Step "Waiting for ctfmon restart..."
$timeout = 100
$ctfmonBack = $false
for ($i = 0; $i -lt $timeout; $i++) {
    if (Get-Process -Name "ctfmon" -ErrorAction SilentlyContinue) {
        $ctfmonBack = $true
        break
    }
    Start-Sleep -Milliseconds 50
}
if ($ctfmonBack) {
    Write-Host "  [OK] ctfmon.exe restarted" -ForegroundColor Green
} else {
    Write-Host "  [WARN] ctfmon has not restarted yet; alt-tab to a text field to trigger it" -ForegroundColor Yellow
}

# ---- Done ----
Write-Host ""
Write-Host "========================================" -ForegroundColor Green
Write-Host "  Dev reload complete!" -ForegroundColor Green
Write-Host "========================================" -ForegroundColor Green
Write-Host ""
Write-Host "  Next: close and reopen your test app" -ForegroundColor White
Write-Host ""
