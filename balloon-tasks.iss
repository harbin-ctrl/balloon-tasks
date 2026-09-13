; The Windows installer, built by `make inno` with Inno Setup 6.
; One installer for ARM64 and x64 Windows; it installs the build that matches.
; Per user, so no administrator rights are needed:
;
;   %LOCALAPPDATA%\Programs\balloon-tasks\   the program and its runtime DLLs
;   Start menu\Programs\Balloon Tasks!.lnk
;   Startup\Balloon Tasks!.lnk               optional; runs --autostart, which
;                                            stays closed after a deliberate quit
;
; Preferences (%APPDATA%\balloon-tasks.preferences) survive an uninstall.
;
; /DForceArch=arm64 or x64 installs that build whatever the machine; for
; testing only.

#ifndef AppVersion
  #define AppVersion "0.1"
#endif
#define AppName "Balloon Tasks!"
#define AppExe "balloon-tasks.exe"

[Setup]
AppId=harbin-ctrl.balloon-tasks
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher=harbin-ctrl
AppPublisherURL=https://github.com/harbin-ctrl/balloon-tasks
DefaultDirName={autopf}\balloon-tasks
DisableDirPage=yes
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible or arm64
ArchitecturesInstallIn64BitMode=x64compatible or arm64
OutputDir=installer
OutputBaseFilename=balloon-tasks-{#AppVersion}-setup
SetupIconFile=balloon-tasks.ico
UninstallDisplayIcon={app}\{#AppExe}
UninstallDisplayName={#AppName}
WizardStyle=modern
Compression=lzma2
SolidCompression=yes
; An upgrade replaces the running program.
CloseApplications=force
RestartApplications=no

[Tasks]
Name: autostart; Description: "Start {#AppName} when I sign in"

[Files]
Source: "installer\stage\arm64\*"; DestDir: "{app}"; Check: InstallArm64; Flags: ignoreversion
Source: "installer\stage\x64\*"; DestDir: "{app}"; Check: not InstallArm64; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\{#AppName}"; Filename: "{app}\{#AppExe}"
Name: "{userstartup}\{#AppName}"; Filename: "{app}\{#AppExe}"; Parameters: "--autostart"; Tasks: autostart

[Run]
Filename: "{app}\{#AppExe}"; Description: "Launch {#AppName}"; Flags: nowait postinstall

[UninstallRun]
Filename: "{sys}\taskkill.exe"; Parameters: "/F /IM {#AppExe}"; Flags: runhidden; RunOnceId: "StopApp"

[Code]
function InstallArm64: Boolean;
begin
#ifdef ForceArch
  Result := '{#ForceArch}' = 'arm64';
#else
  Result := IsArm64;
#endif
end;
