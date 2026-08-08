# Windows 层落地要点与平台差异（windows/）

> 本文是 WinFire Windows 平台层（TSF / 候选窗 / 配置界面 / 图标）的实现参考文档。
> 按需阅读——日常任务只需遵循 [AGENTS.md](../AGENTS.md) 的红线规则即可；
> 涉及 TSF COM、候选窗 GDI+、配置界面 PropertySheet、用户级输入法注册时再查本文。

## 1. TSF TIP（ATL 纯原生 COM，windows/tsf/）
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
- **图标资源**（`fire_tsf.rc` + `Resource.h`）：把 `resources/icons/winfire.ico` 嵌入 DLL 为
  `IDI_FIRE_TSF_ICON`（DLL 内首个 ICON 资源）。一处嵌入两处使用：
  - **注册表图标**：`DllRegisterServer` 的 `AddLanguageProfile` 把本 DLL 路径作为 `IconFile`、
    `IconIndex=0`，系统据此从 DLL 取 index 0 的 ICON 资源，显示在输入法列表 / 设置面板；
  - **运行时图标**：`CFireLangBarButton::GetIcon()` 用 `LoadImageW(g_hInst, MAKEINTRESOURCEW(IDI_FIRE_TSF_ICON), IMAGE_ICON, 0,0, LR_DEFAULTSIZE)`
    加载，作为任务栏托盘 / 输入指示器图标（`LR_DEFAULTSIZE` 按系统 SM_CXICON 取尺寸，高 DPI 自适应）。
- 输入统计：DLL 端不直接持 `Statistics`，上屏事件经 `IDictService` 回调→`DictIpcProxy`→IPC→
  `fire_dictd.exe` 写库（`RecordStat` 异步 fire-and-forget）。
- 按应用输入模式：`OnSetFocus(ITfDocumentMgr*)` 按 `bundle_id()`（宿主 exe 名）做 restore/save。
- 全量配置：`LoadConfigFromDisk` 调用 `firecfg::ConfigStore::Load` 读取 `config.json`（仅 Activate
  bootstrap 用一次，之后 config 全部经 IPC 从 dictd 拿，dictd 是唯一真相源，详见 [ipc.md](./ipc.md)）。
- **配置热加载（零轮询）**：`OnKeyDown` 入口调用 `MaybeReloadConfig`，**每 60s 最多发一次**
  `ValidateCache` IPC（不读盘）。dictd 比对 `config_token`，不一致时响应里带全量 `config_json`，
  DLL 经 `ConfigStore::LoadFromString` 原地填 `config_`（`InputEngine`/`PunctuationConverter`
  持引用，即见，无需重建）。权衡：改完配置最多等 60s（下次打字）生效，但**零磁盘 IO**（旧方案
  每 60s 一次 `GetFileAttributesExW` stat 已删除）。
- **DLL 本地缓存校验**：`InitEngine` 握手成功后调用 `DictIpcProxy::ValidateCache()` 获取 dictd
  的 token + config_token + 全量 config（首次 client_config_token=0 强制全量）；`MaybeReloadConfig`
  节流到期也调用 `ValidateCache()`。DLL 据候选 token 清空本地 LRU、据 config_json 更新 config_。
- **SEH 崩溃保护**：`ActivateEx` 通过 `InitEngineSafe()`（`__try/__except` 包裹 `InitEngine`）
  防止引擎初始化崩溃导致宿主进程（QQ/Word/Chrome…）整体挂掉；崩溃时记录日志并返回 `E_FAIL`，
  宿主进程会优雅降级为不加载输入法。
