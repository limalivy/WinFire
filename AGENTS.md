# WinFire — 微火五笔输入法 Windows 移植（移植自业火五笔 Fire）

将 macOS 上的「业火五笔输入法（Fire）」（Swift + InputMethodKit + SwiftUI + SQLCipher）
重新实现到 Windows 平台。核心策略是**抽离平台无关内核**（状态机 + 词库），
Windows 平台各层只做「宿主适配 + UI 渲染」。

## 1. 总体架构

- **TSF COM 骨架 → ATL（纯原生 COM）**：`windows/tsf/`
- **候选窗 → 纯 VC/Win32 + GDI+ 自绘**：`windows/candidate_window/`
- **状态机 + 词库 → 现代 C++17 跨平台内核**：`core/`
- **配置界面 → 纯 Win32（PropertySheet API）**：`windows/config/`
- **词库构建工具**：`tablebuilder/`

宿主进程（Word / Chrome / Notepad …）加载 `fire_tsf.dll`（TSF Text Input Processor，
ATL 纯原生 COM）。DLL 内 TSF 适配层把 Windows 消息翻译成 `fire::KeyEvent`，交给
`fire_core`（跨平台 C++17 内核）处理，内核通过 `fire::InputClient` 回调驱动组字区、
上屏、以及候选窗（Win32 + GDI+ 自绘的无焦点浮窗）。配置界面 `fire_config.exe`（纯 Win32）
读写 `config.json` / `user-dict.txt`，并调用 `tablebuilder` 生成 `wb_py_dict.sqlite`。

## 2. 目录结构

```
winFire/
├── CMakeLists.txt              # 仅构建跨平台内核 + 测试 + tablebuilder（macOS 可验证）
├── AGENTS.md                   # 本文档
├── README.md                   # 项目说明（含致谢与功能介绍）
├── VERSION.default            # 版本号基线（纳入 git，仅正式发版递增；工作版 VERSION 不跟踪，详见 §5.4）
├── core/                       # 跨平台内核（纯 C++17，不依赖 Windows / macOS API）
│   ├── include/fire/
│   │   ├── types.h             # 枚举 + 默认标点表           <- Fire/types.swift
│   │   ├── candidate.h         # Candidate / CandidateType   <- Fire/types.swift
│   │   ├── config.h            # 运行时配置 Config / Theme    <- Defaults.Keys / ThemeConfig
│   │   ├── key_event.h         # 平台无关按键事件            <- NSEvent 抽象
│   │   ├── input_client.h      # 宿主交互接口                <- IMK client()
│   │   ├── punctuation.h       # 标点转换 + 成对状态机       <- PunctuationConversion.swift
│   │   ├── dict_manager.h      # 词库查询                    <- DictManager.swift
│   │   ├── input_engine.h      # 状态机（16段handler链）      <- FireInputController.swift + Fire.swift
│   │   ├── input_mode_cache.h  # 按应用输入模式 LRU 缓存      <- InputModeCache.swift
│   │   ├── statistics.h        # 输入统计（SQLite）           <- Utils/Statistics.swift
│   │   ├── dict_service.h      # 查字/统计抽象接口 IDictService（本地/IPC 两种实现）
│   │   └── dict_local_impl.h   # IDictService 本地实现（直接持 DictManager+Statistics）
│   └── src/                    # 对应实现
├── ipc/                        # 查字进程分离 IPC 协议（纯 C++17，跨平台，入 CMake 可测）
│   ├── include/fire/ipc/
│   │   ├── wire.h              # payload 二进制读写 Writer/Reader（小端定长 + 越界检查）
│   │   └── protocol.h          # 帧头(16B) + 各消息 encode/decode + MsgType 枚举
│   └── src/                    # wire.cpp / protocol.cpp
├── tablebuilder/main.cpp       # 码表 txt -> sqlite           <- TableBuilder/main.cpp
├── tests/                      # 内核单元测试（极简框架 + ctest）
│   ├── test_util.h             # 断言宏 + 测试词库 + FakeClient
│   ├── test_main.cpp
│   ├── test_punctuation.cpp
│   ├── test_dict.cpp
│   ├── test_engine.cpp
│   ├── test_input_mode_cache.cpp
│   ├── test_statistics.cpp
│   └── test_ipc_protocol.cpp   # IPC 协议编解码往返测试
├── windows/                    # Windows 平台层
│   ├── tsf/                    # ATL TSF TIP DLL（fire_tsf.dll，含 DictIpcProxy + NamedPipeClient）
│   ├── candidate_window/       # Win32 + GDI+ 候选窗
│   ├── config/                 # 纯 Win32 配置界面（fire_config.exe，PropertySheet + GDI）
│   ├── dictd/                  # 后台查字进程 fire_dictd.exe（命名管道 server + DictManager+Statistics）
│   └── common/                 # DLL 与后台共用的 Win32 IPC 常量/工具（IpcShared.h）
├── installer/                  # Inno Setup 脚本与预构建资源
│   ├── winfire.iss             # 安装包脚本
│   └── staging/                # 预构建词库 + 默认 config.json（随包分发）
├── scripts/                    # PowerShell 构建/安装/卸载脚本
│   ├── build_installer.ps1     # 一键编译 + 生成 WinFire-Setup.exe
│   ├── install.ps1             # 直接部署（不走 installer，需管理员）
│   └── uninstall.ps1           # 反注册 + 删除程序文件（用户数据可选）
├── resources/                  # 内置码表（86 版 / 98 版五笔 + 拼音）
└── third_party/sqlite3/        # sqlite3 源码（直接编译进 DLL/EXE）
```

