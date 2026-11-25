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
; OBS plugin, present in dist\ only when build-all.sh found Visual Studio
#define ObsPluginDir DistDir + "\obs-plugin\kagami-cast-obs"
#define HaveObsPlugin FileExists(ObsPluginDir + "\bin\64bit\kagami-cast-obs.dll")

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
; admin: Program Files, Windows Firewall rules and the OBS plugin in ProgramData
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
en.ObsGroup=OBS Studio:
th.ObsGroup=OBS Studio:
en.TaskObs=Install the plugin that shows the iPhone picture as an OBS source (OBS 31 or newer)
th.TaskObs=ติดตั้งปลั๊กอินที่แสดงภาพ iPhone เป็น Source ใน OBS (ต้องใช้ OBS 31 ขึ้นไป)

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"
Name: "firewallpublic"; Description: "{cm:TaskPublic}"; GroupDescription: "{cm:FirewallGroup}"; Flags: unchecked
#if HaveObsPlugin
; pre-ticked only when OBS is installed (see CurPageChanged)
Name: "obsplugin"; Description: "{cm:TaskObs}"; GroupDescription: "{cm:ObsGroup}"
#endif

[Files]
; the portable helper scripts/README are replaced by what this installer does;
; the OBS plugin goes to OBS's plugin folder instead of {app}
Source: "{#DistDir}\*"; DestDir: "{app}"; Excludes: "*.bat,*README*.txt,\obs-plugin"; Flags: ignoreversion recursesubdirs createallsubdirs
#if HaveObsPlugin
; %ProgramData%\obs-studio\plugins: where OBS 31+ loads per-machine plugins from.
; Removed again on uninstall; a running OBS is offered to be closed (CloseApplications).
Source: "{#ObsPluginDir}\*"; DestDir: "{commonappdata}\obs-studio\plugins\kagami-cast-obs"; Tasks: obsplugin; Flags: ignoreversion recursesubdirs createallsubdirs
#endif

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

{ No Apple Bonjour: uxplay's dnssd.dll (dnssd-shim/) uses the DNS-SD service
  built into Windows. A Bonjour installed by version 1.2 or older is left alone.
  Settings/logs in %LOCALAPPDATA%\KagamiCast are kept on uninstall (per-user data). }

#if HaveObsPlugin
var
  ObsTaskDefaultSet: Boolean;

{ OBS's own installer writes this key; a portable or Steam OBS is not detected,
  so the task stays visible for the user to tick. }
function ObsInstalled(): Boolean;
begin
  Result := RegKeyExists(HKLM, 'SOFTWARE\OBS Studio') or RegKeyExists(HKCU, 'SOFTWARE\OBS Studio') or
            FileExists(ExpandConstant('{commonpf64}\obs-studio\bin\64bit\obs64.exe'));
end;

procedure CurPageChanged(CurPageID: Integer);
begin
  { once, so going Back to the page keeps the user's choice }
  if (CurPageID = wpSelectTasks) and (not ObsTaskDefaultSet) then
  begin
    ObsTaskDefaultSet := True;
    if not ObsInstalled() then
      WizardSelectTasks('!obsplugin');
  end;
end;
#endif
