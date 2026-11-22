; Inno Setup script for BatteryHub (https://jrsoftware.org/isinfo.php).
; Per-user install by default: no administrator rights needed, installs to %LOCALAPPDATA%\Programs.
; Setup also offers "Install for all users" (Program Files, needs administrator rights).
; Build with installer\build-installer.ps1 (or: ISCC /DAppVersion=1.2.3 installer\BatteryHub.iss).

#ifndef AppVersion
  #define AppVersion "1.0.0"
#endif
#define Root ".."

[Setup]
AppId={{7E0B5C2A-3F4D-4C8E-9B61-2D5A8F1C4E93}
AppName=BatteryHub
AppVersion={#AppVersion}
AppVerName=BatteryHub {#AppVersion}
AppPublisher=BatteryHub contributors
DefaultDirName={autopf}\BatteryHub
DisableProgramGroupPage=yes
DisableDirPage=auto
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
OutputDir={#Root}\dist
OutputBaseFilename=BatteryHub-Setup-{#AppVersion}
SetupIconFile={#Root}\res\app.ico
UninstallDisplayIcon={app}\BatteryHub.exe
UninstallDisplayName=BatteryHub
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
CloseApplications=no
; English, or Simplified Chinese on a Chinese Windows (PR #43); no language prompt.
ShowLanguageDialog=auto

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "chinesesimp"; MessagesFile: "Languages\ChineseSimplified.isl"

[CustomMessages]
english.AutostartTask=Start BatteryHub when I sign in to Windows
chinesesimp.AutostartTask=登录 Windows 时启动 BatteryHub

[Tasks]
Name: "autostart"; Description: "{cm:AutostartTask}"

[Files]
Source: "{#Root}\BatteryHub.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#Root}\third_party\headsetcontrol\headsetcontrol.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#Root}\LICENSE"; DestDir: "{app}"; DestName: "LICENSE.txt"; Flags: ignoreversion
Source: "{#Root}\installer\THIRD_PARTY_NOTICES.txt"; DestDir: "{app}"; Flags: ignoreversion

[InstallDelete]
; Before 1.1.0 the app was called Wireless Battery: remove its exe and Start menu shortcut on upgrade.
Type: files; Name: "{app}\WirelessBattery.exe"
Type: files; Name: "{autoprograms}\Wireless Battery.lnk"

[Icons]
Name: "{autoprograms}\BatteryHub"; Filename: "{app}\BatteryHub.exe"

[Run]
Filename: "{app}\BatteryHub.exe"; Description: "Start BatteryHub now"; Flags: nowait postinstall skipifsilent; Check: not IsUpdate
Filename: "https://ko-fi.com/krisgfx"; Description: "Enjoying BatteryHub? Support it on Ko-fi"; Flags: shellexec postinstall unchecked skipifsilent nowait runasoriginaluser

[UninstallRun]
Filename: "{app}\BatteryHub.exe"; Parameters: "--quit"; Flags: runhidden waituntilterminated; RunOnceId: "QuitApp"

[Code]
const
  RunKey = 'Software\Microsoft\Windows\CurrentVersion\Run';

function IsUpdate(): Boolean;
begin
  Result := ExpandConstant('{param:WBUPDATE|0}') = '1';
end;

{ Closes the running app by asking its window to close, the same thing "BatteryHub.exe --quit" does,
  without starting the installed exe: antivirus programs such as Norton CyberCapture intercept an
  unsigned exe that is started, lock it while they analyse it, and then setup could not replace it
  ("DeleteFile failed; code 5. Access is denied."). Waits up to 30 s for the window to go away. }
function QuitRunningApp(): Boolean;
var
  Wnd: HWND;
  I: Integer;
begin
  Wnd := FindWindowByClassName('WirelessBatteryWnd');
  if Wnd <> 0 then
  begin
    PostMessage(Wnd, 16 { WM_CLOSE }, 0, 0);
    for I := 1 to 300 do
    begin
      if FindWindowByClassName('WirelessBatteryWnd') = 0 then
        break;
      Sleep(100);
    end;
  end;
  Result := FindWindowByClassName('WirelessBatteryWnd') = 0;
end;

{ Deletes the installed exe before the new one is copied, retrying for up to a minute: right after
  the app closes, or while an antivirus still scans the file, it can stay locked for a while. }
function FreeOldExe(Name: String): Boolean;
var
  Path: String;
  I: Integer;
begin
  Path := ExpandConstant('{app}\') + Name;
  Result := True;
  if not FileExists(Path) then
    exit;
  for I := 1 to 60 do
  begin
    if DeleteFile(Path) then
      exit;
    Sleep(1000);
  end;
  Result := not FileExists(Path);
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  Result := '';
  if not QuitRunningApp() then
    Result := 'BatteryHub could not close. Close it from its tray menu (Exit) and run Setup again.'
  else if not FreeOldExe('BatteryHub.exe') or not FreeOldExe('headsetcontrol.exe') then
    Result := 'Setup cannot replace BatteryHub.exe: another program keeps it locked.' + #13#10#13#10 +
      'This is usually an antivirus (for example Norton CyberCapture) analysing the file. ' +
      'Wait for it to finish, or allow BatteryHub in the antivirus, then run Setup again.';
end;

function InstalledRunValue(): String;
begin
  Result := '"' + ExpandConstant('{app}\BatteryHub.exe') + '"';
end;

{ The startup entry of this copy before the rename (Wireless Battery, before 1.1.0). The value name
  stays 'WirelessBattery' so existing entries keep working. }
function OldRunValue(): String;
begin
  Result := '"' + ExpandConstant('{app}\WirelessBattery.exe') + '"';
end;

{ Removes the startup entry only if it points at this installed copy, so a portable copy's
  "Start with Windows" setting is left alone. }
procedure RemoveOwnAutostart();
var
  Value: String;
begin
  if RegQueryStringValue(HKCU, RunKey, 'WirelessBattery', Value) and
     ((CompareText(Value, InstalledRunValue()) = 0) or (CompareText(Value, OldRunValue()) = 0)) then
    RegDeleteValue(HKCU, RunKey, 'WirelessBattery');
end;

{ An entry that still starts the old exe of this copy now starts BatteryHub.exe (also on silent updates). }
procedure MigrateAutostart();
var
  Value: String;
begin
  if RegQueryStringValue(HKCU, RunKey, 'WirelessBattery', Value) and (CompareText(Value, OldRunValue()) = 0) then
    RegWriteStringValue(HKCU, RunKey, 'WirelessBattery', InstalledRunValue());
end;

{ True if this account can create the chosen folder (or write to it if it already exists).
  Without administrator rights, folders such as D:\Programs or Program Files are often read-only. }
function CanWriteTo(Dir: String): Boolean;
var
  Parent, Probe: String;
begin
  Dir := RemoveBackslashUnlessRoot(Dir);
  if DirExists(Dir) then
  begin
    Probe := AddBackslash(Dir) + '~wirelessbattery-write-test.tmp';
    Result := SaveStringToFile(Probe, '', False);
    DeleteFile(Probe);
    exit;
  end;
  { Find the nearest folder that exists and check that a new folder can be created in it. }
  Parent := Dir;
  while not DirExists(Parent) do
  begin
    if ExtractFileDir(Parent) = Parent then
    begin
      Result := False;  { the drive itself does not exist }
      exit;
    end;
    Parent := ExtractFileDir(Parent);
  end;
  Probe := AddBackslash(Parent) + '~wirelessbattery-write-test';
  Result := CreateDir(Probe);
  if Result then
    RemoveDir(Probe);
end;

function NextButtonClick(CurPageID: Integer): Boolean;
begin
  Result := True;
  { Not in silent installs: there the wizard would wait on a hidden page forever. }
  if (CurPageID = wpSelectDir) and not WizardSilent() and not CanWriteTo(WizardDirValue()) then
  begin
    if IsAdminInstallMode() then
      MsgBox('Setup cannot create or write to this folder:' + #13#10 + WizardDirValue() + #13#10#13#10 +
        'Please choose a different folder.', mbError, MB_OK)
    else
      MsgBox('Your account cannot create or write to this folder without administrator rights:' + #13#10 +
        WizardDirValue() + #13#10#13#10 +
        'Choose a folder you own (the default one always works), or close Setup, run it again and ' +
        'pick "Install for all users".', mbError, MB_OK);
    Result := False;
  end;
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  { The running app is closed and the old exe freed in PrepareToInstall (updates and manual installs). }
  if CurStep = ssPostInstall then
    MigrateAutostart();
  if (CurStep = ssPostInstall) and not IsUpdate() then
  begin
    { Same value the app's own "Start with Windows" menu item writes, so the two stay in sync. }
    if WizardIsTaskSelected('autostart') then
      RegWriteStringValue(HKCU, RunKey, 'WirelessBattery', InstalledRunValue())
    else
      RemoveOwnAutostart();
  end;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usPostUninstall then
  begin
    RemoveOwnAutostart();
    RegDeleteKeyIncludingSubkeys(HKCU, 'Software\WirelessBattery');
  end;
end;
