// src/zendo/MaterialLib.hpp
// R9: A BIBLIOTECA DE MATERIAIS — texturas de arquitetura GERADAS pelo
// próprio Zendo (procedurais, determinísticas, seamless). Nada de download,
// nada de licença: tijolo, madeiras, concretos, pedra, telha, piso, grama…
// nascem em %APPDATA%/Zendo/materiais na primeira execução (512×512, e a
// convenção é 1 tile = 1 m no mundo — a escala 1.0 do balde já veste certo).
#pragma once
#include <QPair>
#include <QString>
#include <QVector>

namespace matlib {

// Garante a biblioteca no disco (gera só o que faltar) e devolve a lista
// (nome amigável, caminho absoluto do .png) na ordem curada.
QVector<QPair<QString, QString>> ensureLibrary();

} // namespace matlib
