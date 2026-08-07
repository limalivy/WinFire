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
; 默认配置（仅首次安装写入，已存在则保留用户自定义）
Source: "{#StagingDir}\config.json";         DestDir: "{userappdata}\WinFire"; Flags: onlyifdoesntexist
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
Filename: "{app}\fire_dictd.exe"; Flags: nowait runhidden skipifsilent

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
UninstallDataPrompt=是否删除用户数据（配置、词库、输入统计）？%n选择「否」将保留 %1 以便将来重装。

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
    // 现场构建词库（ssPostInstall 时 [Files] 已全部就位，且早于 [Run] 启动 dictd，
    // 保证 dictd 首次启动时 sqlite 已存在）。仅当用户无已有词库时触发。
    BuildDictIfMissing();
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
    // 兜底清理用户级 SortOrder\AssemblyItem 中的 WinFire 残留条目
    // （DllUnregisterServer 已清理，此处防 DLL 已删时 regsvr32 失败的兜底）。
    CleanSortOrderAssemblyItems();
    // 兜底清理 HKCU\Control Panel\International\User Profile 中的 WinFire 条目
    // （Get-WinUserLanguageList 数据源，残留会显示「不可用的输入法」）。
    CleanUserProfileInputMethods();
  end;

  if CurUninstallStep = usPostUninstall then
  begin
    // 注意：此处【不】清理 PFR。被宿主进程占用的 DLL 在 usUninstall 阶段已由
    // DeleteOrDeferDll 写入 MoveFileEx 重启删除条目，若在此清空会让这些 DLL 永远
    // 删不掉（{app} 里残留文件 + 重启删除指令被撤）。PFR 条目应留待系统重启时由
    // 内核删除文件，或下次安装开头由 InitializeSetup 的 CleanWinFirePendingOps 兜底清。
    userDataDir := ExpandConstant('{userappdata}\WinFire');
    if DirExists(userDataDir) then
    begin
      if MsgBox(Format(CustomMessage('UninstallDataPrompt'), [userDataDir]),
                mbConfirmation, MB_YESNO or MB_DEFBUTTON2) = IDYES then
      begin
        DelTree(userDataDir, True, True, True);
      end;
    end;
  end;
end;
