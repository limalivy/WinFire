; ============================================================================
; winFire IME Inno Setup Script
;
; 构建：
;   ISCC.exe winfire.iss
;
; 产物：
;   ..\dist\FireIME-Setup.exe   （单文件 installer，含卸载器）
;
; 安装目标：
;   %ProgramFiles%\FireIME\           程序文件 + uninstall.exe
;   %APPDATA%\FireIME\                用户数据（config / 词库 / 统计）
; ============================================================================
#define MyAppName          "业火五笔输入法"
#define MyAppNameEn        "FireIME"
#define MyAppVersion       "0.1.0"
#define MyAppPublisher     "winFire Project"
#define MyAppExeName       "fire_config.exe"
#define MyAppURL          "https://github.com/winFire/winFire"

; 构建产物路径（相对于本 .iss 文件所在目录）
#define BuildTsfDir        "..\windows\tsf\x64\Release"
#define BuildConfigDir     "..\windows\config\x64\Release"
#define ResourcesDir       "..\resources"
#define StagingDir         "staging"

[Setup]
AppId={{F1RE0000-0000-0000-0000-00000000APP1}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} {#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}
AppUpdatesURL={#MyAppURL}
DefaultDirName={autopf}\FireIME
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
OutputDir=..\dist
OutputBaseFilename=FireIME-Setup
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64
ArchitecturesInstallIn64BitMode=x64
PrivilegesRequired=admin
; 安装/卸载时检测占用 fire_tsf.dll 的进程，提示用户关闭
; CloseApplications=yes: 默认弹窗列出占用文件的应用，用户可关闭后继续
; RestartApplications=yes: 安装完成后自动重启被关闭的应用
CloseApplications=yes
RestartApplications=yes
; 卸载时弹确认对话框询问是否保留用户数据
UninstallDisplayIcon={app}\fire_config.exe
UninstallDisplayName={#MyAppName}

; ----------------------------------------------------------------------------
; 程序文件：fire_tsf.dll / fire_config.exe / 码表（供配置界面重新构建词库用）
; 程序目录只读，所有用户可写数据放 %APPDATA%\FireIME
; ----------------------------------------------------------------------------
[Files]
; 主程序（始终覆盖）
Source: "{#BuildTsfDir}\fire_tsf.dll";       DestDir: "{app}"; Flags: ignoreversion restartreplace
Source: "{#BuildConfigDir}\fire_config.exe"; DestDir: "{app}"; Flags: ignoreversion restartreplace
; 预构建词库（随包分发，避免用户安装时长时间等待 tablebuilder 构建 1.3MB 码表）
; 安装到用户数据目录，仅当目标不存在时复制（保留用户已有词库与动态调频）
Source: "{#StagingDir}\wb_py_dict.sqlite";   DestDir: "{userappdata}\FireIME"; Flags: onlyifdoesntexist
; 默认配置（仅首次安装写入，已存在则保留用户自定义）
Source: "{#StagingDir}\config.json";         DestDir: "{userappdata}\FireIME"; Flags: onlyifdoesntexist
; 码表（供 fire_config.exe 词库管理页导入/重建词库使用）
Source: "{#ResourcesDir}\wb_table.txt";      DestDir: "{app}\tables"; Flags: ignoreversion
Source: "{#ResourcesDir}\wb_98_table.txt";   DestDir: "{app}\tables"; Flags: ignoreversion
Source: "{#ResourcesDir}\py_table.txt";      DestDir: "{app}\tables"; Flags: ignoreversion

[Dirs]
; 程序目录与用户数据目录
Name: "{app}";                  Flags: uninsneveruninstall
Name: "{app}\tables";           Flags: uninsneveruninstall
Name: "{userappdata}\FireIME";  Flags: uninsneveruninstall

; ----------------------------------------------------------------------------
; 注册 TSF：regsvr32 /s fire_tsf.dll（DllRegisterServer 自包含 CLSID/Profile/Category 注册）
; ----------------------------------------------------------------------------
[Run]
; 安装完成后静默注册 TSF（/s 静默，DLL 已被 restartreplace 处理占用问题）
Filename: "{win}\System32\regsvr32.exe"; Parameters: "/s ""{app}\fire_tsf.dll"""; \
  StatusMsg: "{cm:StatusRegisteringIME}"; Flags: runhidden

; 安装完成后启动配置工具（可选，用户可在向导中勾选）
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchConfigTool}"; \
  Flags: nowait postinstall skipifsilent

; ----------------------------------------------------------------------------
; 卸载：先反注册 TSF，再删除程序文件；用户数据通过 uninsneveruninstall 保留
; ----------------------------------------------------------------------------
[UninstallRun]
; 反注册 TSF（DllUnregisterServer 删除 CLSID/Profile/Category）
Filename: "{win}\System32\regsvr32.exe"; Parameters: "/s /u ""{app}\fire_tsf.dll"""; \
  Flags: runhidden

; ----------------------------------------------------------------------------
; 卸载后清理程序目录（用户数据在 %APPDATA% 下，不会被删）
; Code 段在卸载阶段删除 tables 目录与残留文件
; ----------------------------------------------------------------------------
[UninstallDelete]
Type: filesandordirs; Name: "{app}\tables"
Type: dirifempty;     Name: "{app}"

; ----------------------------------------------------------------------------
; 「程序和功能」注册表项
; ----------------------------------------------------------------------------
[Registry]
; 程序和功能中的显示名 / 卸载入口（Inno Setup 自动维护 UninstallString，这里补 DisplayVersion 等）
Root: HKLM; Subkey: "Software\Microsoft\Windows\CurrentVersion\Uninstall\{#MyAppExeName}_is1"; \
  ValueType: string; ValueName: "DisplayVersion"; ValueData: "{#MyAppVersion}"; Flags: uninsdeletevalue
Root: HKLM; Subkey: "Software\Microsoft\Windows\CurrentVersion\Uninstall\{#MyAppExeName}_is1"; \
  ValueType: string; ValueName: "Publisher"; ValueData: "{#MyAppPublisher}"; Flags: uninsdeletevalue
Root: HKLM; Subkey: "Software\Microsoft\Windows\CurrentVersion\Uninstall\{#MyAppExeName}_is1"; \
  ValueType: string; ValueName: "DisplayIcon"; ValueData: "{app}\fire_config.exe"; Flags: uninsdeletevalue

; ----------------------------------------------------------------------------
; 卸载完成后询问是否删除用户数据（config / 词库 / 统计）
; ----------------------------------------------------------------------------
[CustomMessages]
; 简体中文本地化
StatusRegisteringIME=正在注册输入法...
LaunchConfigTool=启动配置工具(&L)
UninstallDataPrompt=是否删除用户数据（配置、词库、输入统计）？%n选择「否」将保留 %1 以便将来重装。

[Code]
function InitializeUninstall(): Boolean;
begin
  Result := True;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  userDataDir: String;
begin
  if CurUninstallStep = usPostUninstall then
  begin
    userDataDir := ExpandConstant('{userappdata}\FireIME');
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
