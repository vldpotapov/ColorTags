; Inno Setup script for the ColorTags overlay.
;
; Deliberately only the overlay: it needs no administrator rights and no
; certificate, so it installs the same way for everyone. The shell extension
; (context menu and the Tags column) is registered separately — it packs and
; signs an MSIX, which needs the Windows SDK and a certificate the machine
; trusts, and that does not belong in a per-user installer.
;
; Built by CI:
;   ISCC.exe /DAppVersion=1.0.0 /DPayload=..\package\Overlay installer\ColorTags.iss

#define AppName "ColorTags"
#define AppPublisher "vldpotapov"
#define AppUrl "https://github.com/vldpotapov/ColorTags"

#ifndef AppVersion
  #define AppVersion "0.0.0-dev"
#endif
#ifndef Payload
  #define Payload "..\package\Overlay"
#endif

[Setup]
AppId={{8E9A4E13-6D27-4C2E-9D3B-2C5E8B7F4A61}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#AppPublisher}
AppPublisherURL={#AppUrl}
AppSupportURL={#AppUrl}/issues
AppUpdatesURL={#AppUrl}/releases
; Per-user: no UAC prompt, and nothing is written outside the user's profile.
PrivilegesRequired=lowest
DefaultDirName={localappdata}\Programs\ColorTags
DefaultGroupName=ColorTags
DisableProgramGroupPage=yes
DisableDirPage=auto
LicenseFile=..\LICENSE
OutputDir=..\dist
OutputBaseFilename=ColorTags-Setup-{#AppVersion}
SetupIconFile=assets\setup-icon.ico
WizardImageFile=assets\wizard-image.png
UninstallDisplayIcon={app}\ColorTagsOverlay.exe
UninstallDisplayName={#AppName} {#AppVersion}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
; Restart Manager offers to close an installed copy before replacing its EXE.
; Our own stop event is signalled first, including for a portable copy.
CloseApplications=yes
CloseApplicationsFilter=ColorTagsOverlay.exe
RestartApplications=no

[Tasks]
Name: "startup"; Description: "{cm:AutoStartProgram,ColorTags}"

[Files]
Source: "{#Payload}\ColorTagsOverlay.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#Payload}\Stop-ColorTagsOverlay.ps1"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#Payload}\Set-ColorTagsSettings.ps1"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\README.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\CHANGELOG.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\INSTALL.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\LICENSE"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\ColorTags"; Filename: "{app}\ColorTagsOverlay.exe"
Name: "{group}\{cm:UninstallProgram,ColorTags}"; Filename: "{uninstallexe}"

[Registry]
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; \
    ValueType: string; ValueName: "ColorTags"; \
    ValueData: """{app}\ColorTagsOverlay.exe"""; \
    Flags: uninsdeletevalue; Tasks: startup

[Run]
Filename: "{app}\ColorTagsOverlay.exe"; \
    Description: "{cm:LaunchProgram,ColorTags}"; \
    Flags: nowait postinstall skipifsilent

[UninstallRun]
Filename: "{sys}\WindowsPowerShell\v1.0\powershell.exe"; \
    Parameters: "-NoProfile -ExecutionPolicy Bypass -File ""{app}\Stop-ColorTagsOverlay.ps1"""; \
    Flags: runhidden; RunOnceId: "StopColorTagsOverlay"

[Code]
// The running overlay watches this per-user event, including portable copies.
// If an installed copy still holds a file, Restart Manager asks to close it.
function OpenEventW(DesiredAccess: LongWord; InheritHandle: Boolean;
  Name: string): LongWord;
  external 'OpenEventW@kernel32.dll stdcall';
function SetEvent(EventHandle: LongWord): Boolean;
  external 'SetEvent@kernel32.dll stdcall';
function CloseHandle(Handle: LongWord): Boolean;
  external 'CloseHandle@kernel32.dll stdcall';

procedure SignalOverlayStop();
var
  EventHandle: LongWord;
begin
  EventHandle := OpenEventW(2, False, 'Local\ColorTags.NativeOverlay.Stop');
  if EventHandle <> 0 then begin
    SetEvent(EventHandle);
    CloseHandle(EventHandle);
  end;
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  SignalOverlayStop();
  Result := '';
end;