## 3. macOS -> 跨平台内核 模块映射

| macOS（Swift）                        | 内核（C++）                              | 说明 |
|---------------------------------------|------------------------------------------|------|
| `types.swift` 枚举/标点表             | `types.h/.cpp`                           | 枚举同构，标点表 `default_punctuation()` |
| `Candidate` / `CandidateType`         | `candidate.h/.cpp`                       | 值语义 + `operator==` |
| `Defaults.Keys` / `ThemeConfig`       | `config.h`                               | 内核只读，持久化由外层 JSON 负责 |
| `NSEvent`                             | `key_event.h/.cpp`（`KeyEvent`）         | 平台层把 VK_* 翻译为 `KeyEvent` |
| `IMKInputController.client()`         | `input_client.h`（`InputClient` 纯虚）    | TSF 层实现 |
| `PunctuationConversion.swift`         | `punctuation.h/.cpp`                      | 引号/方括号成对状态机 |
| `DictManager.swift`                   | `dict_manager.h/.cpp`                     | SQLite glob 前缀查询、分页、LRU 缓存、动态调频、用户词库 |
| `FireInputController.swift` + `Fire.swift` | `input_engine.h/.cpp`（`InputEngine`）| 16 段 handler 链 + 顶字状态机 + `getCandidates`/`toggleInputMode` |
| `Utils.shouldConcatWithWhitespace`    | `InputEngine::should_concat_with_whitespace` | 中英文间自动加空格 |
| `ModifierKeyUpChecker.swift`          | `KeyEvent.toggle_input_mode_request`      | 修饰键单击时序检测下沉到平台层 |
| `InputModeCache.swift`                | `input_mode_cache.h/.cpp`（`InputModeCache`） | 按应用输入模式 LRU 缓存（cap=100） |
| `Utils/Statistics.swift`              | `statistics.h/.cpp`（`Statistics`）       | 输入统计 SQLite（去 SQLCipher/异步，改同步写入） |
| `FireInputServer.swift`（per-app）    | `InputEngine::restore/save_input_mode_for_app` | 按应用恢复/保存输入模式 |
| `TableBuilder/main.cpp`               | `tablebuilder/main.cpp`                   | 词库构建 |

## 4. 内核关键设计