- **数据目录布局**（命名统一后）：
  - 程序文件：`%ProgramFiles%\WinFire\`（fire_tsf.dll、fire_config.exe、tables\）
  - 用户数据：`%APPDATA%\WinFire\`（config.json、user-dict.txt、wb_py_dict.sqlite、statistics.sqlite）
  - 调试日志（仅 Debug 版）：`%LOCALAPPDATA%\WinFire\logs\fire_tsf_<pid>.log`
- **用户级输入法安装（出现在「替代默认输入法」下拉）**：`DllRegisterServer` 在系统级注册（HKLM
  的 CTF\TIP + Category）之后，额外调用 `EnableLanguageProfile` / `EnableLanguageProfileByDefault`
  与 `input.dll!InstallLayoutOrTip`（动态 `LoadLibrary`，签名 `HRESULT WINAPI(LPCWSTR, DWORD)`，
  字符串格式 `"<langid十六进制>:<CLSID{大括号}><Profile{大括号}>"`，如 `0804:{...}{...}`），
  把 TIP 写入**当前用户**输入法列表（HKCU）。Windows「设置 → 替代默认输入法」下拉枚举的正是
  用户级列表，仅系统级注册不会出现在下拉里。`DllUnregisterServer` 与 `CleanupStaleRegistrations`
  对称调用 `InstallLayoutOrTip(..., ILOT_UNINSTALL=1)` 清理。**`ILOT_UNINSTALL` 不可靠的兜底**：
  该 API 在部分 Windows 版本上无法移除 `HKCU\Software\Microsoft\CTF\SortOrder\AssemblyItem` 与
  `HKCU\Control Panel\International\User Profile` 中的 WinFire 条目，导致卸载后系统设置残留
  「不可用的输入法」。`DllRegisterServer`/`DllUnregisterServer` 额外调用 `CleanupSortOrderAssemblyItems()`
  与 `CleanupUserProfileInputMethods()` 直接扫描注册表删除匹配 WinFire 基 GUID 前缀的条目
  （Register 时先清全部再由 `InstallLayoutOrTip(0)` 重新添加当前版本；Unregister 时全清）。
  `cleanup_now.ps1` / `uninstall.ps1` / `winfire.iss` 均含对称的 PowerShell 兜底清理
  （防 DLL 已删时 `regsvr32 /u` 失败）。**管理员≠登录用户的限制**：regsvr32
  以调用者身份运行，`InstallLayoutOrTip` 写入调用者的 HKCU；若以管理员账号提权安装而管理员 ≠
  登录用户，需登录用户在「设置 → 语言 → 添加键盘」手动添加一次以触发当前用户安装（此为 TSF
  已知限制，weasel 同样存在，不在代码层解决）。

## 2. 候选窗（Win32 + GDI+ 自绘，windows/candidate_window/）
- 无焦点浮窗：`WS_POPUP`，扩展样式 `WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW`，
  `WM_MOUSEACTIVATE` 返回 `MA_NOACTIVATE`。
- GDI+ 绘制组字区、候选列表（横/竖可切换）、序号、编码、翻页指示（上下箭头）；配色取自 `ThemeConfig`。
- **主题与深色模式**：配色/字号/内边距/圆角全部来自 `config_.theme.appearance(darkMode_)`。
  `darkMode_` 由 `ResolveDarkMode()` 解析：`dark_mode_preference`（0=跟随系统 / 1=浅色 / 2=深色）；
  跟随系统时读注册表 `HKCU\...\Themes\Personalize\AppsUseLightTheme`。主题随 `config.json` 经现有
  `config_json` IPC 字段下发（详见 `docs/ipc.md`），DLL 不直接读主题文件。
- **Font 缓存**：`Measure` 与 `PaintToGraphics` 经 `GetCachedFont(which, dpi)` 共用三档字号字体
  （text=`font_size` / index=`index_font_size` / code=`code_font_size`），仅在 `(font_name, pixel_size, dpi)`
  变化时重建，避免每次候选刷新构造 GDI+ 字体对象（字体加载较重）。
  `Destroy()` 在 `GdiplusShutdown` 前 reset 缓存，保证释放顺序正确。
- **v2 渲染增强**：选中项（首个候选）按 `selected_background_color`（非全透）+ `candidate_radius` 画圆角背景；
  每个候选按 `candidate_padding_*` 包裹；原码区按 `origin_padding_*` 包裹。`enable_liquid_glass`
  在 Windows 无 `NSVisualEffectView` 等价物，渲染层忽略（保持纯色背景）。
- **页面指示器**：`list.size()>1 || hasPrev || hasNext` 时绘制上下箭头（大小 = `font_size*0.5`，
  GDI+ 自绘纯色三角形，等价于业火的 template image 渲染）。横向布局竖排放候选列表右侧，竖向横排放下方。
  颜色用 `page_indicator_color` / `page_indicator_disabled_color`；不可用方向（!hasPrev/!hasNext）点击忽略。
- 定位：根据 `CaretRect` 放在光标下方，越界时翻转到上方/贴边。
- **DPI 自适应**：`Measure` 阶段调用 `GetDpiForWindow` 取系统缩放，所有尺寸/字号按 DPI scale 倍率计算，
  避免 HiDPI 屏下候选窗过小。
- **候选个数可配置**：3-9，由 `config.candidate_count` 控制。
- **翻页**：`-` / `=` 或 PageUp / PageDown，由 handler 链的 `pageKey` 段处理。
- **⚙ 菜单图标**（仅反查模式）：齿轮图标只在反查模式（`IsReverseLookup`：`z_key_query` 开启且
  `original_string` 以 `` ` `` 开头）下绘制在候选列表末尾，点击启动 `fire_config.exe`（`ShellExecute`
  自动定位同目录配置工具）。普通候选视图不显示，避免干扰；非反查模式下 `menuRect_` 留零矩形，
  `HitTest` 也不命中（带非空校验防止左上角误命中）。

