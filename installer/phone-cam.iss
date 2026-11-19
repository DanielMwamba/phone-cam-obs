; Windows installer for the Phone Cam OBS plugin (Inno Setup 6).
; Built by build-aux/package.ps1, which passes the version and the staged plugin folder:
;   ISCC /DAppVersion=0.1.0 /DStageDir=<release\stage\phone-cam> installer\phone-cam.iss

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef StageDir
  #define StageDir "..\release\stage\phone-cam"
#endif

[Setup]
; Never change the AppId: Windows uses it to find the previous version on upgrade.
AppId={{82F2E31B-89F2-478D-BF9B-64E7D0F7CB48}
AppName=Phone Cam for OBS
AppVersion={#AppVersion}
AppVerName=Phone Cam for OBS {#AppVersion}
AppPublisher=Daniel Mwamba
AppPublisherURL=https://github.com/DanielMwamba/phone-cam-obs
AppSupportURL=https://github.com/DanielMwamba/phone-cam-obs/issues
; OBS loads per-machine plugins from C:\ProgramData\obs-studio\plugins.
DefaultDirName={commonappdata}\obs-studio\plugins\phone-cam
DisableDirPage=yes
DisableProgramGroupPage=yes
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
LicenseFile=..\LICENSE
OutputBaseFilename=phone-cam-{#AppVersion}-windows-x64-installer
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
UninstallDisplayName=Phone Cam for OBS
; OBS locks the DLL while it runs: offer to close it instead of failing on the copy.
CloseApplications=yes
RestartApplications=no

[Files]
Source: "{#StageDir}\*"; DestDir: "{app}"; Excludes: "*.pdb"; Flags: recursesubdirs ignoreversion
