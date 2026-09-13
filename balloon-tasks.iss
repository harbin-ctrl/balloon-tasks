; The Windows installer, built by `make installer` with Inno Setup 6.
; Per user, so no administrator rights are needed:
;
;   %LOCALAPPDATA%\Programs\balloon-tasks\   the program and its runtime DLLs
;   Start menu\Programs\Balloon Tasks!.lnk
;   Startup\Balloon Tasks!.lnk               optional; runs --autostart, which
;                                            stays closed after a deliberate quit
;
; Preferences (%APPDATA%\balloon-tasks.preferences) survive an uninstall.

#ifndef AppVersion
  #define AppVersion "0.1"
#endif
; arm64 or x64
#ifndef Arch
  #define Arch "arm64"
#endif
#define AppName "Balloon Tasks!"
#define AppExe "balloon-tasks.exe"
#if Arch == "x64"
  #define ArchAllowed "x64compatible"
#else
  #define ArchAllowed Arch
#endif

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
ArchitecturesAllowed={#ArchAllowed}
ArchitecturesInstallIn64BitMode={#ArchAllowed}
OutputDir=installer
OutputBaseFilename=balloon-tasks-{#AppVersion}-{#Arch}-setup
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
Source: "installer\stage\*"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\{#AppName}"; Filename: "{app}\{#AppExe}"
Name: "{userstartup}\{#AppName}"; Filename: "{app}\{#AppExe}"; Parameters: "--autostart"; Tasks: autostart

[Run]
Filename: "{app}\{#AppExe}"; Description: "Launch {#AppName}"; Flags: nowait postinstall

[UninstallRun]
Filename: "{sys}\taskkill.exe"; Parameters: "/F /IM {#AppExe}"; Flags: runhidden; RunOnceId: "StopApp"
