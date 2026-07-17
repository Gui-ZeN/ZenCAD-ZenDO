# zenio — a camada de FORMATO do ecossistema Zen.
#
# POR QUE ISTO EXISTE. O `.zencad` é o barramento entre os irmãos: o ZenCAD
# grava, o Zendo lê (PlantImport) e devolve cortes por ele (Bridge). Antes desta
# leva não havia dono: o `zendo/CMakeLists.txt` compilava `../app/ProjectIo.cpp`
# como fonte do PRÓPRIO alvo — o 3D recompilava um pedaço do 2D. Funcionava e
# mentia: fazia o Zendo depender do EXECUTÁVEL irmão, não de uma biblioteca.
# Com produtos separados (instaladores próprios) e um 3º app (BIM) no horizonte,
# um dono canônico do formato deixa de ser gosto e vira obrigação.
#
# A CAMADA. cadcore = MODELO (sem Qt, testável headless — não sacrificar isso);
# zenio = FORMATO (persistência, e por isso pode usar Qt). É por essa fronteira
# que o PlotStyle mudou de casa nesta leva: ele é Qt-free, logo é modelo, e foi
# pro core — aqui fica só quem serializa.
find_package(Qt6 REQUIRED COMPONENTS Core Gui)

# add_library puro, NÃO qt_add_library: não há um único Q_OBJECT aqui
# (conferido), então AUTOMOC/finalizers seriam maquinário para nada.
add_library(zenio STATIC
    ProjectIo.cpp
    ProjectIo.hpp
)

# PUBLIC, não PRIVATE — e a distinção não é cosmética: ProjectIo.hpp EXPÕE
# QString/QImage/QJson e DrawingManager/Layout/Style nas próprias assinaturas.
# PRIVATE compilaria hoje por acidente (os dois consumidores já linkam Qt por
# conta própria) e só explodiria no terceiro — o BIM, daqui a meses.
target_link_libraries(zenio PUBLIC cadcore Qt6::Core Qt6::Gui)
target_include_directories(zenio PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_compile_features(zenio PUBLIC cxx_std_20)

# Teste do formato. Mora aqui (e não em src/tools, com o cad_smoke) porque
# precisa de Qt — e src/tools compila no modo default SEM Qt.
add_executable(zenio_smoke zenio_smoke.cpp)
target_link_libraries(zenio_smoke PRIVATE zenio)
add_test(NAME zenio_smoke COMMAND zenio_smoke)
if(MSVC)
  # O mesmo /utf-8 do alvo abaixo: o PRIVATE dele não alcança este executável,
  # e este arquivo tem comentários acentuados em UTF-8 sem BOM. Numa máquina
  # cp1252 passa; num Windows CJK, um byte-lead DBCS no fim de comentário come
  # o \n seguinte. "Obrigatório" vale pro teste também.
  target_compile_options(zenio_smoke PRIVATE /utf-8)
else()
  target_compile_options(zenio_smoke PRIVATE -Wall -Wextra)
endif()

if(MSVC)
  # /utf-8 obrigatório: as mensagens de erro do load têm acento ("Não foi
  # possível abrir…"). Sem isto o MSVC assume a codepage do sistema e corrompe
  # as strings CALADO — o defeito não aparece no build, só na tela do usuário.
  #
  # Só /utf-8, igual aos alvos de onde este código veio (cadapp e zendo). O
  # cadcore usa /W4 /permissive-, e copiar isso pra cá acende **20** avisos
  # C4456 (shadowing) nas linhas 144–267 — a cadeia inteira de
  # `else if (auto* p = dynamic_cast<...>)` do entityToJson, cada `p` cobrindo o
  # anterior. É código que esta leva não tocou, e 20 shadowings são refactor
  # de verdade, não carona de uma extração. Fica nomeado como dívida.
  target_compile_options(zenio PRIVATE /utf-8)
else()
  target_compile_options(zenio PRIVATE -Wall -Wextra)
endif()
