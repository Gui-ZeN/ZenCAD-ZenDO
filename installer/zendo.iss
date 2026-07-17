; Instalador do Zendo (3D) — um produto do ecossistema Zen.
; Compilar: ISCC.exe zendo.iss  ->  ..\dist\Zendo-Setup-{versao}.exe
; O que é comum aos irmãos (versão, migração do Zen monolítico, idioma) mora em
; common.isi; aqui fica só o que é Zendo.

#define AppName    "Zendo"
#define AppExe     "zendo.exe"
#define DeployDir  "..\build-app-rel\src\zendo"

; ANTES do [Setup]: o preprocessador do Inno é sequencial, e o
; OutputBaseFilename abaixo usa o {#AppVersion} que nasce lá dentro.
#include "common.isi"

[Setup]
; GUID próprio — ver a nota do zencad.iss. Mesmo AppId nos dois = um desinstala
; o outro sem avisar.
AppId={{0321539C-B0BB-43D1-9131-35D37913A2E1}}
AppName={#AppName}
DefaultDirName={autopf}\Zen\{#AppName}
DefaultGroupName={#AppName}
UninstallDisplayIcon={app}\{#AppExe}
OutputBaseFilename={#AppName}-Setup-{#AppVersion}
; O ícone é o DELE. Copiar a linha do irmão daria um instalador do Zendo com a
; cara do ZenCAD — o tipo de detalhe que mata a identidade separada que esta
; leva inteira existe pra construir.
SetupIconFile=..\src\zendo\zendo.ico

[Files]
Source: "{#DeployDir}\{#AppExe}"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#DeployDir}\*.dll"; DestDir: "{app}"; Flags: ignoreversion
; ~36 MB: as ~150 texturas CC0 do ambientCG, o HDRI e o render_cena.py. É a
; maior parte deste instalador — e é tudo do Zendo. (Medido: o ZenCAD inteiro
; são 1,48 MB; quem baixava o pacote único levava estes 36 MB sem usar.)
Source: "{#DeployDir}\assets\*"; DestDir: "{app}\assets"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#DeployDir}\platforms\*"; DestDir: "{app}\platforms"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#DeployDir}\styles\*"; DestDir: "{app}\styles"; Flags: ignoreversion recursesubdirs createallsubdirs skipifsourcedoesntexist
Source: "{#DeployDir}\imageformats\*"; DestDir: "{app}\imageformats"; Flags: ignoreversion recursesubdirs createallsubdirs skipifsourcedoesntexist
Source: "{#DeployDir}\iconengines\*"; DestDir: "{app}\iconengines"; Flags: ignoreversion recursesubdirs createallsubdirs skipifsourcedoesntexist
Source: "{#DeployDir}\generic\*"; DestDir: "{app}\generic"; Flags: ignoreversion recursesubdirs createallsubdirs skipifsourcedoesntexist
Source: "{#DeployDir}\networkinformation\*"; DestDir: "{app}\networkinformation"; Flags: ignoreversion recursesubdirs createallsubdirs skipifsourcedoesntexist
Source: "{#DeployDir}\tls\*"; DestDir: "{app}\tls"; Flags: ignoreversion recursesubdirs createallsubdirs skipifsourcedoesntexist
; R52: as translations do Zendo sempre foram deployadas por conta própria, pra
; não pegarem carona no windeployqt do irmão (a carona fazia a sonda --qa-i18n
; passar aqui e o app cair pro inglês na máquina do cliente). Num instalador
; separado a carona é impossível por construção — mas o hazard do
; `skipifsourcedoesntexist` silenciar um deploy quebrado permanece.
Source: "{#DeployDir}\translations\*"; DestDir: "{app}\translations"; Flags: ignoreversion recursesubdirs createallsubdirs skipifsourcedoesntexist

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\{#AppExe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExe}"; Tasks: desktopicon

[Registry]
; O Zendo é o DONO de .zendo.
Root: HKA; Subkey: "Software\Classes\.zendo"; ValueType: string; ValueData: "Zendo.Study"; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Classes\Zendo.Study"; ValueType: string; ValueData: "Estudo 3D Zendo"; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\Zendo.Study\DefaultIcon"; ValueType: string; ValueData: "{app}\{#AppExe},0"
Root: HKA; Subkey: "Software\Classes\Zendo.Study\shell\open\command"; ValueType: string; ValueData: """{app}\{#AppExe}"" ""%1"""

; --- .zencad: CANDIDATO, nunca dono -----------------------------------------
; O Zendo lê .zencad (importa a planta) e grava .zencad (a ponte devolve
; elevações e cortes). Mas o dono da associação é o ZenCAD, e isso é decisão de
; produto, não limitação: duplo clique numa planta 2D abrindo direto num
; modelador 3D seria surpresa ruim. É exatamente o que o Revit faz com DWG —
; aparece em "Abrir com", nunca sequestra o default.
;
; E é o que evita a armadilha do cross-delete: os dois instaladores só apagam
; chaves DELES ao desinstalar. Aqui o `uninsdeletevalue` cai sobre o valor do
; Zendo dentro de OpenWithProgids — não sobre o default de .zencad. Conjuntos
; disjuntos não se assassinam.
Root: HKA; Subkey: "Software\Classes\Zendo.Plant2D"; ValueType: string; ValueData: "Planta 2D (abrir no Zendo)"; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\Zendo.Plant2D\DefaultIcon"; ValueType: string; ValueData: "{app}\{#AppExe},0"
Root: HKA; Subkey: "Software\Classes\Zendo.Plant2D\shell\open\command"; ValueType: string; ValueData: """{app}\{#AppExe}"" ""%1"""
Root: HKA; Subkey: "Software\Classes\.zencad\OpenWithProgids"; ValueType: string; ValueName: "Zendo.Plant2D"; ValueData: ""; Flags: uninsdeletevalue

[Run]
; Sem `unchecked`: no instalador dele, o Zendo é o protagonista.
Filename: "{app}\{#AppExe}"; Description: "Abrir o {#AppName} agora"; Flags: nowait postinstall skipifsilent
