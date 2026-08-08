; ============================================================================
; WinFire IME Inno Setup Script
;
; 构建：
;   ISCC.exe winfire.iss
;
; 产物：
;   ..\dist\WinFire-Setup.exe   （单文件 installer，含卸载器）
;
; 安装目标：
;   %ProgramFiles%\WinFire\           程序文件 + uninstall.exe
;   %APPDATA%\WinFire\                用户数据（config / 词库 / 统计）
; ============================================================================
#define MyAppName          "微火五笔输入法"
#define MyAppNameEn        "WinFire"
; 版本号「单一来源」：仓库根目录的 VERSION 文件（形如 0.1.0）。
; VERSION 不纳入 git（本地测试可频繁递增，见 .gitignore）；缺失时回退到已跟踪的
; 基线 VERSION.default，保证全新 checkout / CI 仍可构建。
; 这里用 ISPP 在编译期读取，避免与 Globals.h / install.ps1 各写一份而失步。
; 发版/测试更新版本时只改 VERSION（或正式发版改 VERSION.default）一处（详见 AGENTS.md）。
#if FileExists("..\VERSION")
  #define VerFile "..\VERSION"
#else
  #define VerFile "..\VERSION.default"
#endif
#define VerFileHandle FileOpen(VerFile)
#define MyAppVersion Trim(FileRead(VerFileHandle))
#expr FileClose(VerFileHandle)
#if MyAppVersion == ""
  #error "VERSION / VERSION.default missing or empty (expected repo-root, e.g. 0.1.0)"
#endif
#define MyAppPublisher     "WinFire Project"
#define MyAppExeName       "fire_config.exe"
#define MyAppURL          "https://github.com/WinFire/WinFire"