## 3. 配置界面（纯 Win32 PropertySheet，windows/config/）
- **UI 框架**：纯 Win32 `PropertySheetW` + `PROPSHEETPAGE` + 通用控件（ListView/ComboBox/Edit/Button），
  无 MFC 依赖。`UiBase.h` 提供 `PageBase` 基类（封装 HWND + OnInit/OnApply/OnCommand/OnNotify 钩子）、
  UTF-8/UTF-16 互转（`Utf8ToWide`/`WideToUtf8`，替代 MFC 的 `CA2W`/`CT2A`）和控件读写辅助。
- **对话框过程**：`PageDlgProc` 通用回调，通过 `PROPSHEETPAGE.lParam` 携带 `PageBase*`，
  `WM_INITDIALOG` 时存到 `DWLP_USER`，后续消息转发给派生类。
- **资源脚本**：`ConfigApp.rc` 用 `<winres.h>` 替代 `<afxres.h>`，对话框模板与 MFC 版完全兼容。
  含 `IDI_WINFIRE ICON "..\..\resources\icons\winfire.ico"`（ID 101），嵌入 EXE 后用于
  PropertySheet 标题栏、Alt+Tab、任务栏图标。
- **静态链接**：`/MT` 编译，链接 `shell32.lib`/`advapi32.lib`/`comctl32.lib`/`comdlg32.lib`，
  无外部 DLL 依赖。Release 开启 /O2+/Os+LTCG 优化与死代码消除（`fire_config.vcxproj` 内注释）；
  sqlite 经 wrapper（`fire_sqlite3_amalg.c` + `fire_sqlite_compile_options.h` 裁剪宏）编译进本
  EXE——统计页 `CStatisticsPage` 直接持 `fire::Statistics` 读 `statistics.sqlite`。EXE 体积约
  1.0MB（MFC 静态版约 7.4MB）。
- **坑点**：`PROPSHEETHEADER.dwFlags` 用 `phpage` 数组（已 `CreatePropertySheetPage` 创建的句柄）时
  **不能**带 `PSH_PROPSHEETPAGE`（该标志表示用 `ppsp` 结构数组，会让 PropertySheet 把 `phpage` 当指针解引用导致 0xC0000005）。
