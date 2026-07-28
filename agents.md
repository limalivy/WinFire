# my_win_input — 业火五笔输入法 Windows 移植

将 macOS 上的「业火五笔输入法（Fire）」（Swift + InputMethodKit + SwiftUI + SQLCipher）
重新实现到 Windows 平台。核心策略是**抽离平台无关内核**（状态机 + 词库），
Windows 平台各层只做「宿主适配 + UI 渲染」。

## 1. 总体架构

- **TSF COM 骨架 → ATL（纯原生 COM）**：`windows/tsf/`
- **候选窗 → 纯 VC/Win32 + GDI+ 自绘**：`windows/candidate_window/`
- **状态机 + 词库 → 现代 C++17 跨平台内核**：`core/`（本仓库唯一需要编译验证的模块）
- **配置界面 → MFC**：`windows/config/`
- **词库构建工具**：`tablebuilder/`

宿主进程（Word / Chrome / Notepad …）加载 `fire_tsf.dll`（TSF Text Input Processor，
ATL 纯原生 COM）。DLL 内 TSF 适配层把 Windows 消息翻译成 `fire::KeyEvent`，交给
`fire_core`（跨平台 C++17 内核）处理，内核通过 `fire::InputClient` 回调驱动组字区、
上屏、以及候选窗（Win32 + GDI+ 自绘的无焦点浮窗）。配置界面 `fire_config.exe`（MFC）
读写 `config.json` / `user-dict.txt`，并调用 `tablebuilder` 生成 `wb_py_dict.sqlite`。

## 2. 目录结构

```
my_win_input/
├── CMakeLists.txt              # 仅构建跨平台内核 + 测试 + tablebuilder（macOS 可验证）
├── agents.md                   # 本文档
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
│   │   └── statistics.h        # 输入统计（SQLite）           <- Utils/Statistics.swift
│   └── src/                    # 对应实现
├── tablebuilder/main.cpp       # 码表 txt -> sqlite           <- TableBuilder/main.cpp
├── tests/                      # 内核单元测试（极简框架 + ctest）
│   ├── test_util.h             # 断言宏 + 测试词库 + FakeClient
│   ├── test_main.cpp
│   ├── test_punctuation.cpp
│   ├── test_dict.cpp
│   ├── test_engine.cpp
│   ├── test_input_mode_cache.cpp
│   └── test_statistics.cpp
└── windows/                    # Windows 平台层（不参与 CMake，不做编译验证）
    ├── tsf/                    # ATL TSF TIP DLL
    ├── candidate_window/       # Win32 + GDI+ 候选窗
    └── config/                 # MFC 配置界面
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

## 5. 构建与验证

只有内核（`core/` + `tablebuilder/` + `tests/`）通过 CMake 构建，可在 macOS 上验证：

```bash
cmake -S . -B build
cmake --build build
cd build && ctest --output-on-failure
```

CMake 选项（默认 ON）：`BUILD_CORE` / `BUILD_TESTS` / `BUILD_TABLEBUILDER`。
sqlite3 从系统 SDK / homebrew 定位（`find_package(SQLite3)`，回退 `-lsqlite3`）。

Windows 层（`windows/`）不参与 CMake，不做编译验证——需在 Windows + Visual Studio
（含 ATL/MFC 组件、Windows SDK）中用各自的工程构建。

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
  显示「中/英」，左键切换、菜单直选中/英；`Activate/Deactivate` 通过 `ITfLangBarItemMgr` 注册/注销。
- 输入统计：`Activate` 时若开启统计开关则创建 `fire::Statistics` 并注册引擎回调写库。
- 按应用输入模式：`OnSetFocus(ITfDocumentMgr*)` 按 `bundle_id()`（宿主 exe 名）做 restore/save。
- 全量配置：`LoadConfigFromDisk` 调用 `firecfg::ConfigStore::Load` 读取 `config.json`。

### 6.2 候选窗（Win32 + GDI+ 自绘，windows/candidate_window/）
- 无焦点浮窗：`WS_POPUP`，扩展样式 `WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW`，
  `WM_MOUSEACTIVATE` 返回 `MA_NOACTIVATE`。
- GDI+ 绘制组字区、候选列表（横/竖）、序号、编码、翻页指示；主题色取自 `ThemeConfig`。
- 定位：根据 `CaretRect` 放在光标下方，越界时翻转到上方/贴边。

### 6.3 配置界面（MFC，windows/config/）
- 属性页 / Tab：输入设置、标点与中英文、按应用模式、输入统计、词库管理（导入/构建）。
  - 输入设置：词组/动态调频/反查/显示编码/五笔编码提示/唯一候选自动上屏/候选个数/编码方案/候选方向/顶字/中英切换键。
  - 按应用模式：`keep_app_input_mode`、模式提示时机、应用固定输入模式列表（增删）。
  - 输入统计：统计开关、累计字数、不同词条数、字词频列表、清除/仅清字词频/导出 CSV。
- 读写 `config.json`；调用 `tablebuilder` 生成 `wb_py_dict.sqlite`；编辑 `user-dict.txt`。
- 注：主题设置、CLI 暂未实现。

## 7. 平台差异与风险

- 进程模型：macOS IMK 独立进程共享状态；Windows TSF 是 in-process DLL，每个宿主进程各自加载。
  共享状态建议通过后台服务或共享文件 + 文件监听同步（初版可只读共享词库，写入串行化）。
- SQLCipher -> SQLite：初版用普通 SQLite；如需加密再引入 SQLCipher。
- UTF-8 / UTF-16 边界：内核统一 UTF-8；TSF/GDI+/MFC 边界处与 wchar_t(UTF-16) 互转。
- UI 不要求 1:1 复刻，主要逻辑保持一致。
