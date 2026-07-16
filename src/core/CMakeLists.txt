# R3: ZENCORE — o kernel de MALHA 3D, independente do mundo 2D (documento,
# entidades, camadas). É a biblioteca do Zendo; o cadcore 2D constrói sobre
# ela. Sem Qt, sem OpenGL, sem documento — só geometria.
add_library(zencore STATIC
    mesh/HalfEdgeMesh.cpp
)
target_include_directories(zencore PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_compile_features(zencore PUBLIC cxx_std_20)
if(MSVC)
  target_compile_options(zencore PRIVATE /W4 /permissive- /utf-8)
else()
  target_compile_options(zencore PRIVATE -Wall -Wextra)
endif()

# Kernel CAD 2D: SEM dependencia de Qt ou OpenGL. Compila e testa headless.
add_library(cadcore STATIC
    geometry/Entity.cpp
    geometry/Line.cpp
    geometry/Circle.cpp
    geometry/Arc.cpp
    geometry/Polyline.cpp
    geometry/MText.cpp
    geometry/Dimension.cpp
    geometry/Hatch.cpp
    geometry/Wipeout.cpp
    geometry/Region.cpp
    geometry/Table.cpp
    geometry/Ellipse.cpp
    geometry/LinePattern.cpp
    geometry/PointEntity.cpp
    geometry/Spline.cpp
    geometry/Wall.cpp
    geometry/BlockRef.cpp
    geometry/XLine.cpp
    geometry/RayLine.cpp
    geometry/MLine.cpp
    geometry/Leader.cpp
    geometry/MLeader.cpp
    text/StrokeFont.cpp
    spatial/Quadtree.cpp
    document/LayerTable.cpp
    document/DrawingManager.cpp
    command/CommandStack.cpp
    interaction/ToolController.cpp
    snap/SnapEngine.cpp
    edit/GeometryOps.cpp
    edit/ConstructOps.cpp
    edit/IntersectOps.cpp
    edit/ExtendOps.cpp
    edit/ChamferOps.cpp
    edit/GripOps.cpp
    edit/DivideOps.cpp
    edit/TangentOps.cpp
    edit/Tangent3Ops.cpp
    edit/SplineOps.cpp
    edit/ModifyOps.cpp
    edit/InquiryOps.cpp
    edit/ReviseCloud.cpp
    edit/TrimOps.cpp
    edit/FilletOps.cpp
    cli/CommandParser.cpp
    ../io/DxfWriter.cpp
    ../io/DxfReader.cpp
)

# Includes no formato "core/..." resolvem a partir de src/.
target_include_directories(cadcore PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_compile_features(cadcore PUBLIC cxx_std_20)
target_link_libraries(cadcore PUBLIC zencore)   # 2D constrói sobre a malha

if(MSVC)
  # /utf-8: fontes E literais em UTF-8 (permite acentos pt-BR nas strings).
  target_compile_options(cadcore PRIVATE /W4 /permissive- /utf-8)
else()
  target_compile_options(cadcore PRIVATE -Wall -Wextra)
endif()
