; Built by build-installer.ps1 from an already deployed, validated runtime.
#ifndef AppVersion
  #error AppVersion must be supplied by build-installer.ps1
#endif
#ifndef PayloadDir
  #error PayloadDir must be supplied by build-installer.ps1
#endif

[Setup]
#ifdef TranslationTrial
AppId={{82C4C15B-955B-4D88-9A55-1772D08997A8}
AppName=picut 图片翻译试用版
AppVerName=picut {#AppVersion} 图片翻译试用版
DefaultDirName={localappdata}\Programs\picut-translation-trial
DefaultGroupName=picut 图片翻译试用版
UninstallDisplayName=picut 图片翻译试用版
OutputBaseFilename=picut-{#AppVersion}-translation-trial-setup-x64
#else
AppId={{226B415D-A271-4655-8B0B-CD525BC942EB}
AppName=picut
AppVerName=picut {#AppVersion}
DefaultDirName={localappdata}\Programs\picut
DefaultGroupName=picut
UninstallDisplayName=picut
OutputBaseFilename=picut-{#AppVersion}-setup-x64
#endif
AppVersion={#AppVersion}
AppPublisher=picut
VersionInfoVersion={#AppVersion}.0
VersionInfoProductName=picut
VersionInfoProductVersion={#AppVersion}
VersionInfoDescription=picut 安装程序
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
OutputDir={#InstallerOutputDir}
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
#ifdef TranslationTrial
Name: "{group}\picut 图片翻译试用版"; Filename: "{app}\picut.exe"; WorkingDir: "{app}"; AppUserModelID: "picut.TranslationTrial"
Name: "{group}\{cm:ReadmeShortcut}"; Filename: "{app}\使用说明.txt"
Name: "{autodesktop}\picut 图片翻译试用版"; Filename: "{app}\picut.exe"; WorkingDir: "{app}"; AppUserModelID: "picut.TranslationTrial"; Tasks: desktopicon
#else
Name: "{group}\picut"; Filename: "{app}\picut.exe"; WorkingDir: "{app}"; AppUserModelID: "picut.Desktop"
Name: "{group}\{cm:ReadmeShortcut}"; Filename: "{app}\使用说明.txt"
Name: "{autodesktop}\picut"; Filename: "{app}\picut.exe"; WorkingDir: "{app}"; AppUserModelID: "picut.Desktop"; Tasks: desktopicon
#endif

[Run]
Filename: "{app}\picut.exe"; WorkingDir: "{app}"; Description: "{cm:LaunchPicut}"; Flags: nowait postinstall skipifsilent

; No UninstallDelete entries: user settings, history and saved captures survive uninstall.