- 属性页 / Tab：输入设置、标点与中英文、按应用模式、输入统计、词库管理、主题。
  - 输入设置：词组/动态调频/反查/显示编码/五笔编码提示/唯一候选自动上屏/候选个数/编码方案/候选方向/顶字/中英切换键。
  - 标点与中英文：标点模式（中文 / 英文）、中英切换键。
  - 按应用模式：`keep_app_input_mode`、模式提示时机、应用固定输入模式列表（增删）。
  - 输入统计：统计开关、累计字数、不同词条数、字词频列表、清除/仅清字词频/导出 CSV。
  - **词库管理**：导入码表（选择 `wb_table.txt` / `wb_98_table.txt` / `py_table.txt`）、
    重建词库（调用 `tablebuilder` 生成 `wb_py_dict.sqlite`）、编辑用户词库（`user-dict.txt`）。
  - **主题**：列出 `%APPDATA%\WinFire\themes\*.json` 主题库（与业火主题格式兼容，v1+v2）；
    选择应用 / 导入业火主题文件 / 导出当前主题 / 删除（默认主题禁删）；深色模式偏好
    （跟随系统 / 浅色 / 深色）。活动主题内联在 `config.json` 的 `theme` 段（唯一真相源），
    经现有 `config_json` IPC 下发，DLL 不读主题文件。
- **config 经 IPC（不直接读写 config.json）**：打开走 `IpcGetConfig`（拉 dictd 全量 config +
  数据文件路径），保存走 `IpcSetConfig`（委托 dictd 原子写 + 热重载）。DictPage 重建词库后立即
  `IpcSetConfig(reinit_dict=true)`，编辑 user-dict 后 OK 时带 `reload_user_dict=true`。dictd 不可用
  时降级 `ConfigStore::Load/Save` 直读写（兜底）。调用 `tablebuilder` 生成 `wb_py_dict.sqlite`；
  user-dict.txt 直接读写文件（路径从 GetConfig 响应取）。详见 [ipc.md](./ipc.md)。

## 4. 图标资源（resources/icons/）
单一图标源 `winfire_icon.svg`（128×128 母版）经 `render_winfire_icons.py`（依赖 Pillow + numpy）
按 SVG 参数重绘为各尺寸 PNG（16/24/32/48/256），合成为单个 `winfire.ico`（多帧）。该 ico 同时被
`fire_tsf.dll`（§1，注册表 + 语言栏）和 `fire_config.exe`（§3，标题栏/Alt+Tab）嵌入使用，
保证全链路图标一致。改图标流程：编辑 `winfire_icon.svg` → 跑 `render_winfire_icons.py` 重新生成
ico/PNG → 重新构建 DLL/EXE（资源由 `.rc` 引用，编译期嵌入，无需改代码）。

## 5. 开发热重载（scripts/dev_reload.ps1）
开发期高频迭代 TSF DLL 的辅助脚本：Build → 部署为 `%ProgramFiles%\WinFire\fire_tsf_DEV.dll`
（带 `-DEV` 后缀，避开正式版本化文件名）→ 刷新 TSF 宿主，**无需重启 / 注销 / 改版本号**。
需管理员权限；`-SkipBuild` 跳过编译直接用现成二进制。一并部署 `fire_config.exe` / `fire_dictd.exe` /
`tablebuilder.exe`。这是除 [versioning.md](./versioning.md) 版本化侧载升级外的第二条快速验证路径，仅限本机开发使用。

## 6. 平台差异与风险

- 进程模型：macOS IMK 独立进程共享状态；Windows TSF 是 in-process DLL，每个宿主进程各自加载。
  共享状态建议通过后台服务或共享文件 + 文件监听同步（初版可只读共享词库，写入串行化）。
- SQLCipher -> SQLite：初版用普通 SQLite；如需加密再引入 SQLCipher。
- UTF-8 / UTF-16 边界：内核统一 UTF-8；TSF/GDI+/Win32 边界处与 wchar_t(UTF-16) 互转（`firecfg::Utf8ToWide`/`WideToUtf8`）。
- UI 不要求 1:1 复刻，主要逻辑保持一致。
