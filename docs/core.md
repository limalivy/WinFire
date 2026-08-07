# 内核设计与实现（core/）

> 本文是 WinFire 跨平台内核（纯 C++17，不依赖 Windows / macOS API）的设计参考文档。
> 按需阅读——日常任务只需遵循 [AGENTS.md](../AGENTS.md) 的红线规则即可；
> 涉及内核状态机、词库、统计、按键处理的具体实现时再查本文。

## 1. macOS → 跨平台内核 模块映射

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

## 2. 关键设计

### 2.1 按键抽象 KeyEvent
- 可见字符统一走 `text`（UTF-8），功能键走 `SpecialKey`，修饰键位为布尔字段。
- 中英文切换的「修饰键单击」时序检测（原 `ModifierKeyUpChecker`）不在内核，
  由平台层完成后置位 `toggle_input_mode_request`，内核只据此调用 `toggle_input_mode()`。

### 2.2 handler 链（16 段，顺序即优先级）
hotkey -> capsLock -> flagChanged -> enMode -> predictor -> pageKey -> deleteKey ->
wubi52Ding -> wubi53Ding -> wubi35Ding -> charKey -> numberKey -> escKey -> enterKey ->
spaceKey -> punctuation

每个 handler 返回 `std::optional<bool>`：有值表示已决定是否消费（true=消费，false=透传），
链终止；无值（`std::nullopt`）继续下一个 handler。`handle_key()` 用成员函数指针数组按序调用。

### 2.3 顶字状态机
- 35 顶：3 码后继续输入默认顶上屏首候选；3 码后按空格声明「继续输入第 4 码」
  （空格仅作占位状态，不插入编辑框），候选窗缓存区展示下划线占位 `abc_`。
- 52 顶：4 码时把候选优先展示为「前 2 码首候选 + 后 2 码首候选」的 2+2 组合。
- 53 顶：4 码时优先展示「前 3 码首候选 + 第 4 码首候选」的 3+1 组合。
- 组合候选上屏时按拆分后的词条分别做字频统计（`hanzi_frequency_parts`）。

### 2.4 词库 DictManager
- `wb_py_dict(id, wbcode, text, type, query)`，`query glob :queryLike` 前缀匹配（`PRAGMA case_sensitive_like=ON`）。
- 1-3 码首屏结果走 LRU 缓存（countLimit=5000，`list<pair> + map<iterator>` 实现，O(1) 提升/淘汰）。
- 内存 LRU 支持持久化快照：构造时从 `query_cache.bin` 加载上次会话积累，析构时全量写回（`SaveCacheStore`/`LoadCacheStore`）；数据库变更时整体删除（`clear_query_cache` 收口）。
- 每次 `clear_query_cache()` 递增 `user_cache_generation_` 代次，供 dictd 纳入 CacheValidate token 计算，DLL 据此失效本地缓存。
- `query.count >= 4` 时应用动态调频（把记忆的首选提到第一位）。
- 反引号 + 拼音反查形码；`;` 前缀临时英文占位候选。

### 2.5 组字区显示
- `show_code_in_window` 开启时组字区只放一个占位空格，编码显示在候选窗；关闭时组字区直接展示输入码。

### 2.6 按应用输入模式（per-app）
- `InputModeCache`：LRU（cap=100），`app_id -> InputMode`；命中刷新访问顺序，超容量淘汰最久未用。
- `restore_input_mode_for_app`：先看 `app_settings` 固定设置（ZhHans/EnUS 直接切；RecentUsed 落缓存），
  再在 `keep_app_input_mode` 开启时读缓存；返回是否发生模式变化。
- `save_input_mode_for_app`：仅在 `keep_app_input_mode` 且该应用无固定设置时写缓存。
- 平台层（TSF）在焦点切换（`ITfThreadMgrEventSink::OnSetFocus`）时按宿主进程名做 save/restore。

### 2.7 输入统计 Statistics
- SQLite 库（`data` / `meta` / `hanzi_freq` / `word_freq` 表，`PRAGMA user_version` 迁移），同步写入。
- `record_candidate`：`enable_stats` 写打字量（`data`），`enable_hanzi` 写字词频（`word_freq`）；
  顶字组合按 `hanzi_frequency_parts` 拆分计数。
- 查询：累计字数、按日期区间、字词频列表（可按 app 聚合）、不同词条数、出现过的应用列表。
- 维护：清除全部 / 仅清字词频 / 导出 CSV（含 BOM，列「应用ID,词,次数」）。
- 引擎通过 `set_candidate_inserted_callback(CandidateInsertedInfo)` 把上屏事件回灌给统计。

> 词库查询的进程分离、IPC 协议与配置收敛设计见 [ipc.md](./ipc.md)。
