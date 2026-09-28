#define MyAppName "Sessio"
#define MyAppPublisher "Sessio"
#define MyAppExeName "Sessio.exe"

#ifndef MyAppVersion
  #define MyAppVersion "0.1.1"
#endif

#ifndef MySourceDir
  #define MySourceDir "."
#endif

#ifndef MyOutputDir
  #define MyOutputDir "."
#endif

#ifndef MyIconFile
  #define MyIconFile "resources\icons\Sessio.ico"
#endif

[Setup]
AppId={{A9A95C24-4C51-4E52-89F6-1FC95B3900E8}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
OutputDir={#MyOutputDir}
OutputBaseFilename=Sessio-windows-setup
Compression=lzma
SolidCompression=yes
WizardStyle=modern
SetupIconFile={#MyIconFile}
UninstallDisplayIcon={app}\{#MyAppExeName}

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"

[Files]
Source: "{#MySourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Registry]
Root: HKCU; Subkey: "Software\Classes\sessio"; ValueType: string; ValueName: ""; ValueData: "URL:Sessio Protocol"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\sessio"; ValueType: string; ValueName: "URL Protocol"; ValueData: ""
Root: HKCU; Subkey: "Software\Classes\sessio\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#MyAppExeName}"" ""%1"""

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#StringChange(MyAppName, '&', '&&')}}"; Flags: nowait postinstall skipifsilent
