# Zendo — o 3D do ecossistema Zen (spike). Compila junto do cadapp
# (-DCADCORE_BUILD_APP=ON): mesmo kernel (cadcore), executável separado.
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets OpenGLWidgets)
qt_standard_project_setup()

qt_add_executable(zendo
    main.cpp
    ZendoWindow.cpp
    ZendoWindow.hpp
    Viewport3D.cpp
    Viewport3D.hpp
    StartPage3D.cpp
    StartPage3D.hpp
    Bridge.cpp
    Bridge.hpp
    PlantImport.cpp         # R3: conector planta→3D (único lugar que vê o 2D)
    PlantImport.hpp
    MaterialLib.cpp         # R9: biblioteca de materiais procedural
    MaterialLib.hpp
    FurnitureLib.cpp        # R14: o mobiliário paramétrico
    FurnitureLib.hpp
    ObjImport.cpp           # R15: importar OBJ como componente
    ObjImport.hpp
    BlenderInstaller.cpp    # R47: o motor de render que se instala sozinho
    BlenderInstaller.hpp
    ../app/ProjectIo.cpp    # leitura/escrita do .zencad compartilhada
    zendo.rc
)

set_target_properties(zendo PROPERTIES WIN32_EXECUTABLE TRUE)
# Ícone (cubo no ensō) no assets do deploy, para o QIcon da janela.
add_custom_command(TARGET zendo POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:zendo>/assets
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_CURRENT_SOURCE_DIR}/zendo.ico
            $<TARGET_FILE_DIR:zendo>/assets/zendo.ico
    # R10: texturas fotográficas CC0 (ambientCG) embutidas no produto
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/materiais_foto
            $<TARGET_FILE_DIR:zendo>/assets/materiais
    # R36: a cena do Fotógrafo (roda dentro do Blender headless)
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_CURRENT_SOURCE_DIR}/render_cena.py
            $<TARGET_FILE_DIR:zendo>/assets/render_cena.py
    # R46: céu real do Fotógrafo (HDRI CC0 do Poly Haven — qwantani_puresky)
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_CURRENT_SOURCE_DIR}/ceu_dia.hdr
            $<TARGET_FILE_DIR:zendo>/assets/ceu_dia.hdr)
# R3: o CORAÇÃO do Zendo depende só da ZENCORE (malha). O cadcore 2D entra
# como BIBLIOTECA DE FORMATO do conector (PlantImport/Bridge/ProjectIo) —
# os irmãos conversam por arquivos .zencad, não por acoplamento.
target_link_libraries(zendo PRIVATE
    zencore
    cadcore
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::OpenGLWidgets
)
if(WIN32)
  # R33: titlebar nativa escura (DwmSetWindowAttribute em ZendoChrome.hpp)
  target_link_libraries(zendo PRIVATE dwmapi)
endif()
if(MSVC)
  # /utf-8: fontes E literais em UTF-8 (acentos pt-BR nas strings da UI).
  target_compile_options(zendo PRIVATE /utf-8)
endif()
