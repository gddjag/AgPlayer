#define AppName "AgPlayer"
#ifndef AppVersion
  #error AppVersion must be supplied by scripts/package-windows.ps1
#endif
#define AppPublisher "AgPlayer"
#define AppURL "https://www.agplayer.com"
#define AppExeName "AgPlayer.exe"

[Setup]
AppId={{8A96E2B6-A6E6-45CD-9FCA-F64A0FE80234}
AppName={#AppName}
AppVersion={#AppVersion}
VersionInfoVersion={#AppVersion}.0
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
ShowLanguageDialog=no
LanguageDetectionMethod=none
CloseApplications=yes
RestartApplications=no
SetupLogging=yes
DisableProgramGroupPage=yes
DisableDirPage=no
SetupIconFile=..\assets\brand\agplayer.ico
WizardImageFile=..\assets\brand\installer-wizard.png
WizardSmallImageFile=..\assets\brand\installer-small.png

[Languages]
Name: "chinesesimplified"; MessagesFile: "languages\ChineseSimplified.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[CustomMessages]
chinesesimplified.AssociateAudioTask=注册常用音频文件关联（可在 Windows 设置中更改）
english.AssociateAudioTask=Register common audio file associations (can be changed in Windows Settings)
chinesesimplified.LaunchAgPlayer=启动 {#AppName}
english.LaunchAgPlayer=Launch {#AppName}
chinesesimplified.UninstallPersonalDataPrompt=是否删除个人歌单、收藏和应用设置？选择“是”将删除 AgPlayer 的个人数据，但不会删除任何音乐文件。
english.UninstallPersonalDataPrompt=Delete personal playlists, favorites, and app settings? Choosing Yes removes AgPlayer personal data but never deletes music files.

[Tasks]
Name: "associateaudio"; Description: "{cm:AssociateAudioTask}"; Flags: unchecked

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
Root: HKCU; Subkey: "Software\Classes\AgPlayerAudioFile"; ValueType: string; ValueData: "AgPlayer Audio"; Flags: uninsdeletekey
; Historical installers used this ProgID. Register uninstall cleanup only.
Root: HKCU; Subkey: "Software\Classes\AgPlayer.Audio"; ValueType: none; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\AgPlayerAudioFile\DefaultIcon"; ValueType: string; ValueData: "{app}\{#AppExeName},0"
Root: HKCU; Subkey: "Software\Classes\AgPlayerAudioFile\shell\open\command"; ValueType: string; ValueData: """{app}\{#AppExeName}"" ""%1"""
Root: HKCU; Subkey: "Software\AgPlayer\Capabilities"; ValueType: string; ValueName: "ApplicationName"; ValueData: "{#AppName}"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\RegisteredApplications"; ValueType: string; ValueName: "AgPlayer"; ValueData: "Software\AgPlayer\Capabilities"; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\Classes\Applications\{#AppExeName}\SupportedTypes"; ValueType: string; ValueName: ".mp3"; ValueData: ""
Root: HKCU; Subkey: "Software\Classes\.mp3\OpenWithProgids"; ValueType: string; ValueName: "AgPlayerAudioFile"; ValueData: ""; Flags: uninsdeletevalue; Tasks: associateaudio
Root: HKCU; Subkey: "Software\AgPlayer\Capabilities\FileAssociations"; ValueType: string; ValueName: ".mp3"; ValueData: "AgPlayerAudioFile"
#define AudioExt "wav"
Root: HKCU; Subkey: "Software\Classes\Applications\{#AppExeName}\SupportedTypes"; ValueType: string; ValueName: ".{#AudioExt}"; ValueData: ""
Root: HKCU; Subkey: "Software\Classes\.{#AudioExt}\OpenWithProgids"; ValueType: string; ValueName: "AgPlayerAudioFile"; ValueData: ""; Flags: uninsdeletevalue; Tasks: associateaudio
Root: HKCU; Subkey: "Software\AgPlayer\Capabilities\FileAssociations"; ValueType: string; ValueName: ".{#AudioExt}"; ValueData: "AgPlayerAudioFile"
#undef AudioExt
#define AudioExt "flac"
Root: HKCU; Subkey: "Software\Classes\Applications\{#AppExeName}\SupportedTypes"; ValueType: string; ValueName: ".{#AudioExt}"; ValueData: ""
Root: HKCU; Subkey: "Software\Classes\.{#AudioExt}\OpenWithProgids"; ValueType: string; ValueName: "AgPlayerAudioFile"; ValueData: ""; Flags: uninsdeletevalue; Tasks: associateaudio
Root: HKCU; Subkey: "Software\AgPlayer\Capabilities\FileAssociations"; ValueType: string; ValueName: ".{#AudioExt}"; ValueData: "AgPlayerAudioFile"
#undef AudioExt
#define AudioExt "aac"
Root: HKCU; Subkey: "Software\Classes\Applications\{#AppExeName}\SupportedTypes"; ValueType: string; ValueName: ".{#AudioExt}"; ValueData: ""
Root: HKCU; Subkey: "Software\Classes\.{#AudioExt}\OpenWithProgids"; ValueType: string; ValueName: "AgPlayerAudioFile"; ValueData: ""; Flags: uninsdeletevalue; Tasks: associateaudio
Root: HKCU; Subkey: "Software\AgPlayer\Capabilities\FileAssociations"; ValueType: string; ValueName: ".{#AudioExt}"; ValueData: "AgPlayerAudioFile"
#undef AudioExt
#define AudioExt "m4a"
Root: HKCU; Subkey: "Software\Classes\Applications\{#AppExeName}\SupportedTypes"; ValueType: string; ValueName: ".{#AudioExt}"; ValueData: ""
Root: HKCU; Subkey: "Software\Classes\.{#AudioExt}\OpenWithProgids"; ValueType: string; ValueName: "AgPlayerAudioFile"; ValueData: ""; Flags: uninsdeletevalue; Tasks: associateaudio
Root: HKCU; Subkey: "Software\AgPlayer\Capabilities\FileAssociations"; ValueType: string; ValueName: ".{#AudioExt}"; ValueData: "AgPlayerAudioFile"
#undef AudioExt
#define AudioExt "ogg"
Root: HKCU; Subkey: "Software\Classes\Applications\{#AppExeName}\SupportedTypes"; ValueType: string; ValueName: ".{#AudioExt}"; ValueData: ""
Root: HKCU; Subkey: "Software\Classes\.{#AudioExt}\OpenWithProgids"; ValueType: string; ValueName: "AgPlayerAudioFile"; ValueData: ""; Flags: uninsdeletevalue; Tasks: associateaudio
Root: HKCU; Subkey: "Software\AgPlayer\Capabilities\FileAssociations"; ValueType: string; ValueName: ".{#AudioExt}"; ValueData: "AgPlayerAudioFile"
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

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  Keys: TArrayOfString;
  I: Integer;
  OpenWith: String;
begin
  if CurUninstallStep <> usPostUninstall then Exit;
  { Runtime associations may have been added after installation, including
    extensions not selected in Setup. Remove only our two owned values;
    never delete shared extension keys or another player's defaults. }
  if RegGetSubkeyNames(HKCU, 'Software\Classes', Keys) then begin
    for I := 0 to GetArrayLength(Keys) - 1 do begin
      if Copy(Keys[I], 1, 1) = '.' then begin
        OpenWith := 'Software\Classes\' + Keys[I] + '\OpenWithProgids';
        RegDeleteValue(HKCU, OpenWith, 'AgPlayerAudioFile');
        RegDeleteValue(HKCU, OpenWith, 'AgPlayer.Audio');
      end;
    end;
  end;
end;

function InitializeUninstall(): Boolean;
begin
  Result := True;
  { Silent enterprise/test uninstall keeps user data and must not block. }
  if not UninstallSilent then begin
    if MsgBox(ExpandConstant('{cm:UninstallPersonalDataPrompt}'),
              mbConfirmation, MB_YESNO) = IDYES then begin
      DelTree(ExpandConstant('{userappdata}\\AgPlayer'), True, True, True);
      DelTree(ExpandConstant('{localappdata}\\AgPlayer'), True, True, True);
    end;
  end;
end;

[Run]
Filename: "{app}\{#AppExeName}"; Description: "{cm:LaunchAgPlayer}"; Flags: nowait postinstall skipifsilent