### 4.1 按键抽象 KeyEvent
- 可见字符统一走 `text`（UTF-8），功能键走 `SpecialKey`，修饰键位为布尔字段。
- 中英文切换的「修饰键单击」时序检测（原 `ModifierKeyUpChecker`）不在内核，
  由平台层完成后置位 `toggle_input_mode_request`，内核只据此调用 `toggle_input_mode()`。

### 4.2 handler 链（16 段，顺序即优先级）
hotkey -> capsLock -> flagChanged -> enMode -> predictor -> pageKey -> deleteKey ->
wubi52Ding -> wubi53Ding -> wubi35Ding -> charKey -> numberKey -> escKey -> enterKey ->
spaceKey -> punctuation

每个 handler 返回 `std::optional<bool>`：有值表示已决定是否消费（true=消费，false=透传），
链终止；无值（`std::nullopt`）继续下一个 handler。`handle_key()` 用成员函数指针数组按序调用。

### 4.3 顶字状态机
- 35 顶：3 码后继续输入默认顶上屏首候选；3 码后按空格声明「继续输入第 4 码」
  （空格仅作占位状态，不插入编辑框），候选窗缓存区展示下划线占位 `abc_`。
- 52 顶：4 码时把候选优先展示为「前 2 码首候选 + 后 2 码首候选」的 2+2 组合。
- 53 顶：4 码时优先展示「前 3 码首候选 + 第 4 码首候选」的 3+1 组合。
- 组合候选上屏时按拆分后的词条分别做字频统计（`hanzi_frequency_parts`）。

### 4.4 词库 DictManager
- `wb_py_dict(id, wbcode, text, type, query)`，`query glob :queryLike` 前缀匹配（`PRAGMA case_sensitive_like=ON`）。
- 1-3 码首屏结果走 LRU 缓存（countLimit=5000）。
- `query.count >= 4` 时应用动态调频（把记忆的首选提到第一位）。
- 反引号 + 拼音反查形码；`;` 前缀临时英文占位候选。

### 4.5 组字区显示
- `show_code_in_window` 开启时组字区只放一个占位空格，编码显示在候选窗；关闭时组字区直接展示输入码。

### 4.6 按应用输入模式（per-app）
- `InputModeCache`：LRU（cap=100），`app_id -> InputMode`；命中刷新访问顺序，超容量淘汰最久未用。
- `restore_input_mode_for_app`：先看 `app_settings` 固定设置（ZhHans/EnUS 直接切；RecentUsed 落缓存），
  再在 `keep_app_input_mode` 开启时读缓存；返回是否发生模式变化。
- `save_input_mode_for_app`：仅在 `keep_app_input_mode` 且该应用无固定设置时写缓存。
- 平台层（TSF）在焦点切换（`ITfThreadMgrEventSink::OnSetFocus`）时按宿主进程名做 save/restore。

### 4.7 输入统计 Statistics
- SQLite 库（`data` / `meta` / `hanzi_freq` / `word_freq` 表，`PRAGMA user_version` 迁移），同步写入。
- `record_candidate`：`enable_stats` 写打字量（`data`），`enable_hanzi` 写字词频（`word_freq`）；
  顶字组合按 `hanzi_frequency_parts` 拆分计数。
- 查询：累计字数、按日期区间、字词频列表（可按 app 聚合）、不同词条数、出现过的应用列表。
- 维护：清除全部 / 仅清字词频 / 导出 CSV（含 BOM，列「应用ID,词,次数」）。
- 引擎通过 `set_candidate_inserted_callback(CandidateInsertedInfo)` 把上屏事件回灌给统计。

### 4.8 查字进程分离（Dict IPC，AppContainer 适配）
- **动机**：TSF DLL 会被加载进 SearchHost.exe / UWP 等 AppContainer 沙箱进程，沙箱无权
  读写 `%APPDATA%` 下词库/统计库，导致「开始菜单搜索」等场景无候选。方案：把查库/统计
  下沉到一个正常完整性级别（IL）的后台进程 `fire_dictd.exe`，DLL 端经 IPC 转发。
