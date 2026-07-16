; Instalador do ecossistema Zen (Inno Setup 6): ZenCAD (2D) + Zendo (3D).
; Compilar: ISCC.exe zencad.iss  ->  ..\dist\Zen-Setup-{versao}.exe
; Um diretÃ³rio sÃ³: os dois executÃ¡veis compartilham as DLLs do Qt e o assets\.
; Associa .zencad (ZenCAD) e .zendo (Zendo), atalhos e desinstalador Ãºnicos.
; RITUAL: bump do 3Âº dÃ­gito a cada leva do Zendo (revisÃ£o pÃ³s-R30 â€” 8 releases
; num dia todos com o mesmo nome tornavam builds indistinguÃ­veis).

#define AppName    "Zen"
#define AppVersion "2.0.49"
#define CadExe     "cadapp.exe"
#define ZendoExe   "zendo.exe"
#define CadDir     "..\build-app-rel\src\app"
#define ZendoDir   "..\build-app-rel\src\zendo"

[Setup]
AppId={{7E2A9C4B-5D1F-4A83-9B60-ZENCAD10}}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher=Guilherme Â· Grupo Christus
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
UninstallDisplayIcon={app}\{#CadExe}
OutputDir=..\dist
OutputBaseFilename=Zen-Setup-{#AppVersion}
SetupIconFile=..\src\app\zencad.ico
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
ArchitecturesInstallIn64BitMode=x64compatible
DisableProgramGroupPage=yes

[Languages]
Name: "brazilianportuguese"; MessagesFile: "compiler:Languages\BrazilianPortuguese.isl"

[Tasks]
Name: "desktopicon"; Description: "Criar atalhos na Ãrea de Trabalho (ZenCAD e Zendo)"; GroupDescription: "Atalhos:"

[Files]
; --- ZenCAD (2D) + deploy Qt ---
Source: "{#CadDir}\{#CadExe}"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#CadDir}\*.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#CadDir}\assets\*"; DestDir: "{app}\assets"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#CadDir}\platforms\*"; DestDir: "{app}\platforms"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#CadDir}\styles\*"; DestDir: "{app}\styles"; Flags: ignoreversion recursesubdirs createallsubdirs skipifsourcedoesntexist
Source: "{#CadDir}\imageformats\*"; DestDir: "{app}\imageformats"; Flags: ignoreversion recursesubdirs createallsubdirs skipifsourcedoesntexist
Source: "{#CadDir}\iconengines\*"; DestDir: "{app}\iconengines"; Flags: ignoreversion recursesubdirs createallsubdirs skipifsourcedoesntexist
Source: "{#CadDir}\generic\*"; DestDir: "{app}\generic"; Flags: ignoreversion recursesubdirs createallsubdirs skipifsourcedoesntexist
Source: "{#CadDir}\networkinformation\*"; DestDir: "{app}\networkinformation"; Flags: ignoreversion recursesubdirs createallsubdirs skipifsourcedoesntexist
Source: "{#CadDir}\tls\*"; DestDir: "{app}\tls"; Flags: ignoreversion recursesubdirs createallsubdirs skipifsourcedoesntexist
Source: "{#CadDir}\translations\*"; DestDir: "{app}\translations"; Flags: ignoreversion recursesubdirs createallsubdirs skipifsourcedoesntexist
; --- Zendo (3D): exe + assets + DLLs extras do deploy dele (merge no mesmo dir) ---
Source: "{#ZendoDir}\{#ZendoExe}"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#ZendoDir}\*.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#ZendoDir}\assets\*"; DestDir: "{app}\assets"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\ZenCAD"; Filename: "{app}\{#CadExe}"
Name: "{group}\Zendo"; Filename: "{app}\{#ZendoExe}"
Name: "{autodesktop}\ZenCAD"; Filename: "{app}\{#CadExe}"; Tasks: desktopicon
Name: "{autodesktop}\Zendo"; Filename: "{app}\{#ZendoExe}"; Tasks: desktopicon

[Registry]
; .zencad -> ZenCAD (projeto 2D)
Root: HKA; Subkey: "Software\Classes\.zencad"; ValueType: string; ValueData: "ZenCAD.Project"; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Classes\ZenCAD.Project"; ValueType: string; ValueData: "Projeto ZenCAD"; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\ZenCAD.Project\DefaultIcon"; ValueType: string; ValueData: "{app}\{#CadExe},0"
Root: HKA; Subkey: "Software\Classes\ZenCAD.Project\shell\open\command"; ValueType: string; ValueData: """{app}\{#CadExe}"" ""%1"""
; .zendo -> Zendo (estudo 3D)
Root: HKA; Subkey: "Software\Classes\.zendo"; ValueType: string; ValueData: "Zendo.Study"; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Classes\Zendo.Study"; ValueType: string; ValueData: "Estudo 3D Zendo"; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\Zendo.Study\DefaultIcon"; ValueType: string; ValueData: "{app}\{#ZendoExe},0"
Root: HKA; Subkey: "Software\Classes\Zendo.Study\shell\open\command"; ValueType: string; ValueData: """{app}\{#ZendoExe}"" ""%1"""

[Run]
Filename: "{app}\{#CadExe}"; Description: "Abrir o ZenCAD agora"; Flags: nowait postinstall skipifsilent
Filename: "{app}\{#ZendoExe}"; Description: "Abrir o Zendo agora"; Flags: nowait postinstall skipifsilent unchecked
