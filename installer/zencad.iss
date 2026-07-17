; Instalador do ZenCAD (2D) — um produto do ecossistema Zen.
; Compilar: ISCC.exe zencad.iss  ->  ..\dist\ZenCAD-Setup-{versao}.exe
; O que é comum aos irmãos (versão, migração do Zen monolítico, idioma) mora em
; common.isi; aqui fica só o que é ZenCAD.

#define AppName    "ZenCAD"
#define AppExe     "cadapp.exe"
#define DeployDir  "..\build-app-rel\src\app"

; ANTES do [Setup]: o preprocessador do Inno é sequencial, e o
; OutputBaseFilename abaixo usa o {#AppVersion} que nasce lá dentro.
#include "common.isi"

[Setup]
; GUID de verdade, gerado pra ESTE produto. O AppId é a identidade do
; aplicativo pro Windows: dois instaladores com o mesmo AppId fariam um
; DESINSTALAR o outro, calado, na máquina de quem já usa. O AppId do Zen
; monolítico (não-GUID) virou constante de migração no common.isi.
AppId={{74F1C317-C3D1-448E-81BB-53E4BEAC2947}}
AppName={#AppName}
; Autodesk literal: Program Files\Zen\ZenCAD — a família fica visível no disco
; sem que os produtos compartilhem pasta (o uninstaller do Inno só remove o que
; ele instalou, e só apaga {autopf}\Zen se ficar vazio).
DefaultDirName={autopf}\Zen\{#AppName}
DefaultGroupName={#AppName}
UninstallDisplayIcon={app}\{#AppExe}
OutputBaseFilename={#AppName}-Setup-{#AppVersion}
SetupIconFile=..\src\app\zencad.ico

[Files]
Source: "{#DeployDir}\{#AppExe}"; DestDir: "{app}"; Flags: ignoreversion
; Sem skip flag de propósito: se o deploy não existir, o ISCC falha ALTO. Um
; instalador que se monta sem o executável é pior que um build quebrado.
Source: "{#DeployDir}\*.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#DeployDir}\assets\*"; DestDir: "{app}\assets"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#DeployDir}\platforms\*"; DestDir: "{app}\platforms"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#DeployDir}\styles\*"; DestDir: "{app}\styles"; Flags: ignoreversion recursesubdirs createallsubdirs skipifsourcedoesntexist
Source: "{#DeployDir}\imageformats\*"; DestDir: "{app}\imageformats"; Flags: ignoreversion recursesubdirs createallsubdirs skipifsourcedoesntexist
Source: "{#DeployDir}\iconengines\*"; DestDir: "{app}\iconengines"; Flags: ignoreversion recursesubdirs createallsubdirs skipifsourcedoesntexist
Source: "{#DeployDir}\generic\*"; DestDir: "{app}\generic"; Flags: ignoreversion recursesubdirs createallsubdirs skipifsourcedoesntexist
Source: "{#DeployDir}\networkinformation\*"; DestDir: "{app}\networkinformation"; Flags: ignoreversion recursesubdirs createallsubdirs skipifsourcedoesntexist
Source: "{#DeployDir}\tls\*"; DestDir: "{app}\tls"; Flags: ignoreversion recursesubdirs createallsubdirs skipifsourcedoesntexist
Source: "{#DeployDir}\translations\*"; DestDir: "{app}\translations"; Flags: ignoreversion recursesubdirs createallsubdirs skipifsourcedoesntexist

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\{#AppExe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExe}"; Tasks: desktopicon

[Registry]
; O ZenCAD é o DONO de .zencad: duplo clique numa planta abre o CAD 2D.
; (O Zendo também lê .zencad, mas entra só como "Abrir com" — ver zendo.iss.)
Root: HKA; Subkey: "Software\Classes\.zencad"; ValueType: string; ValueData: "ZenCAD.Project"; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Classes\ZenCAD.Project"; ValueType: string; ValueData: "Projeto ZenCAD"; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\ZenCAD.Project\DefaultIcon"; ValueType: string; ValueData: "{app}\{#AppExe},0"
Root: HKA; Subkey: "Software\Classes\ZenCAD.Project\shell\open\command"; ValueType: string; ValueData: """{app}\{#AppExe}"" ""%1"""

[Run]
Filename: "{app}\{#AppExe}"; Description: "Abrir o {#AppName} agora"; Flags: nowait postinstall skipifsilent