- **接口抽象**：`IDictService`（`core/include/fire/dict_service.h`）纯虚接口，`InputEngine`
  只依赖它。两种实现：
  - `DictLocalImpl`（内核内，直接持 `DictManager`+`Statistics`）——非沙箱进程/后台不可用时回退。
  - `DictIpcProxy`（`windows/tsf/`）——把调用编码成 IPC 请求转发给 `fire_dictd.exe`。
- **协议**（`ipc/`，纯 C++17 入 CMake 可测）：16 字节定长帧头（magic `0x57464452`"WFDR" /
  version u16=1 / msg_type u16 / request_id u32 / payload_len u32）+ 手写小端二进制 payload
  （`Writer/Reader`，越界检查）。MsgType：`0x01` Hello、`0x02` QueryCandidates、`0x03`
  ReverseLookup、`0x04` RememberFreq、`0x05` SetCandidateFirst、`0x06` PrependCandidate、
  `0x07` GetUserCandidates、`0x08` RecordStat、`0x09` Reinit、`0xFF` Error。
- **传输**：命名管道 `\\.\pipe\WinFire_Dict_<会话id>`（`PIPE_TYPE_MESSAGE`），SDDL
  `D:(A;;GRGW;;;WD)(A;;GRGW;;;AC)S:(ML;;NW;;;LW)` 放开 AppContainer + 低 IL 访问。
  server/client 均用 `FILE_FLAG_OVERLAPPED` 创建，所有读写走 overlapped + 超时。
  查询同步（20ms 超时，超时/失败即回退空结果并标记重连）；统计/调频/Reinit 异步
  fire-and-forget。
- **后台生命周期**：单实例 mutex `Global\WinFire_Dictd_<会话id>`；空闲超时退出（每轮
  30s，连续 20 轮约 10 分钟无连接自动退出）；由安装脚本/正常 IL 进程拉起（AppContainer
  进程通常无权 CreateProcess，故不能只靠 DLL 端按需拉起）。DLL 端 `DictIpcProxy` 首次
  连不上会尝试 `CreateProcessW` 拉起同目录 `fire_dictd.exe` 并重试（非沙箱场景兜底）。
- **数据路径与权限**：安装时 `icacls "{app}" /grant *S-1-15-2-1:(OI)(CI)(RX) /T`（授予
  ALL APPLICATION PACKAGES 读/执行）；后台以正常 IL 读写用户数据库，绕开沙箱写限制。

## 5. 构建与验证

### 5.1 跨平台内核（CMake，可在 macOS / Linux 验证）

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

### 5.2 Windows 层（MSBuild，需 VS2022 + ATL + Windows SDK 10）

> 注：fire_config 已从 MFC 迁移到纯 Win32（PropertySheet API），不再需要 MFC 组件。
> fire_tsf.dll 仍需 ATL（纯原生 COM）。

```powershell
# 编译 TSF TIP DLL（fire_tsf.dll，含 DictIpcProxy + NamedPipeClient + ipc/ 协议）
MSBuild.exe windows\tsf\fire_tsf.vcxproj /p:Configuration=Release /p:Platform=x64

# 编译配置工具 EXE（fire_config.exe）
MSBuild.exe windows\config\fire_config.vcxproj /p:Configuration=Release /p:Platform=x64

# 编译后台查字进程 EXE（fire_dictd.exe，命名管道 server + DictManager+Statistics）
MSBuild.exe windows\dictd\fire_dictd.vcxproj /p:Configuration=Release /p:Platform=x64

# 构建 tablebuilder.exe（生成预构建词库用）
cmake -S . -B build -DBUILD_TABLEBUILDER=ON
cmake --build build --target tablebuilder --config Release
```

注意：`windows/config/ConfigApp.rc` 含 UTF-8 中文，文件首行已加 `#pragma code_page(65001)`
声明，避免 rc.exe 用系统 GBK 误解析。

### 5.3 安装包（Inno Setup 6）

