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
; 程序文件：fire_tsf.dll / fire_config.exe / 码表（供配置界面重新构建词库用）
; 程序目录只读，所有用户可写数据放 %APPDATA%\WinFire
; ----------------------------------------------------------------------------
[Files]
; 主程序 TSF DLL：版本化文件名，写入新文件（不覆盖被占用的旧版同名文件）。
; 不用 restartreplace——新文件名必然不冲突，可立即写入并注册，无需重启。
Source: "{#BuildTsfDir}\fire_tsf.dll";       DestDir: "{app}"; DestName: "{#TsfDllName}"; Flags: ignoreversion
; 配置工具 EXE 一般不会被长期占用；仍用 restartreplace 兜底（正被打开时延迟替换）。
Source: "{#BuildConfigDir}\fire_config.exe"; DestDir: "{app}"; Flags: ignoreversion restartreplace
; 预构建词库（随包分发，避免用户安装时长时间等待 tablebuilder 构建 1.3MB 码表）
; 安装到用户数据目录，仅当目标不存在时复制（保留用户已有词库与动态调频）
Source: "{#StagingDir}\wb_py_dict.sqlite";   DestDir: "{userappdata}\WinFire"; Flags: onlyifdoesntexist
; 默认配置（仅首次安装写入，已存在则保留用户自定义）
Source: "{#StagingDir}\config.json";         DestDir: "{userappdata}\WinFire"; Flags: onlyifdoesntexist
; 码表（供 fire_config.exe 词库管理页导入/重建词库使用）
Source: "{#ResourcesDir}\wb_table.txt";      DestDir: "{app}\tables"; Flags: ignoreversion
Source: "{#ResourcesDir}\wb_98_table.txt";   DestDir: "{app}\tables"; Flags: ignoreversion
Source: "{#ResourcesDir}\py_table.txt";      DestDir: "{app}\tables"; Flags: ignoreversion

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
; 安装完成后静默注册 TSF（/s 静默）。DLL 为全新版本化文件名，无占用问题。
Filename: "{win}\System32\regsvr32.exe"; Parameters: "/s ""{app}\{#TsfDllName}"""; \
  StatusMsg: "{cm:StatusRegisteringIME}"; Flags: runhidden

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

; ----------------------------------------------------------------------------
; 卸载完成后询问是否删除用户数据（config / 词库 / 统计）
; ----------------------------------------------------------------------------
[CustomMessages]
; 简体中文本地化
StatusRegisteringIME=正在注册输入法...
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

function InitializeSetup(): Boolean;
begin
  // 安装前先清理上次卸载残留的 PendingFileRenameOperations 条目，
  // 避免 Inno Setup 误判为"上一次安装/卸载未完成"而拒绝安装。
  CleanWinFirePendingOps();
  Result := True;
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssPostInstall then
  begin
    CleanupOldTsfDlls();
    // DeleteOrDeferDll 可能又写入了新的 PFR 条目（旧 DLL 仍被占用），
    // 再次清理确保不留残余。
    CleanWinFirePendingOps();
  end;
end;

function InitializeUninstall(): Boolean;
begin
  // 卸载前清理残留的 PFR 条目，保证卸载流程不被残留状态阻塞
  CleanWinFirePendingOps();
  Result := True;
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
  end;

  if CurUninstallStep = usPostUninstall then
  begin
    // 卸载末尾清理 PFR 残留（卸载过程中 DeleteOrDeferDll 可能又写入了条目）
    CleanWinFirePendingOps();
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
