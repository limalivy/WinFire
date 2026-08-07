# WinFire — 微火五笔输入法 Windows 移植（移植自业火五笔 Fire）

将 macOS「业火五笔输入法（Fire）」（Swift + InputMethodKit + SwiftUI + SQLCipher）
重新实现到 Windows。核心策略：**抽离平台无关内核**（状态机 + 词库，纯 C++17），
Windows 平台各层只做「宿主适配 + UI 渲染」。

> 本文件是 AI 协作的**行为规则**（必读必守）。架构与实现细节已拆到 `docs/`，按需查阅。
> 项目面向用户的介绍见 [README.md](README.md)。

---

## 1. 分层架构边界（每层职责，违反即越界）

| 层 | 目录 | 职责 | 硬边界 |
|----|------|------|--------|
| **跨平台内核** | `core/` | 状态机（16 段 handler 链）、词库查询、标点转换、输入统计 | **纯 C++17，不得依赖任何 Windows / macOS API**；对外接口 `IDictService` |
| **IPC 协议** | `ipc/` | 命名管道消息的帧头 + payload 编解码（纯 C++17，入 CMake 可测） | 只描述协议字节，不实现传输；传输层（命名管道）仅 Win32，macOS 不编译 |
| **Windows 平台层** | `windows/` | TSF TIP DLL / 候选窗 / 配置界面 / 后台查字进程 | 业务逻辑下沉内核；本层只做按键翻译、组字区、UI 渲染、IPC 转发 |
| **词库构建工具** | `tablebuilder/` | 码表 txt → sqlite | 独立 CMake 工程，被安装包与配置工具调用 |
| **安装/脚本** | `installer/` `scripts/` | Inno Setup 安装包、PowerShell 部署/维护脚本 | 不含业务逻辑 |

子目录速查：`windows/tsf/`（ATL TIP DLL）、`windows/candidate_window/`（GDI+ 候选窗）、
`windows/config/`（纯 Win32 配置 EXE）、`windows/dictd/`（后台查字进程）、`windows/common/`（IPC 常量）。

## 2. 红线规则（务必遵守）

### 2.1 版本号：测试即发版（最重要）
TSF 是 in-process DLL，旧版本 DLL 会被宿主进程（Word/Chrome/explorer/ctfmon…）加映像锁占用。
**每次产出可安装/可注册构建物用于测试前，必须先递增 `VERSION`**（通常 +PATCH），否则会命中
旧文件占用与旧 CLSID 缓存，产生「改了没生效」的假象。

- 版本文件：`VERSION`（不进 git，本地测试用）/ `VERSION.default`（进 git，仅正式发版递增）。
  三处消费方（`Version.h` / `winfire.iss` / `install.ps1`）**优先读 `VERSION`，回退 `VERSION.default`**。
- **单点修改**：只改 `VERSION` 一处；**禁止**手改 `Version.h`（构建覆盖）、`winfire.iss`/`install.ps1`（不含字面量）。
- 正式发版时把定稿版本写入 `VERSION.default` 并提交；提交前确认 `VERSION` 与 `Version.h` 不在 git 变更中。
- 完整机制（三消费方派生、CLSID/Profile 侧载、操作步骤）见 [docs/versioning.md](docs/versioning.md) §4。

### 2.2 字符串编码边界
内核统一 **UTF-8**；TSF / GDI+ / Win32 边界处与 `wchar_t`(UTF-16) 互转
（`firecfg::Utf8ToWide` / `WideToUtf8`）。跨边界传字符串前确认编码。

### 2.3 平台隔离
内核代码**禁止**包含任何 `#include <windows.h>` 或 macOS 系统头。平台差异由 `windows/` 层吸收。

## 3. 构建与验证

```bash
# 跨平台内核（CMake，可在 macOS / Linux 验证回归）
cmake -S . -B build && cmake --build build
(cd build && ctest --output-on-failure)
```

```powershell
# Windows 层（MSBuild，需 VS2022 + ATL + Windows SDK 10；fire_config 已无 MFC）
MSBuild.exe windows\tsf\fire_tsf.vcxproj    /p:Configuration=Release /p:Platform=x64
MSBuild.exe windows\config\fire_config.vcxproj /p:Configuration=Release /p:Platform=x64
MSBuild.exe windows\dictd\fire_dictd.vcxproj  /p:Configuration=Release /p:Platform=x64

# 一键安装包（编译三工程 → tablebuilder → 校验词库链 → ISCC）
powershell -ExecutionPolicy Bypass -File scripts\build_installer.ps1
```

CMake 选项（默认 ON）：`BUILD_CORE` / `BUILD_TESTS` / `BUILD_TABLEBUILDER`。
完整构建步骤、sqlite wrapper 裁剪宏、安装时现场生成词库、版本化侧载升级细节见 [docs/versioning.md](docs/versioning.md)。
开发期 TSF DLL 高频迭代可用 `scripts\dev_reload.ps1`（免重启/改版本号，见 [docs/windows.md](docs/windows.md) §5）。

## 4. 深入阅读（按需查阅，不必全量载入上下文）

| 主题 | 文档 | 何时查 |
|------|------|--------|
| 内核状态机 / 顶字 / 词库 / 统计 / 按键处理 | [docs/core.md](docs/core.md) | 改 `core/` 的 handler 链、DictManager、Statistics、KeyEvent |
| 查字进程分离 / IPC 字节级协议 / 配置收敛到 dictd | [docs/ipc.md](docs/ipc.md) | 改 `ipc/`、`windows/dictd/`、`DictIpcProxy`、config.json 热加载 |
| TSF COM / 候选窗 GDI+ / 配置界面 / 图标 / 平台差异 | [docs/windows.md](docs/windows.md) | 改 `windows/` 任意子目录、用户级输入法注册 |
| 构建 / 版本号管理 / 安装包 | [docs/versioning.md](docs/versioning.md) | 构建、发版、版本号递增、安装脚本 |

面向终端用户的功能介绍与安装说明见 [README.md](README.md)。
