#define AppName "AgPlayer"
#define AppVersion "1.0.0"
#define AppPublisher "AgPlayer"
#define AppURL "https://www.agplayer.com"
#define AppExeName "AgPlayer.exe"

[Setup]
AppId={{8A96E2B6-A6E6-45CD-9FCA-F64A0FE80234}
AppName={#AppName}
AppVersion={#AppVersion}
UninstallDisplayName={#AppName}
AppPublisher={#AppPublisher}
AppPublisherURL={#AppURL}
AppSupportURL={#AppURL}
AppUpdatesURL={#AppURL}
DefaultDirName={localappdata}\Programs\{#AppName}
DefaultGroupName={#AppName}
UninstallDisplayIcon={app}\{#AppExeName}
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=lowest
OutputDir=..\build\installer
OutputBaseFilename=AgPlayer-Setup-{#AppVersion}-x64
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
CloseApplications=yes
RestartApplications=no
SetupLogging=yes
DisableProgramGroupPage=yes
DisableDirPage=no
SetupIconFile=..\assets\brand\agplayer.ico

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "associateaudio"; Description: "注册常用音频文件关联（可在 Windows 设置中更改）"; Flags: unchecked

[Files]
Source: "..\build\package\AgPlayer\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\{#AppExeName}"; WorkingDir: "{app}"; IconFilename: "{app}\{#AppExeName}"; AppUserModelID: "AgPlayer.Desktop"; Flags: createonlyiffileexists
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExeName}"; WorkingDir: "{app}"; IconFilename: "{app}\{#AppExeName}"; AppUserModelID: "AgPlayer.Desktop"; Flags: createonlyiffileexists

[Registry]
Root: HKCU; Subkey: "Software\Classes\AppUserModelId\AgPlayer.Desktop"; ValueType: string; ValueName: "DisplayName"; ValueData: "{#AppName}"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\AppUserModelId\AgPlayer.Desktop"; ValueType: string; ValueName: "IconUri"; ValueData: "{app}\{#AppExeName},0"
Root: HKCU; Subkey: "Software\Classes\AppUserModelId\AgPlayer.Desktop"; ValueType: string; ValueName: "RelaunchCommand"; ValueData: """{app}\{#AppExeName}"""
Root: HKCU; Subkey: "Software\Classes\Applications\{#AppExeName}"; ValueType: string; ValueName: "FriendlyAppName"; ValueData: "{#AppName}"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\Applications\{#AppExeName}\DefaultIcon"; ValueType: string; ValueData: "{app}\{#AppExeName},0"
Root: HKCU; Subkey: "Software\Classes\Applications\{#AppExeName}\shell\open\command"; ValueType: string; ValueData: """{app}\{#AppExeName}"" ""%1"""
Root: HKCU; Subkey: "Software\Classes\AgPlayer.Audio"; ValueType: string; ValueData: "AgPlayer Audio"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\AgPlayer.Audio\DefaultIcon"; ValueType: string; ValueData: "{app}\{#AppExeName},0"
Root: HKCU; Subkey: "Software\Classes\AgPlayer.Audio\shell\open\command"; ValueType: string; ValueData: """{app}\{#AppExeName}"" ""%1"""
Root: HKCU; Subkey: "Software\AgPlayer\Capabilities"; ValueType: string; ValueName: "ApplicationName"; ValueData: "{#AppName}"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\RegisteredApplications"; ValueType: string; ValueName: "AgPlayer"; ValueData: "Software\AgPlayer\Capabilities"; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\Classes\Applications\{#AppExeName}\SupportedTypes"; ValueType: string; ValueName: ".mp3"; ValueData: ""
Root: HKCU; Subkey: "Software\Classes\.mp3\OpenWithProgids"; ValueType: string; ValueName: "AgPlayer.Audio"; ValueData: ""; Flags: uninsdeletevalue; Tasks: associateaudio
Root: HKCU; Subkey: "Software\AgPlayer\Capabilities\FileAssociations"; ValueType: string; ValueName: ".mp3"; ValueData: "AgPlayer.Audio"
#define AudioExt "wav"
Root: HKCU; Subkey: "Software\Classes\Applications\{#AppExeName}\SupportedTypes"; ValueType: string; ValueName: ".{#AudioExt}"; ValueData: ""
Root: HKCU; Subkey: "Software\Classes\.{#AudioExt}\OpenWithProgids"; ValueType: string; ValueName: "AgPlayer.Audio"; ValueData: ""; Flags: uninsdeletevalue; Tasks: associateaudio
Root: HKCU; Subkey: "Software\AgPlayer\Capabilities\FileAssociations"; ValueType: string; ValueName: ".{#AudioExt}"; ValueData: "AgPlayer.Audio"
#undef AudioExt
#define AudioExt "flac"
Root: HKCU; Subkey: "Software\Classes\Applications\{#AppExeName}\SupportedTypes"; ValueType: string; ValueName: ".{#AudioExt}"; ValueData: ""
Root: HKCU; Subkey: "Software\Classes\.{#AudioExt}\OpenWithProgids"; ValueType: string; ValueName: "AgPlayer.Audio"; ValueData: ""; Flags: uninsdeletevalue; Tasks: associateaudio
Root: HKCU; Subkey: "Software\AgPlayer\Capabilities\FileAssociations"; ValueType: string; ValueName: ".{#AudioExt}"; ValueData: "AgPlayer.Audio"
#undef AudioExt
#define AudioExt "aac"
Root: HKCU; Subkey: "Software\Classes\Applications\{#AppExeName}\SupportedTypes"; ValueType: string; ValueName: ".{#AudioExt}"; ValueData: ""
Root: HKCU; Subkey: "Software\Classes\.{#AudioExt}\OpenWithProgids"; ValueType: string; ValueName: "AgPlayer.Audio"; ValueData: ""; Flags: uninsdeletevalue; Tasks: associateaudio
Root: HKCU; Subkey: "Software\AgPlayer\Capabilities\FileAssociations"; ValueType: string; ValueName: ".{#AudioExt}"; ValueData: "AgPlayer.Audio"
#undef AudioExt
#define AudioExt "m4a"
Root: HKCU; Subkey: "Software\Classes\Applications\{#AppExeName}\SupportedTypes"; ValueType: string; ValueName: ".{#AudioExt}"; ValueData: ""
Root: HKCU; Subkey: "Software\Classes\.{#AudioExt}\OpenWithProgids"; ValueType: string; ValueName: "AgPlayer.Audio"; ValueData: ""; Flags: uninsdeletevalue; Tasks: associateaudio
Root: HKCU; Subkey: "Software\AgPlayer\Capabilities\FileAssociations"; ValueType: string; ValueName: ".{#AudioExt}"; ValueData: "AgPlayer.Audio"
#undef AudioExt
#define AudioExt "ogg"
Root: HKCU; Subkey: "Software\Classes\Applications\{#AppExeName}\SupportedTypes"; ValueType: string; ValueName: ".{#AudioExt}"; ValueData: ""
Root: HKCU; Subkey: "Software\Classes\.{#AudioExt}\OpenWithProgids"; ValueType: string; ValueName: "AgPlayer.Audio"; ValueData: ""; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\AgPlayer\Capabilities\FileAssociations"; ValueType: string; ValueName: ".{#AudioExt}"; ValueData: "AgPlayer.Audio"
#undef AudioExt

[Code]
procedure CurStepChanged(CurStep: TSetupStep);
var
  ResultCode: Integer;
begin
  if CurStep = ssPostInstall then begin
    { [Icons] recreates shortcuts on upgrades; invalidate the Shell icon cache. }
    ShellExec('', ExpandConstant('{cmd}'),
      '/c ie4uinit.exe -show', '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
  end;
end;

function InitializeUninstall(): Boolean;
begin
  Result := True;
  { Silent enterprise/test uninstall keeps user data and must not block. }
  if not UninstallSilent then begin
    if MsgBox('是否删除个人歌单、收藏和应用设置？'#13#10#13#10 +
              '选择“是”将删除 AgPlayer 的个人数据；不会删除任何音乐文件。',
              mbConfirmation, MB_YESNO) = IDYES then begin
      DelTree(ExpandConstant('{userappdata}\\AgPlayer'), True, True, True);
      DelTree(ExpandConstant('{localappdata}\\AgPlayer'), True, True, True);
    end;
  end;
end;

[Run]
Filename: "{app}\{#AppExeName}"; Description: "启动 {#AppName}"; Flags: nowait postinstall skipifsilent
