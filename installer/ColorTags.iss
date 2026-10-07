; Full per-user product. Registration separately requests administrator access
; for package certificate trust and the Explorer property handler.
;
; Built by CI:
;   ISCC.exe /DAppVersion=1.0.1 /DPayload=..\package installer\ColorTags.iss

#define AppName "ColorTags"
#define AppPublisher "vldpotapov"
#define AppUrl "https://github.com/vldpotapov/ColorTags"

#ifndef AppVersion
  #define AppVersion "0.0.0-dev"
#endif
#ifndef Payload
#define Payload "..\package"
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
; Keep setup unelevated so MSIX and HKCU belong to the installing user.
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
Source: "{#Payload}\Overlay\ColorTagsOverlay.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#Payload}\Overlay\Stop-ColorTagsOverlay.ps1"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#Payload}\Overlay\Set-ColorTagsSettings.ps1"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#Payload}\ShellExtension\*"; DestDir: "{app}\ShellExtension"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#Payload}\Runtime\*"; DestDir: "{app}\Runtime"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#Payload}\src\*"; DestDir: "{app}\src"; Excludes: "__pycache__\*,*.pyc"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "integrate.ps1"; DestDir: "{app}\installer"; Flags: ignoreversion
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
    Flags: nowait postinstall skipifsilent; Check: IntegrationReady

[UninstallRun]
Filename: "{sys}\WindowsPowerShell\v1.0\powershell.exe"; \
    Parameters: "-NoProfile -ExecutionPolicy Bypass -File ""{app}\Stop-ColorTagsOverlay.ps1"""; \
    Flags: runhidden; RunOnceId: "StopColorTagsOverlay"

[Code]
var
  IntegrationFailed: Boolean;

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

function RunIntegration(Uninstall: Boolean): Boolean;
var
  Params: String;
  ExitCode: Integer;
begin
  Params := '-NoProfile -ExecutionPolicy Bypass -File "' + ExpandConstant('{app}\installer\integrate.ps1') + '"';
  if Uninstall then Params := Params + ' -Uninstall';
  Result := Exec(ExpandConstant('{sys}\WindowsPowerShell\v1.0\powershell.exe'),
    Params, '', SW_HIDE, ewWaitUntilTerminated, ExitCode);
  if Result then Result := ExitCode = 0;
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssPostInstall then begin
    IntegrationFailed := not RunIntegration(False);
    if IntegrationFailed then
      RaiseException('ColorTags Explorer integration failed. Approve the administrator prompts and run setup again. Details: %LOCALAPPDATA%\Colortags\setup-integration.log');
  end;
end;

function GetCustomSetupExitCode(): Integer;
begin
  if IntegrationFailed then Result := 1 else Result := 0;
end;

function IntegrationReady(): Boolean;
begin
  Result := not IntegrationFailed;
end;

function ShouldSkipPage(PageID: Integer): Boolean;
begin
  Result := IntegrationFailed and (PageID = wpFinished);
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usUninstall then begin
    SignalOverlayStop();
    if not RunIntegration(True) then
      RaiseException('Explorer integration could not be removed. Approve administrator access and retry uninstall. Details: %LOCALAPPDATA%\Colortags\setup-integration.log');
  end;
end;
