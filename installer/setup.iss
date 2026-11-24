; SPDX-License-Identifier: GPL-3.0-or-later
; Inno Setup 6 script for Kagami Cast. build-all.sh compiles it with:
;   ISCC /DAppName=.. /DAppVersion=.. /DAppPublisher=.. /DAppURL=.. /DAppId=..
;        /DDistDir=<dist> /DOutputDir=<build\release> installer\setup.iss
; (values come from product.conf and app/CMakeLists.txt)
; This file is UTF-8 with BOM because of the Thai messages below.

#ifndef AppName
  #define AppName "Kagami Cast"
#endif
#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef AppPublisher
  #define AppPublisher AppName + " contributors"
#endif
#ifndef AppURL
  #define AppURL ""
#endif
#ifndef AppId
  #error AppId missing: pass /DAppId=<APP_INSTALLER_ID from product.conf>
#endif
#ifndef DistDir
  #define DistDir "..\dist"
#endif
#ifndef OutputDir
  #define OutputDir "..\build\release"
#endif
#define AppExe    "KagamiCast.exe"
#define FwRuleTcp AppName + " (TCP)"
#define FwRuleUdp AppName + " (UDP)"

[Setup]
; "{{" is a literal "{" — AppId itself is "{GUID}"
AppId={{#AppId}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#AppPublisher}
#if AppURL != ""
AppPublisherURL={#AppURL}
AppSupportURL={#AppURL}
AppUpdatesURL={#AppURL}
#endif
VersionInfoVersion={#AppVersion}
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
; admin: Program Files, Windows Firewall rules and the Bonjour service
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
OutputDir={#OutputDir}
OutputBaseFilename=KagamiCast-{#AppVersion}-setup
SetupIconFile=..\app\app.ico
UninstallDisplayIcon={app}\{#AppExe}
UninstallDisplayName={#AppName}
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
; close a running Kagami Cast (and its uxplay) before replacing files on upgrade
CloseApplications=yes
RestartApplications=no

[Languages]
Name: "en"; MessagesFile: "compiler:Default.isl"
Name: "th"; MessagesFile: "compiler:Languages\Thai.isl"

[CustomMessages]
en.FirewallGroup=Windows Firewall:
th.FirewallGroup=Windows Firewall:
en.TaskPublic=Also allow on Public networks (not recommended: anyone on the same Wi-Fi could cast to this PC)
th.TaskPublic=อนุญาตบนเครือข่าย Public ด้วย (ไม่แนะนำ: ใครอยู่ Wi-Fi เดียวกันก็ส่งภาพขึ้นจอได้)
en.StatusFirewall=Adding Windows Firewall rules...
th.StatusFirewall=กำลังเพิ่มกฎ Windows Firewall...
en.StatusBonjour=Installing Apple Bonjour (lets the iPhone find this PC)...
th.StatusBonjour=กำลังติดตั้ง Apple Bonjour (ทำให้ iPhone มองเห็นเครื่องนี้)...
en.BonjourFailed=Apple Bonjour could not be installed automatically (winget is missing or there is no internet).%n%nPlease install "Bonjour Print Services for Windows" from Apple (or iTunes), then start {#AppName}. Without Bonjour the iPhone cannot see this PC.
th.BonjourFailed=ติดตั้ง Apple Bonjour อัตโนมัติไม่สำเร็จ (ไม่มี winget หรือไม่มีอินเทอร์เน็ต)%n%nกรุณาติดตั้ง "Bonjour Print Services for Windows" จาก Apple (หรือ iTunes) แล้วค่อยเปิด {#AppName} — ถ้าไม่มี Bonjour iPhone จะมองไม่เห็นเครื่องนี้

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"
Name: "firewallpublic"; Description: "{cm:TaskPublic}"; GroupDescription: "{cm:FirewallGroup}"; Flags: unchecked

[Files]
; the portable helper scripts/README are replaced by what this installer does
Source: "{#DistDir}\*"; DestDir: "{app}"; Excludes: "*.bat,*README*.txt"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\{#AppName}"; Filename: "{app}\{#AppExe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExe}"; Tasks: desktopicon

[Run]
; inbound rules for the receiver engine only (KagamiCast.exe itself has no network)
Filename: "{sys}\netsh.exe"; Parameters: "advfirewall firewall delete rule name=""{#FwRuleTcp}"""; Flags: runhidden; StatusMsg: "{cm:StatusFirewall}"
Filename: "{sys}\netsh.exe"; Parameters: "advfirewall firewall delete rule name=""{#FwRuleUdp}"""; Flags: runhidden; StatusMsg: "{cm:StatusFirewall}"
Filename: "{sys}\netsh.exe"; Parameters: "advfirewall firewall add rule name=""{#FwRuleTcp}"" dir=in action=allow program=""{app}\uxplay.exe"" protocol=TCP enable=yes profile={code:FirewallProfiles}"; Flags: runhidden; StatusMsg: "{cm:StatusFirewall}"
Filename: "{sys}\netsh.exe"; Parameters: "advfirewall firewall add rule name=""{#FwRuleUdp}"" dir=in action=allow program=""{app}\uxplay.exe"" protocol=UDP enable=yes profile={code:FirewallProfiles}"; Flags: runhidden; StatusMsg: "{cm:StatusFirewall}"
Filename: "{app}\{#AppExe}"; Description: "{cm:LaunchProgram,{#AppName}}"; Flags: nowait postinstall skipifsilent

[UninstallRun]
Filename: "{sys}\netsh.exe"; Parameters: "advfirewall firewall delete rule name=""{#FwRuleTcp}"""; Flags: runhidden; RunOnceId: "DeleteFirewallTcp"
Filename: "{sys}\netsh.exe"; Parameters: "advfirewall firewall delete rule name=""{#FwRuleUdp}"""; Flags: runhidden; RunOnceId: "DeleteFirewallUdp"

[Code]
function FirewallProfiles(Param: String): String;
begin
  if WizardIsTaskSelected('firewallpublic') then
    Result := 'private,public,domain'
  else
    Result := 'private,domain';
end;

function BonjourInstalled(): Boolean;
begin
  Result := RegKeyExists(HKLM, 'SYSTEM\CurrentControlSet\Services\Bonjour Service');
end;

{ Apple's Bonjour installer is not redistributed with this program; winget
  fetches it from Apple. Settings/logs in %LOCALAPPDATA%\KagamiCast are kept
  on uninstall (per-user data). }
procedure InstallBonjourIfMissing();
var
  ResultCode: Integer;
begin
  if BonjourInstalled() then
    Exit;
  WizardForm.StatusLabel.Caption := CustomMessage('StatusBonjour');
  WizardForm.Refresh();
  if (not Exec(ExpandConstant('{cmd}'),
               '/c winget install --id Apple.Bonjour -e --silent --accept-source-agreements --accept-package-agreements --disable-interactivity',
               '', SW_HIDE, ewWaitUntilTerminated, ResultCode)) or (not BonjourInstalled()) then
    SuppressibleMsgBox(CustomMessage('BonjourFailed'), mbInformation, MB_OK, IDOK);
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssPostInstall then
    InstallBonjourIfMissing();
end;
