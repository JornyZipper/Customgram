; Customgram V13.0.1 - Windows installer (per-user)

; IMPORTANT: Never package dist\data or any tdata directory.
; They may contain private Telegram account sessions.

[Setup]
; Keep the same AppId for upgrades and uninstallation.
AppId={{F2300972-15EB-4B3E-8516-2711E0B96982}
AppName=Customgram
AppVersion=13.0.1
AppVerName=Customgram V13.0.1
AppPublisher=Customgram
DefaultDirName={localappdata}\Programs\Customgram
DefaultGroupName=Customgram
PrivilegesRequired=lowest
WizardStyle=modern
SetupIconFile=Telegram\Resources\art\Customgram.ico
OutputDir=dist\installer
OutputBaseFilename=Customgram_Setup_V13.0.1
Compression=lzma2
SolidCompression=yes
UninstallDisplayIcon={app}\Customgram.exe
CloseApplications=yes

[Dirs]
; Keep user session data separate from the installed program.
Name: "{localappdata}\Customgram\data"; Flags: uninsneveruninstall

[Files]
; Only program files. Do not include dist\data, tdata, or logs.
Source: "dist\Customgram.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "dist\modules\*"; DestDir: "{app}\modules"; Flags: ignoreversion recursesubdirs createallsubdirs

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Additional shortcuts:"

[Icons]
Name: "{group}\Customgram"; Filename: "{app}\Customgram.exe"; Parameters: "-many -workdir ""{localappdata}\Customgram\data"" -noupdate"; WorkingDir: "{app}"; IconFilename: "{app}\Customgram.exe"
Name: "{userdesktop}\Customgram"; Filename: "{app}\Customgram.exe"; Parameters: "-many -workdir ""{localappdata}\Customgram\data"" -noupdate"; WorkingDir: "{app}"; IconFilename: "{app}\Customgram.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\Customgram.exe"; Description: "Launch Customgram"; Parameters: "-many -workdir ""{localappdata}\Customgram\data"" -noupdate"; WorkingDir: "{app}"; Flags: nowait postinstall skipifsilent
