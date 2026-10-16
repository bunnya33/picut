; Built by build-installer.ps1 from an already deployed, validated runtime.
#ifndef AppVersion
  #error AppVersion must be supplied by build-installer.ps1
#endif
#ifndef PayloadDir
  #error PayloadDir must be supplied by build-installer.ps1
#endif

[Setup]
AppId={{226B415D-A271-4655-8B0B-CD525BC942EB}
AppName=picut
AppVersion={#AppVersion}
AppPublisher=picut
AppVerName=picut {#AppVersion}
VersionInfoVersion={#AppVersion}.0
VersionInfoProductName=picut
VersionInfoProductVersion={#AppVersion}
VersionInfoDescription=picut 安装程序
DefaultDirName={localappdata}\Programs\picut
DefaultGroupName=picut
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.17763
DisableDirPage=no
DisableProgramGroupPage=yes
AllowNoIcons=yes
WizardStyle=modern
LanguageDetectionMethod=none
SetupIconFile={#SourcePath}\..\assets\picut.ico
UninstallDisplayIcon={app}\picut.exe
UninstallDisplayName=picut
OutputDir={#InstallerOutputDir}
OutputBaseFilename=picut-{#AppVersion}-setup-x64
Compression=lzma2/max
SolidCompression=yes
LZMAUseSeparateProcess=yes
CloseApplications=no
RestartApplications=no
ChangesAssociations=no
ChangesEnvironment=no
SetupLogging=yes

[Languages]
Name: "chinesesimplified"; MessagesFile: "{#ChineseMessages}"
Name: "english"; MessagesFile: "compiler:Default.isl"

[CustomMessages]
chinesesimplified.LaunchPicut=启动 picut
english.LaunchPicut=Launch picut
chinesesimplified.ReadmeShortcut=使用说明
english.ReadmeShortcut=User guide

[Messages]
chinesesimplified.WindowsVersionNotSupported=picut 需要 Windows 10 1809（内部版本 17763）或更新的 Windows 系统。
english.WindowsVersionNotSupported=picut requires Windows 10 version 1809 (build 17763) or later.

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "{#PayloadDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\picut"; Filename: "{app}\picut.exe"; WorkingDir: "{app}"; AppUserModelID: "picut.Desktop"
Name: "{group}\{cm:ReadmeShortcut}"; Filename: "{app}\使用说明.txt"
Name: "{autodesktop}\picut"; Filename: "{app}\picut.exe"; WorkingDir: "{app}"; AppUserModelID: "picut.Desktop"; Tasks: desktopicon

[Run]
Filename: "{app}\picut.exe"; WorkingDir: "{app}"; Description: "{cm:LaunchPicut}"; Flags: nowait postinstall skipifsilent

; No UninstallDelete entries: user settings, history and saved captures survive uninstall.
