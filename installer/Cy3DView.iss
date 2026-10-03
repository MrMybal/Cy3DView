; SPDX-License-Identifier: GPL-3.0-only
; Copyright (C) 2026 Cyberalien
; Build with ISCC /DAppVersion=x.y.z /DPackageDir=... /DOutputDir=... Cy3DView.iss
#ifndef AppVersion
  #error AppVersion is required
#endif
#ifndef PackageDir
  #define PackageDir "..\dist\Cy3DView"
#endif
#ifndef OutputDir
  #define OutputDir "..\dist"
#endif
#ifndef AppIdentity
  #define AppIdentity "{C2D6A3D0-6125-453A-A8AE-F9CA4BC175EA}"
#endif
#ifndef RegistryName
  #define RegistryName "Cy3DView.exe"
#endif

[Setup]
AppId={{#AppIdentity}
AppName=Cy3DView
AppVersion={#AppVersion}
AppPublisher=Cyberalien
AppPublisherURL=https://github.com/MrMybal/Cy3DView
AppSupportURL=https://github.com/MrMybal/Cy3DView/issues
AppUpdatesURL=https://github.com/MrMybal/Cy3DView/releases
DefaultDirName={localappdata}\Programs\Cy3DView
DefaultGroupName=Cy3DView
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
DisableProgramGroupPage=yes
LicenseFile=..\LICENSE
SetupIconFile=..\assets\cy3dview.ico
UninstallDisplayIcon={app}\Cy3DView.exe
OutputDir={#OutputDir}
OutputBaseFilename=Cy3DView-{#AppVersion}-win64-setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
LanguageDetectionMethod=none
UsePreviousLanguage=yes
CloseApplications=yes
RestartApplications=no
AppMutex=Local\Cy3DView.App
VersionInfoCompany=Cyberalien
VersionInfoCopyright=Copyright (C) 2026 Cyberalien
VersionInfoDescription=Cy3DView installer
ChangesAssociations=yes

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "french"; MessagesFile: "compiler:Languages\French.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "{#PackageDir}\*"; DestDir: "{app}"; Excludes: "Cy3DView.ini,*.log,*.pdb,*.ilk"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\Cy3DView"; Filename: "{app}\Cy3DView.exe"; WorkingDir: "{app}"
Name: "{group}\{cm:UninstallProgram,Cy3DView}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\Cy3DView"; Filename: "{app}\Cy3DView.exe"; WorkingDir: "{app}"; Tasks: desktopicon

[Registry]
; Offer Open with without changing the user's default application.
Root: HKCU; Subkey: "Software\Classes\Applications\{#RegistryName}"; ValueType: string; ValueName: "FriendlyAppName"; ValueData: "Cy3DView"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\Applications\{#RegistryName}\shell\open\command"; ValueType: string; ValueData: """{app}\Cy3DView.exe"" ""%1"""

[Run]
Filename: "{app}\Cy3DView.exe"; Description: "{cm:LaunchProgram,Cy3DView}"; Flags: nowait postinstall skipifsilent

[Code]
const
  SYNCHRONIZE = $00100000;
  WAIT_TIMEOUT = 258;
function OpenProcess(Access: Cardinal; Inherit: Boolean; ProcessId: Cardinal): THandle;
  external 'OpenProcess@kernel32.dll stdcall';
function WaitForSingleObject(Handle: THandle; Milliseconds: Cardinal): Cardinal;
  external 'WaitForSingleObject@kernel32.dll stdcall';
function CloseHandle(Handle: THandle): Boolean;
  external 'CloseHandle@kernel32.dll stdcall';
function InitializeSetup(): Boolean;
var
  Pid: Integer;
  Process: THandle;
begin
  Result := True;
  Pid := StrToIntDef(ExpandConstant('{param:WAITPID|0}'), 0);
  if Pid > 0 then begin
    Process := OpenProcess(SYNCHRONIZE, False, Pid);
    if Process <> 0 then begin
      Result := WaitForSingleObject(Process, 30000) <> WAIT_TIMEOUT;
      CloseHandle(Process);
    end;
  end;
end;
