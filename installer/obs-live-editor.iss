#ifndef MyAppVersion
  #define MyAppVersion "0.1.0"
#endif
#ifndef BuildRoot
  #define BuildRoot "..\release\RelWithDebInfo\obs-live-editor"
#endif
#ifndef OutputRoot
  #define OutputRoot "..\release"
#endif

#define MyAppName "Live Editor for OBS"
#define MyAppPublisher "OBS Live Editor contributors"
#define MyAppExeName "obs64.exe"

[Setup]
AppId={{39D4249E-A2FB-4DBE-90D2-891D70A55AA5}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
DefaultDirName={autopf}\OBS Live Editor
DisableDirPage=yes
DisableProgramGroupPage=yes
OutputDir={#OutputRoot}
OutputBaseFilename=obs-live-editor-{#MyAppVersion}-windows-x64-setup
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
CloseApplications=force
RestartApplications=no
UninstallDisplayName={#MyAppName}
VersionInfoVersion={#MyAppVersion}
VersionInfoProductName={#MyAppName}
VersionInfoCompany={#MyAppPublisher}
VersionInfoDescription=OBS live information editor plugin installer

[Languages]
Name: "korean"; MessagesFile: "compiler:Languages\Korean.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[Files]
Source: "{#BuildRoot}\bin\64bit\obs-live-editor.dll"; DestDir: "{code:GetObsPath}\obs-plugins\64bit"; Flags: ignoreversion restartreplace uninsrestartdelete
Source: "{#BuildRoot}\bin\64bit\tls\qschannelbackend.dll"; DestDir: "{code:GetObsPath}\bin\64bit\tls"; Flags: ignoreversion restartreplace uninsrestartdelete
Source: "{#BuildRoot}\data\*"; DestDir: "{code:GetObsPath}\data\obs-plugins\obs-live-editor"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\OBS Studio"; Filename: "{code:GetObsPath}\bin\64bit\{#MyAppExeName}"; WorkingDir: "{code:GetObsPath}\bin\64bit"
Name: "{group}\{cm:UninstallProgram,{#MyAppName}}"; Filename: "{uninstallexe}"

[Run]
Filename: "{code:GetObsPath}\bin\64bit\{#MyAppExeName}"; Description: "{cm:LaunchProgram,OBS Studio}"; WorkingDir: "{code:GetObsPath}\bin\64bit"; Flags: nowait postinstall skipifsilent

[Code]
var
  CachedObsPath: string;

function DetectObsPath(): string;
var
  UninstallCommand: string;
  Candidate: string;
begin
  Result := '';
  if RegQueryStringValue(HKLM32,
      'SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\OBS Studio',
      'UninstallString', UninstallCommand) then
  begin
    Candidate := ExtractFileDir(RemoveQuotes(UninstallCommand));
    if FileExists(AddBackslash(Candidate) + 'bin\64bit\{#MyAppExeName}') then
      Result := Candidate;
  end;

  if Result = '' then
  begin
    Candidate := ExpandConstant('{autopf}\obs-studio');
    if FileExists(AddBackslash(Candidate) + 'bin\64bit\{#MyAppExeName}') then
      Result := Candidate;
  end;
end;

function GetObsPath(Param: string): string;
begin
  if CachedObsPath = '' then
    CachedObsPath := DetectObsPath();
  Result := CachedObsPath;
end;

function InitializeSetup(): Boolean;
begin
  CachedObsPath := DetectObsPath();
  Result := CachedObsPath <> '';
  if not Result then
    MsgBox('64비트 OBS Studio 설치를 찾지 못했습니다. OBS를 먼저 설치해 주세요.', mbError, MB_OK);
end;
