# GUI Qt6 — compilada apenas com -DCADCORE_BUILD_APP=ON e Qt6 disponivel.
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets OpenGLWidgets)
qt_standard_project_setup()

qt_add_executable(cadapp
    main.cpp
    MainWindow.cpp
    MainWindow.hpp
    ViewportWidget.cpp
    ViewportWidget.hpp
    Camera.hpp
    StatusBar.cpp
    StatusBar.hpp
    GridRenderer.hpp
    ToolIcons.hpp
    CursorHud.hpp
    LayersPanel.cpp
    LayersPanel.hpp
    ProjectIo.cpp
    ProjectIo.hpp
    StartPage.cpp
    StartPage.hpp
    zencad.rc
)

# Ícones embutidos no binário (usados pelo QSS do tema, ex.: seta dos combos).
qt_add_resources(cadapp "theme_icons" PREFIX "/icons" BASE icons FILES
    icons/arrow_down.png
    icons/arrow_down_hi.png
)

# App de JANELA (sem console preso ao processo) + ícone no assets do deploy.
set_target_properties(cadapp PROPERTIES WIN32_EXECUTABLE TRUE)
add_custom_command(TARGET cadapp POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:cadapp>/assets
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_CURRENT_SOURCE_DIR}/zencad.ico
            $<TARGET_FILE_DIR:cadapp>/assets/zencad.ico
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_SOURCE_DIR}/branding/logo.png
            $<TARGET_FILE_DIR:cadapp>/assets/logo.png)
target_link_libraries(cadapp PRIVATE
    cadcore
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::OpenGLWidgets
)
if(MSVC)
  # /utf-8: fontes E literais em UTF-8 (acentos pt-BR nas strings da UI).
  target_compile_options(cadapp PRIVATE /utf-8)
endif()
