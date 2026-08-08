# ============================================================================
# winFire Installer Build Script
#
# 流程：
#   1. 用 MSBuild 编译 fire_tsf.vcxproj / fire_config.vcxproj / fire_dictd.vcxproj (Release|x64)
#   2. 用 CMake 构建 tablebuilder.exe 并拷贝到 installer\staging\
#   3. 校验词库工具链（tablebuilder + 码表 → 临时目录现场构建并验证 wb_py_dict.sqlite 产物）
#      （不再预构建词库进包：安装时由 winfire.iss [Code] BuildDictIfMissing 现场生成）
#   4. 生成默认 config.json 到 installer\staging\
#   5. 调用 ISCC.exe 编译 installer\winfire.iss -> dist\WinFire-Setup.exe
#
# 用法：
#   powershell -ExecutionPolicy Bypass -File build_installer.ps1
#   powershell -ExecutionPolicy Bypass -File build_installer.ps1 -SkipBuild   # 跳过 VS 编译（用现有产物）
# ============================================================================
[CmdletBinding()]
param(
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
$ScriptDir  = Split-Path -Parent $MyInvocation.MyCommand.Path
$RepoRoot   = Split-Path -Parent $ScriptDir
$InstallerDir = Join-Path $RepoRoot "installer"
$StagingDir    = Join-Path $InstallerDir "staging"
$DistDir     = Join-Path $RepoRoot "dist"

function Write-Step($msg) {
    Write-Host ""
    Write-Host "========================================" -ForegroundColor Cyan
    Write-Host "  $msg" -ForegroundColor Cyan
    Write-Host "========================================" -ForegroundColor Cyan
}

function Find-MSBuild {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswhere)) { throw "vswhere not found. Install Visual Studio 2022." }
    $msbuild = & $vswhere -latest -requires Microsoft.Component.MSBuild -find "MSBuild\**\Bin\MSBuild.exe" | Select-Object -First 1
    if (-not $msbuild) { throw "MSBuild not found. Install VS Build Tools 2022 with C++ workload." }
    return $msbuild
}

function Find-ISCC {
    # 优先 winget 安装位置（用户级），其次 Program Files
    $candidates = @(
        "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe",
        "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe",
        "$env:ProgramFiles\Inno Setup 6\ISCC.exe"
    )
    foreach ($c in $candidates) {
        if (Test-Path $c) { return $c }
    }
    $cmd = Get-Command iscc -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    throw "Inno Setup 6 not found. Install: winget install JRSoftware.InnoSetup"
}

# ============================================================================
# 1. 编译 VS 工程（fire_tsf.dll + fire_config.exe）
# ============================================================================
if (-not $SkipBuild) {
    Write-Step "[1/5] Compiling Visual Studio projects"
    $msbuild = Find-MSBuild
    Write-Host "MSBuild: $msbuild"

    $projects = @(
        "windows\tsf\fire_tsf.vcxproj",
        "windows\config\fire_config.vcxproj",
        "windows\dictd\fire_dictd.vcxproj"
    )
    foreach ($p in $projects) {
        $proj = Join-Path $RepoRoot $p
        Write-Host "  Building $p ..." -ForegroundColor Yellow
        & $msbuild $proj /p:Configuration=Release /p:Platform=x64 /m /v:minimal /nologo
        if ($LASTEXITCODE -ne 0) { throw "Build failed: $p" }
    }
    Write-Host "  [OK] VS projects built" -ForegroundColor Green
} else {
    Write-Step "[1/5] Skipping VS build (-SkipBuild)"
}

# ============================================================================
# 2. 用 CMake 构建 tablebuilder.exe
# ============================================================================
Write-Step "[2/5] Building tablebuilder via CMake"
$buildDir = Join-Path $RepoRoot "build"
if (-not (Test-Path $buildDir)) {
    New-Item -ItemType Directory -Path $buildDir -Force | Out-Null
}
Write-Host "  CMake configure ..." -ForegroundColor Yellow
& cmake -S $RepoRoot -B $buildDir -DBUILD_CORE=ON -DBUILD_TESTS=OFF -DBUILD_TABLEBUILDER=ON 2>&1 | ForEach-Object { Write-Host "    $_" -ForegroundColor DarkGray }
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed" }

Write-Host "  CMake build tablebuilder ..." -ForegroundColor Yellow
& cmake --build $buildDir --config Release --target tablebuilder 2>&1 | ForEach-Object { Write-Host "    $_" -ForegroundColor DarkGray }
if ($LASTEXITCODE -ne 0) { throw "CMake build failed" }

$tablebuilder = Join-Path $buildDir "Release\tablebuilder.exe"
if (-not (Test-Path $tablebuilder)) {
    $tablebuilder = Join-Path $buildDir "tablebuilder.exe"
}
if (-not (Test-Path $tablebuilder)) { throw "tablebuilder.exe not found after build" }
Write-Host "  [OK] $tablebuilder" -ForegroundColor Green

# 拷贝到 staging，供 winfire.iss 打包进安装程序
if (-not (Test-Path $StagingDir)) {
    New-Item -ItemType Directory -Path $StagingDir -Force | Out-Null
}
Copy-Item $tablebuilder "$StagingDir\tablebuilder.exe" -Force
Write-Host "  [OK] Copied to staging" -ForegroundColor Green