; 安装/卸载 AppId（Inno 用它派生「程序和功能」卸载注册表键：{AppId}_is1）。
; MyAppIdRaw 为纯 GUID；MyAppId 前导 {{ 是 Inno 对字面 { 的转义，实际 AppId
; 值为 {F1RE0000-...-00000000APP1}，Inno 生成的卸载键即 <该值>_is1。
#define MyAppIdRaw "F1RE0000-0000-0000-0000-00000000APP1"
#define MyAppId "{{" + MyAppIdRaw + "}"

; TSF TIP DLL 采用「版本化文件名」：每个发行版的 DLL 文件名都带版本号，
; 从而升级时释放的是一个全新文件名的新 DLL，不会与仍被宿主进程占用（映像锁）
; 的旧同名 DLL 冲突。旧 DLL 由 [Code] 段反注册并（占用时）延迟到重启删除。
; 该文件名必须与 CFireTextService 内 GetModuleFileName 写入注册表的真实路径一致，
; 而后者取运行时真实路径，故此处任意版本化命名均可。
#define TsfDllName         "fire_tsf_" + MyAppVersion + ".dll"

; 构建产物路径（相对于本 .iss 文件所在目录）
#define BuildTsfDir        "..\windows\tsf\x64\Release"
#define BuildConfigDir     "..\windows\config\x64\Release"
#define BuildDictdDir      "..\windows\dictd\x64\Release"
#define ResourcesDir       "..\resources"
#define StagingDir         "staging"

[Setup]
AppId={#MyAppId}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} {#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}
AppUpdatesURL={#MyAppURL}
DefaultDirName={autopf}\WinFire
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
OutputDir=..\dist
OutputBaseFilename=WinFire-Setup
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64
ArchitecturesInstallIn64BitMode=x64
PrivilegesRequired=admin
; 版本化 DLL 文件名 + 侧载注册后，新版 DLL 与被占用的旧版是不同文件，无需强制
; 关闭宿主进程（Word/Chrome/explorer）即可完成安装。因此不再启用 CloseApplications
; 强制弹窗；仅让 Restart Manager 记录占用，实际不阻塞。
CloseApplications=no
RestartApplications=no
; 卸载时弹确认对话框询问是否保留用户数据
UninstallDisplayIcon={app}\fire_config.exe
UninstallDisplayName={#MyAppName}

; ----------------------------------------------------------------------------
; 程序文件：fire_tsf.dll / fire_config.exe / fire_dictd.exe / tablebuilder.exe / 码表 / 默认配置
; 词库不预构建：由下方 BuildDictIfMissing 在安装时用 tablebuilder + 码表现场生成。
; 程序目录只读，所有用户可写数据放 %APPDATA%\WinFire
; ----------------------------------------------------------------------------
[Files]
; 主程序 TSF DLL：版本化文件名，写入新文件（不覆盖被占用的旧版同名文件）。
; 不用 restartreplace——新文件名必然不冲突，可立即写入并注册，无需重启。
Source: "{#BuildTsfDir}\fire_tsf.dll";       DestDir: "{app}"; DestName: "{#TsfDllName}"; Flags: ignoreversion
; 配置工具 EXE 一般不会被长期占用；仍用 restartreplace 兜底（正被打开时延迟替换）。
Source: "{#BuildConfigDir}\fire_config.exe"; DestDir: "{app}"; Flags: ignoreversion restartreplace
; 后台查字进程 EXE（正常 IL，供 AppContainer 沙箱进程经 IPC 查库）。
; 后台可能常驻，安装/升级前由 [Code] 先温和结束旧进程；restartreplace 兜底占用场景。
Source: "{#BuildDictdDir}\fire_dictd.exe";   DestDir: "{app}"; Flags: ignoreversion restartreplace
; 词库（wb_py_dict.sqlite）不再随包预构建：改由 [Code] 段 BuildDictIfMissing 在
; 安装时用 tablebuilder.exe + 码表现场生成（仅当用户数据目录下不存在时）。
; 这样省去 ~7.78MB 预构建 db（包内 ~3.4MB），且现场构建仅约 1 秒。用户已有词库
; （含动态调频）保留不覆盖，语义与原 onlyifdoesntexist 一致。
; 默认 config.json 模板：释放到 {tmp}（deleteafterinstall 安装后自动清理），
; 由 [Code] WriteDefaultConfigIfMissing 读取后展开 {APP} 占位符写到用户数据目录。
; 模板唯一真相源为 resources/config.default.json，与 build_installer.ps1 /
; install.ps1 / dev_reload.ps1 共用同一文件。
Source: "{#ResourcesDir}\config.default.json"; DestDir: "{tmp}"; \
  DestName: "_wf_config_template.json"; Flags: ignoreversion deleteafterinstall
; 码表（供 fire_config.exe 词库管理页导入/重建词库使用）
Source: "{#ResourcesDir}\wb_table.txt";      DestDir: "{app}\tables"; Flags: ignoreversion
Source: "{#ResourcesDir}\wb_98_table.txt";   DestDir: "{app}\tables"; Flags: ignoreversion
Source: "{#ResourcesDir}\py_table.txt";      DestDir: "{app}\tables"; Flags: ignoreversion
; 词库构建工具（fire_config.exe 词库管理页点击"生成词库"时调用）
Source: "{#StagingDir}\tablebuilder.exe";    DestDir: "{app}"; Flags: ignoreversion

[Dirs]
; 程序目录与用户数据目录
Name: "{app}";                  Flags: uninsneveruninstall
Name: "{app}\tables";           Flags: uninsneveruninstall
Name: "{userappdata}\WinFire";  Flags: uninsneveruninstall

; ----------------------------------------------------------------------------
; 注册 TSF：regsvr32 /s <版本化DLL>（DllRegisterServer 自包含 CLSID/Profile/Category 注册）
; 侧载：新版 CLSID/Profile 按版本派生，与旧版不同，故新版注册不影响旧版；
;       旧版的反注册与旧 DLL 清理在 [Code] 的 CurStepChanged(ssPostInstall) 完成。
; ----------------------------------------------------------------------------
[Run]
; 授予 ALL APPLICATION PACKAGES（AppContainer，SID S-1-15-2-1）对程序目录的
; 读取+执行权限，使 SearchHost.exe / UWP 等沙箱进程能加载 fire_tsf.dll 并读 tables。
Filename: "{sys}\icacls.exe"; Parameters: """{app}"" /grant *S-1-15-2-1:(OI)(CI)(RX) /T /C /Q"; \
  StatusMsg: "{cm:StatusGrantingAcl}"; Flags: runhidden

; 安装完成后静默注册 TSF（/s 静默）。DLL 为全新版本化文件名，无占用问题。
Filename: "{win}\System32\regsvr32.exe"; Parameters: "/s ""{app}\{#TsfDllName}"""; \
  StatusMsg: "{cm:StatusRegisteringIME}"; Flags: runhidden

; 安装完成后立即拉起后台查字进程（正常 IL），使沙箱进程首次输入即可出候选，
; 无需等 DLL 端按需拉起（AppContainer 进程通常无权 CreateProcess）。
; BeforeInstall 在该 [Run] 条目启动 dictd 之前执行：此时 [Files] 已全部拷贝完毕
; （Inno 顺序为 [Files] → [Run] → ssPostInstall），tablebuilder.exe 与码表均已就位。
; 必须在此处而非 ssPostInstall 构建词库——否则 dictd 早于词库启动，一次性 Init 打开
; sqlite 失败后 db_ 永不复原，Hello.ready 永远 false，全部按键透传（等同英文）。
Filename: "{app}\fire_dictd.exe"; Flags: nowait runhidden skipifsilent; \
  BeforeInstall: BuildDictIfMissing

; 安装完成后启动配置工具（可选，用户可在向导中勾选）
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchConfigTool}"; \
  Flags: nowait postinstall skipifsilent

; ----------------------------------------------------------------------------
; 卸载：先反注册 TSF，再删除程序文件；用户数据通过 uninsneveruninstall 保留
; ----------------------------------------------------------------------------
[UninstallRun]
; 反注册本版本 TSF（DllUnregisterServer 删除本版本 CLSID/Profile/Category）
Filename: "{win}\System32\regsvr32.exe"; Parameters: "/s /u ""{app}\{#TsfDllName}"""; \
  Flags: runhidden

; ----------------------------------------------------------------------------
; 卸载后清理程序目录（用户数据在 %APPDATA% 下，不会被删）
; Code 段在卸载阶段删除 tables 目录与残留文件
; ----------------------------------------------------------------------------
[UninstallDelete]
; 清理所有版本化 TSF DLL（含历史升级中因占用而延迟删除失败的残留）
Type: files;          Name: "{app}\fire_tsf_*.dll"
; 三个独立 EXE：fire_dictd.exe 是常驻后台进程，卸载时可能仍被映像锁占用
; （TSF DLL 被宿主进程按键触发时可能重新拉起 dictd，见 NamedPipeClient.cpp）。
; 列在这里让 Inno 尝试删除；被占用时由 [Code] 的 DeleteOrDeferDll 标记重启删除。
Type: files;          Name: "{app}\fire_dictd.exe"
Type: files;          Name: "{app}\fire_config.exe"
Type: files;          Name: "{app}\tablebuilder.exe"
Type: filesandordirs; Name: "{app}\tables"
Type: dirifempty;     Name: "{app}"

; ----------------------------------------------------------------------------
; 「程序和功能」注册表项
; ----------------------------------------------------------------------------
[Registry]
; 程序和功能中的显示名 / 卸载入口（Inno Setup 自动维护 UninstallString，这里补 DisplayVersion 等）
; 卸载键名必须与 Inno 依据 AppId 生成的键一致：<AppId>_is1（旧代码误用 {#MyAppExeName}_is1
; = fire_config.exe_is1，与实际键对不上，补写的值挂错位置，故修正为 {#MyAppId}_is1）。
Root: HKLM; Subkey: "Software\Microsoft\Windows\CurrentVersion\Uninstall\{#MyAppId}_is1"; \
  ValueType: string; ValueName: "DisplayVersion"; ValueData: "{#MyAppVersion}"; Flags: uninsdeletevalue
Root: HKLM; Subkey: "Software\Microsoft\Windows\CurrentVersion\Uninstall\{#MyAppId}_is1"; \
  ValueType: string; ValueName: "Publisher"; ValueData: "{#MyAppPublisher}"; Flags: uninsdeletevalue
Root: HKLM; Subkey: "Software\Microsoft\Windows\CurrentVersion\Uninstall\{#MyAppId}_is1"; \
  ValueType: string; ValueName: "DisplayIcon"; ValueData: "{app}\fire_config.exe"; Flags: uninsdeletevalue

; 用户数据目录绝对路径（Fix B）：TIP DLL 加载到 SearchHost.exe 等 SYSTEM 进程时，
; CSIDL_APPDATA 会指向系统目录；通过注册表固化用户 AppData 绝对路径来解决。
Root: HKLM; Subkey: "Software\WinFire"; ValueType: string; \
  ValueName: "UserDataDir"; ValueData: "{userappdata}\WinFire"; \
  Flags: uninsdeletekey

; fire_dictd.exe 开机自启动：系统重启后，SearchHost.exe 等 AppContainer 沙箱进程
; 首次加载输入法时无权 CreateProcess 拉起后台。Run 键保证 dictd 随用户登录自启，
; 沙箱进程首次输入即可经 IPC 查库出候选。Inno 的 HKCU 在 PrivilegesRequired=admin
; 下指向运行安装程序的登录用户（非管理员账户），语义正确。
; uninstall 时 uninsdeletevalue 自动清掉该值，无需在 [Code] 手动处理。
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; \
  ValueType: string; ValueName: "WinFireDictd"; \
  ValueData: """{app}\fire_dictd.exe"""; Flags: uninsdeletevalue

; ----------------------------------------------------------------------------
; 卸载完成后询问是否删除用户数据（config / 词库 / 统计）
; ----------------------------------------------------------------------------
[CustomMessages]
; 简体中文本地化
StatusRegisteringIME=正在注册输入法...
StatusGrantingAcl=正在配置沙箱访问权限...
StatusBuildingDict=正在构建词库，请稍候...
LaunchConfigTool=启动配置工具(&L)
UninstallDataPrompt=是否删除用户数据（配置、词库、输入统计、日志）？%n选择「否」将保留 %1 以便将来重装。

[Code]
const
  MOVEFILE_DELAY_UNTIL_REBOOT = $00000004;

// 延迟到重启时删除（当文件仍被宿主进程占用、无法立即删除时使用）。
// lpNewFileName 传 '' 表示删除 lpExistingFileName。
function MoveFileExW(lpExistingFileName, lpNewFileName: String; dwFlags: DWORD): Boolean;
  external 'MoveFileExW@kernel32.dll stdcall';

// 清理 PendingFileRenameOperations 中 WinFire 的残留条目。
// DeleteOrDeferDll 在 DLL 被占用时会写入 MoveFileEx 重启删除指令，
// 若重启后文件已被其他途径删除，PFR 条目会变成永不清除的幽灵记录，
// 导致 Inno Setup 拒绝安装（"the installation/removal of a previous
// program was not completed"）。
procedure CleanWinFirePendingOps();
var
  psPath, psContent, psArgs: String;
  ResultCode: Integer;
begin
  psPath := ExpandConstant('{tmp}\_wf_pfr_cleanup.ps1');
  psContent :=
    '$k = "HKLM:\SYSTEM\CurrentControlSet\Control\Session Manager"' + #13#10 +
    '$v = (Get-ItemProperty $k -EA SilentlyContinue).PendingFileRenameOperations' + #13#10 +
    'if (-not $v) { exit 0 }' + #13#10 +
    '$n = @(); $s = 0' + #13#10 +
    'foreach ($e in $v) {' + #13#10 +
    '  if ($e -match "WinFire") { $s = 1; continue }' + #13#10 +
    '  if ($s) { $s = 0; continue }' + #13#10 +
    '  $n += $e' + #13#10 +
    '}' + #13#10 +
    'if ($n.Count -ne $v.Count) {' + #13#10 +
    '  Set-ItemProperty $k -Name PendingFileRenameOperations -Value $n' + #13#10 +
    '}';
  SaveStringToFile(psPath, psContent, False);
  psArgs := '-NoProfile -ExecutionPolicy Bypass -File "' + psPath + '"';
  Exec('powershell.exe', psArgs, '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
  DeleteFile(psPath);
end;

// 尝试删除一个 DLL；占用无法删除时改为标记「重启后删除」，不阻塞流程。
procedure DeleteOrDeferDll(const DllPath: String);
begin
  if not FileExists(DllPath) then
    exit;
  if DeleteFile(DllPath) then
    exit;
  // 仍被占用：交给 MoveFileEx 在下次重启时删除（用户无需立即重启）。
  MoveFileExW(DllPath, '', MOVEFILE_DELAY_UNTIL_REBOOT);
end;

// 安装完成后：把「除当前版本外」的旧版本化 DLL 反注册并删除（或延迟删除）。
// 新版本已在 [Run] 中侧载注册（新 CLSID/Profile），此处清理旧版本注册与文件。
procedure CleanupOldTsfDlls();
var
  appDir, curDll, pattern, foundPath: String;
  fr: TFindRec;
  rc: Integer;
begin
  appDir := ExpandConstant('{app}');
  curDll := appDir + '\' + '{#TsfDllName}';
  pattern := appDir + '\fire_tsf_*.dll';
  if FindFirst(pattern, fr) then
  begin
    try
      repeat
        foundPath := appDir + '\' + fr.Name;
        // 跳过当前版本 DLL（刚安装并注册的那个）
        if CompareText(foundPath, curDll) <> 0 then
        begin
          // 反注册旧版本的 TSF（旧 CLSID/Profile/Category 自包含在旧 DLL 里）
          Exec(ExpandConstant('{win}\System32\regsvr32.exe'),
               '/s /u "' + foundPath + '"', '', SW_HIDE, ewWaitUntilTerminated, rc);
          // 删除旧 DLL 文件；占用则延迟到重启删除
          DeleteOrDeferDll(foundPath);
        end;
      until not FindNext(fr);
    finally
      FindClose(fr);
    end;
  end;
end;

// 主动结束 WinFire 的独立 EXE（fire_dictd.exe / fire_config.exe / tablebuilder.exe）。
// 这些 EXE 不像 fire_tsf.dll 那样会被宿主进程映像锁长期占用，但用户可能正开着
// 配置工具（fire_config.exe）或在词库管理页调用 tablebuilder.exe。安装/升级/卸载前
// 先结束它们，释放映像占用，使新版 EXE 立即覆盖、卸载时能干净删除 {app} 目录；
// 避免 restartreplace 在卸载阶段不生效时残留文件导致「上次卸载未完成」。
// fire_tsf.dll 被宿主进程（Word/Chrome/explorer/ctfmon）加载，这里不杀宿主，
// 其映像锁由版本化文件名 + 侧载 + DeleteOrDeferDll(MoveFileEx 延迟删除) 处理。
// fire_dictd.exe 改为常驻进程（不再空闲退出），杀掉后本次会话不再重生，
// 下次登录由 HKCU\Run 拉起。
procedure KillUserExes();
var
  ResultCode: Integer;
begin
  // 三个 EXE 都结束；不存在对应进程时 taskkill 返回非零，忽略即可（不阻断流程）。
  Exec(ExpandConstant('{sys}\taskkill.exe'), '/F /IM fire_dictd.exe',
       '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
  Exec(ExpandConstant('{sys}\taskkill.exe'), '/F /IM fire_config.exe',
       '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
  Exec(ExpandConstant('{sys}\taskkill.exe'), '/F /IM tablebuilder.exe',
       '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
end;

// 调用 tablebuilder.exe 执行一次子命令；成功（进程启动且退出码 0）返回 True。
// Exec 返回 False 表示连进程都启动不了（如缺 CRT），ResultCode 非零表示子命令报错。
function RunTableBuilder(const Params: String; var ResultCode: Integer): Boolean;
begin
  Result := Exec(ExpandConstant('{app}\tablebuilder.exe'), Params, '',
                 SW_HIDE, ewWaitUntilTerminated, ResultCode);
end;

// 安装时现场构建词库（仅当用户数据目录下不存在 wb_py_dict.sqlite 时）。
// 不再随包预构建 ~7.78MB 的 sqlite（包内省 ~3.4MB），改由 tablebuilder + 码表
// 现场生成（约 1 秒）。用户已有词库（含动态调频）保留，语义同原 onlyifdoesntexist。
// 失败时不中断安装：dictd 缺库仅返回空结果不崩溃，用户可在配置工具词库管理页重建
// （tablebuilder.exe 与码表都在 {app}，随时可用）。
procedure BuildDictIfMissing();
var
  dbPath, appDir: String;
  ResultCode: Integer;
begin
  dbPath := ExpandConstant('{userappdata}\WinFire\wb_py_dict.sqlite');
  if FileExists(dbPath) then
    exit;  // 保留用户已有词库与动态调频
  appDir := ExpandConstant('{app}');
  WizardForm.StatusLabel.Caption := CustomMessage('StatusBuildingDict');

  // 三步与 build_installer.ps1 / DictPage.cpp 完全一致：建 wb_dict → 建 py_dict → 合并
  // （tablebuilder 内部 combine 完会 DROP 中间表 + VACUUM，产物即最终词库）。
  // 注意：每步首参必须带 '--create-dict' 子命令；tablebuilder 按 argv[1] 路由子命令，
  // 漏掉会落入 usage 分支静默 return 0（既不报错也不建表），导致 combine 找不到中间表。
  if (not RunTableBuilder('--create-dict "' + appDir + '\tables\wb_table.txt" wb_dict "' + dbPath + '"', ResultCode))
     or (ResultCode <> 0) then exit;
  if (not RunTableBuilder('--create-dict "' + appDir + '\tables\py_table.txt" py_dict "' + dbPath + '"', ResultCode))
     or (ResultCode <> 0) then exit;
  RunTableBuilder('--combine-dict "' + dbPath + '"', ResultCode);
end;

// 生成默认 config.json 到用户数据目录（仅当不存在时——保留用户自定义）。
// 模板唯一真相源为 resources/config.default.json，由 [Files] 释放到 {tmp}。
// 用 LoadStringsFromFile 读取（自动识别 UTF-8，返回 Unicode 字符串数组，
// 绕过 LoadStringFromFile 的 AnsiString 类型陷阱），按行以 CRLF 拼回完整文本，
// 再 StringChange 展开 {APP} 占位符为真实 {app} 安装路径，使词库管理「已选」
// 落盘即指向 wb_table.txt / py_table.txt。在 ssPostInstall 调用（[Files] 已就位）。
procedure WriteDefaultConfigIfMissing();
var
  userDataDir, tmplPath, tmpl, appDir, configPath: String;
  lines: TArrayOfString;
  i: Integer;
begin
  userDataDir := ExpandConstant('{userappdata}\WinFire');
  configPath := userDataDir + '\config.json';
  if FileExists(configPath) then exit;  // 仅首次安装写入，已存在则保留用户自定义

  // 用户数据目录此时可能尚未由 [Dirs] 创建（ssInstall 早于 [Files]），主动建目录。
  if not DirExists(userDataDir) then
    CreateDir(userDataDir);

  // {APP} 为自定义占位符（非 Inno 常量，ISPP 不会展开；不能用 {app}，否则编译期被展开）。
  tmplPath := ExpandConstant('{tmp}\_wf_config_template.json');
  if not LoadStringsFromFile(tmplPath, lines) then exit;  // 模板读取失败则放弃
  tmpl := '';
  for i := 0 to GetArrayLength(lines) - 1 do begin
    if i > 0 then tmpl := tmpl + #13#10;
    tmpl := tmpl + lines[i];
  end;
  appDir := ExpandConstant('{app}');
  StringChange(tmpl, '{APP}', appDir);
  SaveStringToFile(configPath, tmpl, False);
end;

function InitializeSetup(): Boolean;
begin
  // 安装前先清理上次卸载残留的 PendingFileRenameOperations 条目，
  // 避免 Inno Setup 误判为"上一次安装/卸载未完成"而拒绝安装。
  CleanWinFirePendingOps();
  // 结束可能常驻/正被用户打开的 EXE（dictd 后台 + config/tablebuilder 工具），
  // 释放映像占用，保证新版 EXE 能即时覆盖（不必走 restartreplace 延迟替换）。
  KillUserExes();
  Result := True;
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssPostInstall then
  begin
    CleanupOldTsfDlls();
    // 生成默认 config.json（仅当用户无已有 config 时，onlyifdoesntexist 语义）。
    // 内联 camelCase 模板并展开 {APP} 占位符为真实 {app} 路径，使词库管理「已选」
    // 落盘即正确显示 wb_table.txt / py_table.txt。
    WriteDefaultConfigIfMissing();
    // 词库构建（BuildDictIfMissing）已移至 [Run] 中 dictd 启动条目的 BeforeInstall：
    // Inno 实际顺序为 [Files] → [Run] → ssPostInstall，原放 ssPostInstall 会导致 dictd
    // 早于词库启动、一次性 Init 打开 sqlite 失败后永不复原（Hello.ready 恒 false）。
    // 此处不再调用 BuildDictIfMissing。
    // 注意：此处【不】清理 PFR。CleanupOldTsfDlls 中 DeleteOrDeferDll 对仍被占用的
    // 旧版本 DLL 写入了 MoveFileEx 重启删除条目，立即清空会让旧 DLL 永远删不掉
    // （文件留在 {app}，重启删除指令被撤）。PFR 条目留待系统重启删除，或下次安装
    // 开头由 InitializeSetup 的 CleanWinFirePendingOps 兜底清（若已随重启删完）。
  end;
end;

function InitializeUninstall(): Boolean;
begin
  // 卸载前清理残留的 PFR 条目，保证卸载流程不被残留状态阻塞
  CleanWinFirePendingOps();
  // 结束常驻后台 + 正被用户打开的配置/词库工具，释放映像占用以便删除 {app}。
  KillUserExes();
  Result := True;
end;

// 把一段 PowerShell 脚本写到临时文件、执行、删除。CleanSortOrderAssemblyItems
// 与 CleanUserProfileInputMethods 的脚手架相同，抽此辅助过程消除重复。
procedure RunCleanupScript(const scriptName, psBody: String);
var
  psPath, psArgs: String;
  ResultCode: Integer;
begin
  psPath := ExpandConstant('{tmp}\' + scriptName);
  SaveStringToFile(psPath, psBody, False);
  psArgs := '-NoProfile -ExecutionPolicy Bypass -File "' + psPath + '"';
  Exec('powershell.exe', psArgs, '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
  DeleteFile(psPath);
end;

// 清理用户级 SortOrder\AssemblyItem 中的 WinFire 残留条目。
// regsvr32 /u（[UninstallRun]）调用 DllUnregisterServer 会清理这些，但若 DLL 已被
// 删除则 regsvr32 失败，此处用 PowerShell 兜底直接扫描注册表删除。
procedure CleanSortOrderAssemblyItems();
begin
  RunCleanupScript('_wf_sortorder_cleanup.ps1',
    '$p = "8E9F0B21-3C4D-4E5A-9B7C-1F2A3B"' + #13#10 +
    '$b = "HKCU:\Software\Microsoft\CTF\SortOrder\AssemblyItem"' + #13#10 +
    'if (Test-Path $b) {' + #13#10 +
    '  Get-ChildItem $b | ForEach-Object {' + #13#10 +
    '    $lang = $_' + #13#10 +
    '    Get-ChildItem $lang.PSPath | ForEach-Object {' + #13#10 +
    '      Get-ChildItem $_.PSPath | ForEach-Object {' + #13#10 +
    '        $e = Get-ItemProperty $_.PSPath' + #13#10 +
    '        if ($e.CLSID -and $e.CLSID -match $p) {' + #13#10 +
    '          Remove-Item $_.PSPath -Force' + #13#10 +
    '        }' + #13#10 +
    '      }' + #13#10 +
    '    }' + #13#10 +
    '  }' + #13#10 +
    '}');
end;

// 清理 HKCU\Control Panel\International\User Profile 中的 WinFire 输入法条目。
// Get-WinUserLanguageList 与「替代默认输入法」下拉均从此处读，ILOT_UNINSTALL 不可靠，
// 直接扫描删除，防止残留「不可用的输入法」。
procedure CleanUserProfileInputMethods();
begin
  RunCleanupScript('_wf_userprofile_cleanup.ps1',
    '$p = "8E9F0B21-3C4D-4E5A-9B7C-1F2A3B"' + #13#10 +
    '$b = "HKCU:\Control Panel\International\User Profile"' + #13#10 +
    'if (Test-Path $b) {' + #13#10 +
    '  Get-ChildItem $b | ForEach-Object {' + #13#10 +
    '    $lang = $_' + #13#10 +
    '    $key = Get-Item $lang.PSPath' + #13#10 +
    '    if ($key) {' + #13#10 +
    '      $toRemove = @()' + #13#10 +
    '      foreach ($v in $key.Property) { if ($v -like "*$p*") { $toRemove += $v } }' + #13#10 +
    '      foreach ($n in $toRemove) { Remove-ItemProperty -Path $lang.PSPath -Name $n -Force }' + #13#10 +
    '    }' + #13#10 +
    '  }' + #13#10 +
    '}');
end;

// 删除一个目录树：先整体 DelTree；若仍有残留（如 dictd 句柄锁住的 sqlite/wal/shm，
// 或被宿主进程占用的文件无法立即删除），对剩余文件逐个 DeleteOrDeferDll，把无法
// 立即删除者标记为「重启后删除」（MoveFileEx），避免静默失败留下残留。
// 与 DeleteOrDeferDll 同样：用户无需立即重启，残留文件会在下次重启时由内核删除。
//
// 目录本身也一并延迟删除：当内部有文件被占用时，DelTree 无法删掉目录（非空），
// Inno 的 [UninstallDelete] dirifempty 此时也因目录非空而跳过。重启后文件先被内核
// 删除，目录变空，但无人再删它 → 留下空目录残留。此处把目录本身也写入 PFR
// （MoveFileEx 对空目录有效；PFR 中文件条目先于目录条目处理，重启时目录已空）。
procedure DeleteTreeWithDefer(const dirPath: String);
var
  fr: TFindRec;
  fullPath: String;
begin
  if not DirExists(dirPath) then
    exit;
  // 先整体删除（覆盖绝大多数文件未被占用的常见情形）。
  DelTree(dirPath, True, True, True);
  if not DirExists(dirPath) then
    exit;
  // DelTree 对被占用的文件会静默跳过：再扫一遍残留，逐个延迟删除。
  if FindFirst(dirPath + '\*', fr) then
  begin
    try
      repeat
        // 跳过目录项本身（FindFirst/FindNext 会返回 . 与 ..）
        if (fr.Name <> '.') and (fr.Name <> '..') then
        begin
          fullPath := dirPath + '\' + fr.Name;
          // 仅处理文件；子目录（如 logs）由其内部文件删除后变空，再随父目录一并删除。
          if DirExists(fullPath) then
            DeleteTreeWithDefer(fullPath)
          else
            DeleteOrDeferDll(fullPath);
        end;
      until not FindNext(fr);
    finally
      FindClose(fr);
    end;
  end;
  // 内部文件已删（或已标记重启删除）：再尝试删空目录；仍失败则把目录本身
  // 也写入重启删除队列，避免重启后文件已删、目录却留下成为空壳残留。
  if DirExists(dirPath) then
  begin
    if not RemoveDir(dirPath) then
      MoveFileExW(dirPath, '', MOVEFILE_DELAY_UNTIL_REBOOT);
  end;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  userDataDir, appDir, pattern, foundPath: String;
  fr: TFindRec;
begin
  // 反注册（[UninstallRun]）之后：删除所有版本化 DLL；被占用者延迟到重启删除。
  if CurUninstallStep = usUninstall then
  begin
    appDir := ExpandConstant('{app}');
    pattern := appDir + '\fire_tsf_*.dll';
    if FindFirst(pattern, fr) then
    begin
      try
        repeat
          foundPath := appDir + '\' + fr.Name;
          DeleteOrDeferDll(foundPath);
        until not FindNext(fr);
      finally
        FindClose(fr);
      end;
    end;
    // 此时 TSF Profile 已被 [UninstallRun] 的 regsvr32 /u 反注册，宿主进程下一次按键
    // 不再调用本 DLL，也就不会经 LaunchBackend 重新拉起 dictd。在此删除三个独立 EXE：
    // fire_dictd.exe 是常驻后台，此前由 InitializeUninstall 的 KillUserExes 释放过，
    // 但为兜底「杀完又被重生」或「未成功结束」的窗口，这里再次尝试删除，占用则延迟
    // 到重启删除（与 DLL 同等对待，DeleteOrDeferDll 机制对 EXE 通用）。
    DeleteOrDeferDll(appDir + '\fire_dictd.exe');
    DeleteOrDeferDll(appDir + '\fire_config.exe');
    DeleteOrDeferDll(appDir + '\tablebuilder.exe');
    // {app} 与 {app}\tables 由 [Dirs] uninsneveruninstall 标记，Inno 不会自动删除；
    // [UninstallDelete] 的 dirifempty 在文件被占用时也跳过。此处主动尝试删除 tables
    // 子目录与 {app} 本身，删除失败（内部仍有被占用文件）则延迟到重启删除，避免留下
    // 空目录残留（重启时文件先被删，目录变空后被删，顺序由 PFR 写入顺序保证）。
    if DirExists(appDir + '\tables') then
    begin
      if not RemoveDir(appDir + '\tables') then
        MoveFileExW(appDir + '\tables', '', MOVEFILE_DELAY_UNTIL_REBOOT);
    end;
    if DirExists(appDir) then
    begin
      if not RemoveDir(appDir) then
        MoveFileExW(appDir, '', MOVEFILE_DELAY_UNTIL_REBOOT);
    end;
    // 兜底清理用户级 SortOrder\AssemblyItem 中的 WinFire 残留条目
    // （DllUnregisterServer 已清理，此处防 DLL 已删时 regsvr32 失败的兜底）。
    CleanSortOrderAssemblyItems();
    // 兜底清理 HKCU\Control Panel\International\User Profile 中的 WinFire 条目
    // （Get-WinUserLanguageList 数据源，残留会显示「不可用的输入法」）。
    CleanUserProfileInputMethods();
  end;

  if CurUninstallStep = usPostUninstall then
  begin
    // 注意：此处【不】清理 PFR。被宿主进程占用的 DLL/EXE 在 usUninstall 阶段已由
    // DeleteOrDeferDll 写入 MoveFileEx 重启删除条目，若在此清空会让这些文件永远
    // 删不掉（{app}/{userappdata} 里残留文件 + 重启删除指令被撤）。PFR 条目应留待系统
    // 重启时由内核删除文件，或下次安装开头由 InitializeSetup 的 CleanWinFirePendingOps
    // 兜底清。
    userDataDir := ExpandConstant('{userappdata}\WinFire');
    if DirExists(userDataDir) then
    begin
      if MsgBox(Format(CustomMessage('UninstallDataPrompt'), [userDataDir]),
                mbConfirmation, MB_YESNO or MB_DEFBUTTON2) = IDYES then
      begin
        // 用 DeleteTreeWithDefer 而非 DelTree：被 dictd 句柄锁住的 sqlite/wal/shm
        // 无法立即删除，逐个 MoveFileEx 延迟到重启删除，避免静默失败留下残留
        // （此前 DelTree 静默跳过这些文件，导致用户选「删除」后仍残留 sqlite）。
        DeleteTreeWithDefer(userDataDir);
        // %LOCALAPPDATA%\WinFire（Debug 构建下 fire_tsf 日志落盘于此，可达上百 MB；
        // 此前三条卸载路径都不覆盖，是独立覆盖盲区）。与用户数据目录同等对待：
        // 用户选「删除」时一并清理，选「保留」时一并保留，语义一致。
        DeleteTreeWithDefer(ExpandConstant('{localappdata}\WinFire'));
      end;
    end;
  end;
end;
