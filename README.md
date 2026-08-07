# WinFire / 微火五笔输入法

> **致谢**：本项目移植自 macOS 平台的 [**业火五笔输入法（Fire）**](https://github.com/qwertyyb/Fire)（作者 @qwertyyb）。感谢原作者以 Swift + InputMethodKit + SwiftUI + SQLCipher 完整实现了一个现代化的五笔输入法，并将其开源。WinFire 的核心状态机、词库设计、标点成对状态机、顶字规则、按应用输入模式、输入统计等关键逻辑均源自 Fire 项目，仅做了 Swift → C++17 的语言转译与跨平台适配。**没有业火输入法的开源贡献，就没有 WinFire。**

## 项目简介

WinFire 是业火五笔输入法在 Windows 平台的非官方移植版。核心策略是**抽离平台无关内核**（状态机 + 词库，纯 C++17），Windows 平台各层只做「宿主适配 + UI 渲染」。

- **TSF COM 骨架**：ATL 纯原生 COM 实现
- **候选窗**：纯 Win32 + GDI+ 自绘
- **状态机 + 词库**：跨平台 C++17 内核（不依赖 Windows / macOS API）
- **配置界面**：纯 Win32（PropertySheet API + GDI 自绘控件，无 MFC 依赖）
- **词库构建工具**：独立 CMake 工程

## 特点

- **跨平台内核**：状态机、词库查询、标点转换、统计等核心逻辑以纯 C++17 实现，可在 macOS / Linux 上编译验证（`cmake + ctest`），便于回归测试。
- **平台层薄**：Windows 层只做按键翻译、组字区管理、UI 渲染，不包含业务逻辑。
- **原生 COM**：TSF TIP 使用 ATL 纯原生 COM，不依赖 .NET / WPF / Electron，加载到任意宿主进程（QQ / Word / Chrome…）都能稳定工作。
- **静态链接**：fire_tsf.dll / fire_config.exe / fire_dictd.exe 均静态链接 CRT（fire_tsf 另链接 ATL），避免与宿主进程的 CRT 版本冲突；fire_config.exe 不再依赖 MFC，安装包体积显著缩小。fire_tsf.dll 已移除 SQLite 依赖（查字/统计完全经 IPC 转发给 fire_dictd.exe 后台进程），DLL 体积进一步减小约 1MB。
- **数据隔离**：程序文件在 `%ProgramFiles%\WinFire\`，用户数据在 `%APPDATA%\WinFire\`，卸载默认保留用户数据。
- **查字进程分离**：TSF DLL 加载进 AppContainer 沙箱进程（SearchHost.exe / UWP）时无权读写用户数据目录，故所有查库/统计下沉到正常完整性级别的后台进程 `fire_dictd.exe`，DLL 经命名管道 IPC 转发。DLL 层维护本地 LRU 候选缓存（cap=1000），通过 `CacheValidate` IPC 协议由 dictd 裁决有效性，热路径命中缓存时 0 次 IPC 往返。后台进程常驻不退出，开机由注册表 Run 键自启动（保证系统重启后沙箱进程首次输入即可用）；dictd 意外退出时由 DLL 在首次查询时按需拉起。

## 当前程序功能

### 输入核心
- **五笔 86 / 98 编码方案**：可切换，码表内置
- **词组输入**：支持多字词，按词频排序
- **拼音反查**：`` ` `` 前缀切到拼音，输入拼音反查形码；候选项显示对应五笔编码
- **动态调频**：4 码及以上的查询，把记忆的首选提到第一位（可在配置中关闭）
- **唯一候选自动上屏**：输入码命中的候选只有 1 条时直接上屏，无需按空格
- **顶字状态机**：
  - **35 顶**：3 码后继续输入默认顶上屏首候选；3 码后按空格声明「继续输入第 4 码」
  - **52 顶**：4 码时把候选优先展示为「前 2 码首候选 + 后 2 码首候选」的 2+2 组合
  - **53 顶**：4 码时优先展示「前 3 码首候选 + 第 4 码首候选」的 3+1 组合
- **中英文间自动加空格**：中文与英文相邻时自动插入空格
- **标点成对状态机**：成对的引号 / 方括号自动成对输出（`“”`、`‘’`、`【】`、`《》`）

### 切换与模式
- **中英文切换**：默认 Shift 单击（可配置为 Ctrl / Alt）
- **Caps Lock 切换**：大写锁定时自动切英文模式
- **按应用记忆输入模式**：每个应用独立保存上次的输入模式（可关闭）
- **应用固定模式**：可为指定进程固定中/英文模式，不受全局模式影响

### 候选窗（Win32 + GDI+ 自绘）
- 无焦点浮窗：`WS_EX_NOACTIVATE | WS_EX_TOPMOST`，不抢宿主焦点
- 横排 / 竖排可切换
- 光标定位：自动放在光标下方，越界翻转到上方 / 贴边
- DPI 自适应：跟随系统缩放
- 显示内容：序号、候选词、五笔编码提示（可关闭，反查时强制显示）
- 候选个数：3-9 可配置
- 翻页：`-` / `=` 或 PageUp / PageDown
- ⚙ 菜单图标：仅在反查模式（`` ` `` 前缀）下出现，点击启动配置工具

### 语言栏按钮
- 显示「中 / 英」状态
- 左键切换中英文
- 右键菜单：直接选中 / 英模式
- Tooltip：「微火五笔：点击切换中/英文」
- 图标：输入法列表、设置面板、任务栏托盘统一显示微火火焰图标（嵌入 DLL/EXE 资源）

### 配置工具（fire_config.exe，纯 Win32）
属性页式界面，包含：
- **输入设置**：词组输入 / 动态调频 / 反查 / 候选窗内显示编码 / 五笔编码提示 / 唯一候选自动上屏 / 候选个数 / 编码方案 / 候选方向 / 顶字规则 / 中英切换键
- **标点与中英文**：标点模式（中文 / 英文）、按应用模式开关、应用固定模式列表
- **按应用模式**：`keep_app_input_mode`、模式提示时机、应用固定输入模式增删
- **输入统计**：统计开关、累计字数、不同词条数、字词频列表、清除 / 仅清字词频 / 导出 CSV
- **词库管理**：导入码表、重建词库、编辑用户词库（`user-dict.txt`）

### 输入统计
- SQLite 本地存储（`statistics.sqlite`），同步写入，无需联网
- 累计字数（按日期 / 应用聚合）
- 字词频统计（顶字组合按拆分后的字分别计数）
- 出现过的应用列表
- 导出 CSV（含 BOM，列「应用ID,词,次数」）
- 可一键清除全部 / 仅清字词频

### 安装与部署
- **Inno Setup 安装包**：`WinFire-Setup.exe`，单文件包含 DLL / 配置工具 / 后台进程 / 词库构建工具 / 码表 / 默认配置
- **词库安装时现场生成**：安装包不预构建词库，安装时由 `tablebuilder.exe` + 码表现场生成 `wb_py_dict.sqlite`（约 1 秒；已有词库则保留，失败不中断安装，可稍后在配置工具「词库管理」重建）
- **自动注册 TSF**：安装时静默 `regsvr32`，卸载时反注册
- **版本化侧载升级**：DLL 文件名带版本号（`fire_tsf_<版本>.dll`），新版以新 CLSID/Profile 侧载注册，无需强制关闭宿主进程；被占用的旧 DLL 标记为重启后删除。安装/卸载前自动结束 fire_dictd / fire_config / tablebuilder 三个独立 EXE，保证干净覆盖与卸载
- **数据保留**：卸载默认保留用户数据，弹窗询问是否一并删除
- **PowerShell 脚本**：`install.ps1` / `uninstall.ps1` / `build_installer.ps1`，便于自动化

## 构建与验证

只有跨平台内核（`core/` + `tablebuilder/` + `tests/`）通过 CMake 构建，可在 macOS / Linux 上验证：

```bash
cmake -S . -B build
cmake --build build
cd build && ctest --output-on-failure
```

Windows 层（`windows/`）不参与 CMake，需在 Windows + Visual Studio 2022（含 ATL 组件、Windows SDK 10）中用各自的 `.vcxproj` 构建（注：fire_config 已迁移至纯 Win32，不再需要 MFC 组件）：

```powershell
# 编译 TSF TIP DLL
MSBuild.exe windows\tsf\fire_tsf.vcxproj /p:Configuration=Release /p:Platform=x64

# 编译配置工具 EXE
MSBuild.exe windows\config\fire_config.vcxproj /p:Configuration=Release /p:Platform=x64

# 编译后台查字进程 EXE（fire_dictd.exe，命名管道 server + 词库/统计 + SQLite）
MSBuild.exe windows\dictd\fire_dictd.vcxproj /p:Configuration=Release /p:Platform=x64

# 构建词库构建工具
cmake -S . -B build -DBUILD_TABLEBUILDER=ON
cmake --build build --target tablebuilder --config Release

# 生成安装包（需要 Inno Setup 6）
powershell -ExecutionPolicy Bypass -File scripts\build_installer.ps1
```

CMake 选项（默认 ON）：`BUILD_CORE` / `BUILD_TESTS` / `BUILD_TABLEBUILDER`。

开发期高频迭代 TSF DLL 时，可用 `scripts\dev_reload.ps1`（需管理员）一键
「编译 → 部署为 `fire_tsf_DEV.dll` → 刷新宿主」，无需重启 / 改版本号。

## 目录结构

```
winFire/
├── core/                       # 跨平台内核（纯 C++17，不依赖 Windows / macOS API）
│   ├── include/fire/           # 公开头文件（含 dict_service.h / query_cache_store.h 等）
│   └── src/                    # 实现
├── ipc/                        # 查字进程分离 IPC 协议（纯 C++17，跨平台，入 CMake 可测）
│   ├── include/fire/ipc/       # 帧协议 + 消息编解码 + 二进制读写
│   └── src/                    # 实现
├── tablebuilder/               # 码表 txt -> sqlite 构建工具
├── tests/                      # 内核单元测试（含 IPC 协议编解码）
├── windows/                    # Windows 平台层
│   ├── tsf/                    # ATL TSF TIP DLL（fire_tsf.dll，含 DictIpcProxy + NamedPipeClient）
│   ├── candidate_window/       # Win32 + GDI+ 候选窗
│   ├── config/                 # 纯 Win32 配置界面（fire_config.exe）
│   ├── dictd/                  # 后台查字进程（fire_dictd.exe，命名管道 server + SQLite）
│   └── common/                 # DLL 与后台共用的 Win32 IPC 常量/工具
├── installer/                  # Inno Setup 脚本与 staging 资源（tablebuilder.exe + 默认 config.json）
├── scripts/                    # PowerShell 构建/安装/卸载脚本（dev_reload.ps1 供开发热重载）
├── resources/                  # 内置码表（86 版 / 98 版五笔 + 拼音）+ icons/（图标资源）
├── third_party/sqlite3/        # sqlite3 源码（经 wrapper + 集中裁剪宏编译进三个 EXE：fire_dictd / fire_config / tablebuilder；DLL 不再链接）
├── CMakeLists.txt              # 仅构建内核 + 测试 + tablebuilder
└── AGENTS.md                   # 项目详细架构说明
```

## 自定义码表

WinFire 支持用户自行替换五笔码表（86 / 98 版）和拼音码表，通过配置工具的「词库管理」页面操作。

### 码表文件格式

码表为纯文本（UTF-8 编码），每行格式为：

```
编码<TAB>词条1 词条2 词条3 …
```

- **编码**：仅限 ASCII 字母（a-z / A-Z），必须写在行首
- **词条**：多个词条以空格分隔，与编码之间用 Tab 或空格分隔
- 空行会被自动跳过
- 示例（`wb_table.txt`）：
  ```
  a       工 戈
  aa      式
  aaaa    工 恭恭敬敬 花花草草 劳斯莱斯
  aaad    工期
  ```

### 码表放置位置

码表文件放在安装目录的 `tables\` 子目录下，即：

```
%ProgramFiles%\WinFire\tables\
```

安装包内置三份码表：

| 文件 | 说明 |
|------|------|
| `wb_table.txt` | 五笔 86 版码表 |
| `wb_98_table.txt` | 五笔 98 版码表 |
| `py_table.txt` | 拼音码表（用于反查） |

若需要自定义码表，将你的 `.txt` 文件放入 `tables\` 目录即可，配置工具会自动识别。

### 如何换码表

1. 打开配置工具 `fire_config.exe`（开始菜单 → 微火五笔输入法）
2. 切换到「词库管理」页面
3. 在「五笔码表」下拉框中选择需要的码表文件（来自 `tables\` 目录）
4. 可选：在「拼音码表」下拉框中选择拼音码表（用于反查功能）
5. 点击「生成词库」，等待构建完成
6. 重新切换输入法即可生效

> 注意：码表格式必须为 **编码在前、词条在后**（即 `编码 词条`）。如果格式反了（`词条 编码`），配置工具会提示格式错误，此时生成的词库查询会返回空结果。

### 用户词库

用户词库文件位于 `%APPDATA%\WinFire\user-dict.txt`，可在配置工具「词库管理」页面点击「编辑用户词库」用记事本编辑。每行一个词条，格式为：

```
词条 编码
```

示例：
```
 WinFire    winf
 微火五笔    whwb
```

保存后切换输入法即可生效。

## 杀毒软件误报说明

安装包或运行时某些杀毒软件可能会弹出安全警告，原因如下：

### Windows Defender：未验证发布者

安装包 `WinFire-Setup.exe` 没有数字签名（Authenticode 签名证书需每年付费，对开源项目不现实），因此 Windows Defender 会显示「Windows 已保护你的电脑」/「未验证的发布者」。这是正常现象，点击「仍要运行」即可。

若仍不放心，可先右键安装包 → 属性 → 数字签名（确认无签名），然后查杀确认无毒后再安装。

### 其他杀毒软件：注册表与自启动报警

安装和运行时，杀毒软件可能对以下行为报警：

| 行为 | 原因 | 是否必要 |
|------|------|----------|
| 注册 COM 组件（写注册表） | 输入法需要注册为 TSF 文本输入处理器，这是 Windows 输入法的标准注册方式，所有第三方输入法均如此 | 必要，否则无法作为输入法加载 |
| 添加开机自启动（`HKCU\Run`） | 后台查字进程 `fire_dictd.exe` 需要随用户登录启动，使 SearchHost.exe 等沙箱进程首次输入即可查词 | 必要，否则开始菜单搜索等场景无候选 |
| 写 `%ProgramFiles%` 和 `%APPDATA%` | 程序文件与用户数据分离存放 | 必要，标准 Windows 应用规范 |

以上行为不含任何恶意逻辑。所有代码完全开源，担心的用户可通过以下方式自行验证：

1. **AI 解读源码**：将项目代码提交给 AI 分析，询问「此代码是否有恶意行为」，AI 可逐行解读
2. **审查构建脚本**：`scripts/build_installer.ps1` 和 `installer/winfire.iss` 清晰展示了安装包做什么
3. **审查网络连接**：WinFire 不联网，所有代码中搜索 `socket`/`connect`/`HTTP`/`URL` 可确认无网络通信
4. **审查注册表操作**：搜索 `RegSetValue`/`RegCreateKey`/`DllRegisterServer` 可确认只写输入法需要的键

## 当前版本

**0.0.1** — 初版移植完成，核心功能可用，主题设置 / CLI 等次要功能暂未实现。

## 许可与致谢

本项目采用 [MIT License](LICENSE) 开源，版权人：WinFire Project Contributors。

本项目继承业火输入法的开源精神。再次感谢 [@qwertyyb](https://github.com/qwertyyb) 与 [业火五笔输入法（Fire）](https://github.com/qwertyyb/Fire) 项目，是 WinFire 的源头与基础。如对原始 macOS 版本感兴趣，欢迎前往原作者仓库支持。