# ============================================================================
# 3. 校验词库工具链（tablebuilder + 码表 → 能产出正确的 wb_py_dict.sqlite）
#    不再预构建 staging db 打包：安装时由 winfire.iss [Code] BuildDictIfMissing
#    现场生成。此处仅构建到临时路径并校验产物，确认 tablebuilder 与码表健康，
#    保证 (a) 安装时现场构建、(b) 配置工具词库管理页「生成词库」两条路径都可靠。
# ============================================================================
Write-Step "[3/5] Verifying dict toolchain (tablebuilder + tables)"
$wbTable = Join-Path $RepoRoot "resources\wb_table.txt"
$pyTable = Join-Path $RepoRoot "resources\py_table.txt"
if (-not (Test-Path $wbTable)) { throw "wb_table.txt not found: $wbTable" }
if (-not (Test-Path $pyTable)) { throw "py_table.txt not found: $pyTable" }

$verifyDb = Join-Path $env:TEMP "winfire_dict_verify.db"
if (Test-Path $verifyDb) { Remove-Item $verifyDb -Force }

Write-Host "  Building verify db (wb + py + combine) ..." -ForegroundColor Yellow
& $tablebuilder --create-dict $wbTable wb_dict $verifyDb 2>&1 | ForEach-Object { Write-Host "    $_" -ForegroundColor DarkGray }
if ($LASTEXITCODE -ne 0) { Remove-Item $verifyDb -Force -ErrorAction SilentlyContinue; throw "wb_dict creation failed (toolchain broken)" }

& $tablebuilder --create-dict $pyTable py_dict $verifyDb 2>&1 | ForEach-Object { Write-Host "    $_" -ForegroundColor DarkGray }
if ($LASTEXITCODE -ne 0) { Remove-Item $verifyDb -Force -ErrorAction SilentlyContinue; throw "py_dict creation failed (toolchain broken)" }

& $tablebuilder --combine-dict $verifyDb 2>&1 | ForEach-Object { Write-Host "    $_" -ForegroundColor DarkGray }
if ($LASTEXITCODE -ne 0) { Remove-Item $verifyDb -Force -ErrorAction SilentlyContinue; throw "combine-dict failed (toolchain broken)" }

# 产物校验：瘦身后正确词库约 7.78MB（19 万行 + 索引 + VACUUM），下限 5MB 足以捕获
# 工具链崩溃 / 缺码表 / 缺 CRT（tablebuilder 已静态 /MT）/ sqlite 损坏等主要故障。
$verifySize = (Get-Item $verifyDb).Length
if ($verifySize -lt 5MB) {
    Remove-Item $verifyDb -Force -ErrorAction SilentlyContinue
    throw "verify db too small: $verifySize bytes (expected >5MB; toolchain broken)"
}
Write-Host "  [OK] Toolchain verified (verify db: $verifySize bytes)" -ForegroundColor Green
Remove-Item $verifyDb -Force -ErrorAction SilentlyContinue

# ============================================================================
# 4. 生成默认 config.json 到 staging
# ============================================================================
Write-Step "[4/5] Generating default config.json"
# installer/staging/ 被 gitignore，config.json 是构建期产物（唯一真相源即此 heredoc）。
# camelCase 键 + 整数枚举（与 windows/config/ConfigStore.cpp::LoadFromString 一致），
# 码表路径用 {APP} 占位符，由 winfire.iss WriteDefaultConfigIfMissing / install.ps1 /
# dev_reload.ps1 在安装期展开为真实程序目录。此 heredoc 必须与各安装脚本保持同步。
$configPath = Join-Path $StagingDir "config.json"
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
  "pyTablePath": "{APP}\\tables\\py_table.txt"
}
"@
[System.IO.File]::WriteAllText($configPath, $config, [System.Text.UTF8Encoding]::new($false))
Write-Host "  [OK] $configPath" -ForegroundColor Green

# ============================================================================
# 5. 调用 ISCC 生成 installer
# ============================================================================
Write-Step "[5/5] Compiling Inno Setup installer"
$iscc = Find-ISCC
$issFile = Join-Path $InstallerDir "winfire.iss"
Write-Host "  ISCC: $iscc" -ForegroundColor Yellow
Write-Host "  ISS:  $issFile" -ForegroundColor Yellow

if (-not (Test-Path $DistDir)) {
    New-Item -ItemType Directory -Path $DistDir -Force | Out-Null
}

& $iscc /Qp $issFile 2>&1 | ForEach-Object { Write-Host "    $_" -ForegroundColor DarkGray }
if ($LASTEXITCODE -ne 0) { throw "ISCC compilation failed" }

$installer = Join-Path $DistDir "WinFire-Setup.exe"
if (Test-Path $installer) {
    $size = (Get-Item $installer).Length
    Write-Host ""
    Write-Host "  [OK] Installer built successfully" -ForegroundColor Green
    Write-Host "       Path: $installer" -ForegroundColor White
    Write-Host "       Size: $([math]::Round($size / 1MB, 2)) MB" -ForegroundColor White
} else {
    throw "Installer not found at expected path: $installer"
}

Write-Host ""
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "  Build Complete!" -ForegroundColor Green
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""
Write-Host "  Run installer:  $installer" -ForegroundColor White
Write-Host "  Install target: $env:ProgramFiles\WinFire" -ForegroundColor White
Write-Host "  User data:      $env:APPDATA\WinFire" -ForegroundColor White
Write-Host ""
