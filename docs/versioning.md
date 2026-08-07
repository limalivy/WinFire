# 构建与版本号管理

> 本文是 WinFire 的构建流程与版本号管理完整说明。
> 日常任务遵循 [AGENTS.md](../AGENTS.md) 的版本红线（摘要）即可；
> 需要完整的构建步骤、三消费方派生机制、强制约束细节时再查本文。

## 1. 跨平台内核（CMake，可在 macOS / Linux 验证）

只有内核（`core/` + `tablebuilder/` + `tests/`）通过 CMake 构建：

```bash
cmake -S . -B build
cmake --build build
cd build && ctest --output-on-failure
```

CMake 选项（默认 ON）：`BUILD_CORE` / `BUILD_TESTS` / `BUILD_TABLEBUILDER`。
sqlite3 从系统 SDK / homebrew 定位（`find_package(SQLite3)`，回退 `-lsqlite3`）。
IPC 协议库（`ipc/`）随 `BUILD_CORE` 一并构建，`test_ipc_protocol.cpp` 随 ctest 验证
（编解码往返；命名管道传输层仅 Win32，macOS 不编译）。

## 2. Windows 层（MSBuild，需 VS2022 + ATL + Windows SDK 10）

> 注：fire_config 已从 MFC 迁移到纯 Win32（PropertySheet API），不再需要 MFC 组件。
> fire_tsf.dll 仍需 ATL（纯原生 COM）。

```powershell
# 编译 TSF TIP DLL（fire_tsf.dll，含 DictIpcProxy + NamedPipeClient + ipc/ 协议；不含 SQLite）
MSBuild.exe windows\tsf\fire_tsf.vcxproj /p:Configuration=Release /p:Platform=x64

# 编译配置工具 EXE（fire_config.exe）
MSBuild.exe windows\config\fire_config.vcxproj /p:Configuration=Release /p:Platform=x64

# 编译后台查字进程 EXE（fire_dictd.exe，命名管道 server + DictManager+Statistics + SQLite）
MSBuild.exe windows\dictd\fire_dictd.vcxproj /p:Configuration=Release /p:Platform=x64

# 构建 tablebuilder.exe（打包进安装包：安装时/配置工具用它现场生成词库）
cmake -S . -B build -DBUILD_TABLEBUILDER=ON
cmake --build build --target tablebuilder --config Release
```

注意：`windows/config/ConfigApp.rc` 含 UTF-8 中文，文件首行已加 `#pragma code_page(65001)`
声明，避免 rc.exe 用系统 GBK 误解析。三个工程（DLL 除外）编译 sqlite 均经
`third_party/sqlite3/fire_sqlite3_amalg.c` wrapper 引入 `fire_sqlite_compile_options.h`
集中裁剪宏（SQLITE_OMIT_*），保持 sqlite3.c/h 原文件不动、便于升级 amalgamation。

## 3. 安装包（Inno Setup 6）

```powershell
# 一键流程：MSBuild 编译三个 VS 工程 → CMake 构建 tablebuilder → 校验词库工具链 →
# 生成默认 config.json → ISCC 编译 winfire.iss
powershell -ExecutionPolicy Bypass -File scripts\build_installer.ps1

# 仅编译 installer（跳过 VS 编译，使用现有产物）
powershell -ExecutionPolicy Bypass -File scripts\build_installer.ps1 -SkipBuild
```