```powershell
# 一键流程：MSBuild 编译 → tablebuilder 预构建词库 → ISCC 编译 winfire.iss
powershell -ExecutionPolicy Bypass -File scripts\build_installer.ps1

# 仅编译 installer（跳过 VS 编译，使用现有产物）
powershell -ExecutionPolicy Bypass -File scripts\build_installer.ps1 -SkipBuild
```

产物：`dist\WinFire-Setup.exe`（单文件 installer，含卸载器）。
安装目标：`%ProgramFiles%\WinFire\`（程序文件） + `%APPDATA%\WinFire\`（用户数据）。

### 5.4 版本号管理（单一来源 + 强制约束）

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

**强制约束（务必遵守）**：

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

**更新版本号操作步骤**：

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

## 6. Windows 层落地要点

### 6.1 TSF TIP（ATL 纯原生 COM，windows/tsf/）
- 实现 `ITfTextInputProcessorEx`、`ITfKeyEventSink`、`ITfCompositionSink`、`ITfThreadMgrEventSink`。
- DLL 导出 `DllRegisterServer` 注册 CLSID + Category + 语言 Profile（zh-CN）。
- `OnKeyDown`/`OnTestKeyDown` 把 VK_* + 修饰键状态翻译为 `fire::KeyEvent`，交给
  `InputEngine::handle_key()`；返回值决定 `pfEaten`。
- 用 `ITfComposition` + `ITfInsertAtSelection` 实现组字区与上屏。
- 实现 `fire::InputClient`：组字/上屏、`get_caret_rect`（`GetTextExt`）、`get_previous_text`、
  `bundle_id`（宿主进程名）、`show_candidates`/`hide_candidates`（驱动候选窗）。
- 中英文切换：`ModifierKeyUpChecker` 等价实现，检测 Shift 单击后置位 `toggle_input_mode_request`。
- 语言栏按钮（`LangBarButton.h/.cpp`，`ITfLangBarItemButton` + `ITfSource`）替代 macOS 状态栏图标：
  显示「中/英」，左键切换、菜单直选中/英；tooltip 为「微火五笔：点击切换中/英文」；
  `Activate/Deactivate` 通过 `ITfLangBarItemMgr` 注册/注销。
- 输入统计：`Activate` 时若开启统计开关则创建 `fire::Statistics` 并注册引擎回调写库。
- 按应用输入模式：`OnSetFocus(ITfDocumentMgr*)` 按 `bundle_id()`（宿主 exe 名）做 restore/save。
- 全量配置：`LoadConfigFromDisk` 调用 `firecfg::ConfigStore::Load` 读取 `config.json`。
- **SEH 崩溃保护**：`ActivateEx` 通过 `InitEngineSafe()`（`__try/__except` 包裹 `InitEngine`）
  防止引擎初始化崩溃导致宿主进程（QQ/Word/Chrome…）整体挂掉；崩溃时记录日志并返回 `E_FAIL`，
  宿主进程会优雅降级为不加载输入法。
- **数据目录布局**（命名统一后）：
  - 程序文件：`%ProgramFiles%\WinFire\`（fire_tsf.dll、fire_config.exe、tables\）
  - 用户数据：`%APPDATA%\WinFire\`（config.json、user-dict.txt、wb_py_dict.sqlite、statistics.sqlite）
  - 调试日志（仅 Debug 版）：`%LOCALAPPDATA%\WinFire\logs\fire_tsf_<pid>.log`

### 6.2 候选窗（Win32 + GDI+ 自绘，windows/candidate_window/）
- 无焦点浮窗：`WS_POPUP`，扩展样式 `WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW`，
  `WM_MOUSEACTIVATE` 返回 `MA_NOACTIVATE`。
- GDI+ 绘制组字区、候选列表（横/竖可切换）、序号、编码、翻页指示；主题色取自 `ThemeConfig`。
- 定位：根据 `CaretRect` 放在光标下方，越界时翻转到上方/贴边。
- **DPI 自适应**：`Measure` 阶段调用 `GetDpiForWindow` 取系统缩放，所有尺寸/字号按 DPI scale 倍率计算，
  避免 HiDPI 屏下候选窗过小。
- **候选个数可配置**：3-9，由 `config.candidate_count` 控制。
- **翻页**：`-` / `=` 或 PageUp / PageDown，由 handler 链的 `pageKey` 段处理。
- **⚙ 菜单图标**：候选窗右上角绘制齿轮图标，点击启动 `fire_config.exe`（通过 `ShellExecute`，
  自动定位同目录下的配置工具），便于用户快速进入设置而无需到开始菜单查找。

### 6.3 配置界面（纯 Win32 PropertySheet，windows/config/）
- **UI 框架**：纯 Win32 `PropertySheetW` + `PROPSHEETPAGE` + 通用控件（ListView/ComboBox/Edit/Button），
  无 MFC 依赖。`UiBase.h` 提供 `PageBase` 基类（封装 HWND + OnInit/OnApply/OnCommand/OnNotify 钩子）、
  UTF-8/UTF-16 互转（`Utf8ToWide`/`WideToUtf8`，替代 MFC 的 `CA2W`/`CT2A`）和控件读写辅助。
- **对话框过程**：`PageDlgProc` 通用回调，通过 `PROPSHEETPAGE.lParam` 携带 `PageBase*`，
  `WM_INITDIALOG` 时存到 `DWLP_USER`，后续消息转发给派生类。
- **资源脚本**：`ConfigApp.rc` 用 `<winres.h>` 替代 `<afxres.h>`，对话框模板与 MFC 版完全兼容。
- **静态链接**：`/MT` 编译，链接 `comctl32.lib`/`comdlg32.lib`，无外部 DLL 依赖，
  EXE 体积约 2.7MB（MFC 静态版约 7.4MB，减 62.7%）。
- **坑点**：`PROPSHEETHEADER.dwFlags` 用 `phpage` 数组（已 `CreatePropertySheetPage` 创建的句柄）时
  **不能**带 `PSH_PROPSHEETPAGE`（该标志表示用 `ppsp` 结构数组，会让 PropertySheet 把 `phpage` 当指针解引用导致 0xC0000005）。
- 属性页 / Tab：输入设置、标点与中英文、按应用模式、输入统计、词库管理。
  - 输入设置：词组/动态调频/反查/显示编码/五笔编码提示/唯一候选自动上屏/候选个数/编码方案/候选方向/顶字/中英切换键。
  - 标点与中英文：标点模式（中文 / 英文）、中英切换键。
  - 按应用模式：`keep_app_input_mode`、模式提示时机、应用固定输入模式列表（增删）。
  - 输入统计：统计开关、累计字数、不同词条数、字词频列表、清除/仅清字词频/导出 CSV。
  - **词库管理**：导入码表（选择 `wb_table.txt` / `wb_98_table.txt` / `py_table.txt`）、
    重建词库（调用 `tablebuilder` 生成 `wb_py_dict.sqlite`）、编辑用户词库（`user-dict.txt`）。
- 读写 `config.json`；调用 `tablebuilder` 生成 `wb_py_dict.sqlite`；编辑 `user-dict.txt`。
- 注：主题设置、CLI 暂未实现。

## 7. 平台差异与风险

- 进程模型：macOS IMK 独立进程共享状态；Windows TSF 是 in-process DLL，每个宿主进程各自加载。
  共享状态建议通过后台服务或共享文件 + 文件监听同步（初版可只读共享词库，写入串行化）。
- SQLCipher -> SQLite：初版用普通 SQLite；如需加密再引入 SQLCipher。
- UTF-8 / UTF-16 边界：内核统一 UTF-8；TSF/GDI+/Win32 边界处与 wchar_t(UTF-16) 互转（`firecfg::Utf8ToWide`/`WideToUtf8`）。
- UI 不要求 1:1 复刻，主要逻辑保持一致。