流程（详见 `build_installer.ps1` 注释）：MSBuild 编译 `fire_tsf.dll` / `fire_config.exe` /
`fire_dictd.exe` → CMake 构建 `tablebuilder.exe` 拷贝到 `installer\staging\` → 用
tablebuilder + 码表在临时目录**现场构建并校验**词库工具链（产物 < 5MB 判失败，保证
安装时现场构建与配置工具「生成词库」两条路径都可靠）→ 生成默认 `config.json` 到 staging
→ ISCC 编译 `winfire.iss`。

产物：`dist\WinFire-Setup.exe`（单文件 installer，含卸载器）。
安装目标：`%ProgramFiles%\WinFire\`（程序文件） + `%APPDATA%\WinFire\`（用户数据）。

**词库安装时现场生成**：安装包不再预构建 `wb_py_dict.sqlite`（省 ~3.4MB 包体积），改为
安装完成时由 `winfire.iss` 的 `[Code] BuildDictIfMissing` 调用 `{app}\tablebuilder.exe` +
`{app}\tables` 码表现场生成（约 1 秒；仅当用户无已有词库，失败不中断安装，可稍后在配置
工具「词库管理」重建）。安装/卸载前 `KillUserExes` 统一结束三个独立 EXE（fire_dictd /
fire_config / tablebuilder）；PFR（PendingFileRenameOperations）清理只在
`InitializeSetup` / `InitializeUninstall` 进行，**不在** post 阶段清（避免撤销
`DeleteOrDeferDll` 的重启删除指令，详见 winfire.iss 内注释）。

## 4. 版本号管理（单一来源 + 强制约束）

**单一来源（Single Source of Truth）**：仓库根目录的版本文件（纯文本，形如 `0.1.0`，
`MAJOR.MINOR.PATCH` 三段，各段必须 `<= 255`）。分两个文件以兼顾「单点修改」与「不频繁污染 git」：

- `VERSION`（**不纳入 git**，见 `.gitignore`）：本地测试用的工作版本，可频繁递增，不产生提交噪音。
- `VERSION.default`（**纳入 git**）：基线版本，仅在**正式发版**时递增并提交。

三个消费方均**优先读 `VERSION`，缺失时回退 `VERSION.default`**（保证全新 clone / CI 也能构建）。
所有其它位置的版本号都从这里派生，**禁止**在别处手写版本号：

| 消费方 | 派生机制 | 说明 |
|--------|----------|------|
| `windows/tsf/Version.h`（`FIRE_VER_MAJOR/MINOR/PATCH`、`FIRE_VER_STRING`） | `fire_tsf.vcxproj` 的 `GenerateVersionHeader` 目标在 `ClCompile` 前读版本文件生成 | 生成产物，**不纳入版本控制**（已 `.gitignore`）；`Globals.h` 只 `#include "Version.h"`。CLSID/Profile GUID 末 3 字节由此派生（侧载升级）。 |
| `installer/winfire.iss`（`MyAppVersion`） | ISPP 编译期 `FileOpen/FileRead` 版本文件 | DLL 版本化文件名 `fire_tsf_<VERSION>.dll`、AppVersion、卸载注册表 DisplayVersion 均取自此。 |
| `scripts/install.ps1`（`$Version`） | 运行时 `Get-Content <RepoRoot>\VERSION`（回退 `VERSION.default`） | 直接部署脚本的版本化 DLL 文件名。 |

三处均在两文件都缺失/为空时报错中止，确保不会静默用到过期版本号。

### 强制约束（务必遵守）

- **测试即发版语义**：每次要产出可安装/可注册的构建物用于**测试**（编译 `fire_tsf.dll` 并
  `regsvr32` 注册、跑 installer、或跑 `install.ps1`）时，**必须先递增 `VERSION`**（通常 +PATCH）。
  原因：TSF 是 in-process DLL，旧版本 DLL 会被宿主进程（Word/Chrome/explorer/ctfmon…）加映像锁
  占用；只有版本化文件名 + 按版本派生的 CLSID/Profile 侧载，才能让新构建物在不重启、不强杀宿主
  进程的前提下安装并生效。**复用同一版本号重复测试会命中旧文件占用与旧 CLSID 缓存，导致「改了没生效」
  的假象**。
- **单点修改**：测试期只改 `VERSION` 一处；正式发版时把定稿版本写入 `VERSION.default`（并提交），
  然后重新构建。**不要**手改 `Version.h`、`winfire.iss`、`install.ps1` 里的版本号（`Version.h` 会被
  构建覆盖，另两处根本不含字面量）。
- **提交前检查**：`VERSION`（本地工作版本）与 `windows/tsf/Version.h`（生成产物）都不应出现在 git
  变更中；只有正式发版才提交 `VERSION.default`。

### 更新版本号操作步骤

```text
1. 本地测试：编辑 VERSION（如 0.1.0 -> 0.1.1），此文件不进 git。
   正式发版：同步把定稿版本写入 VERSION.default 并提交。三段均需 <= 255。
2. 重新构建：
   - 内核回归：cmake --build build && (cd build && ctest --output-on-failure)
   - Windows 层/安装包：scripts\build_installer.ps1（MSBuild 会自动重生成 Version.h）
     或单独 MSBuild windows\tsf\fire_tsf.vcxproj，或 scripts\install.ps1 直接部署。
3. 安装新构建物即为侧载升级：新版用新 DLL 文件名 + 新 CLSID/Profile 注册，
   旧版反注册、旧 DLL 占用时延迟到重启删除（无需重启系统即可测试新版）。
```
