cmake_minimum_required(VERSION 3.20)
project(CadCore LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Default to Release para os single-config generators (Ninja/Make).
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# A GUI Qt6 fica OFF por padrao: o kernel + testes compilam sem Qt/OpenGL.
option(CADCORE_BUILD_APP "Build the Qt6 GUI app (requires Qt6 in CMAKE_PREFIX_PATH)" OFF)

add_subdirectory(src/core)    # alvo: cadcore (STATIC) — testavel headless
add_subdirectory(src/tools)   # alvo: cad_smoke (executavel de teste)

if(CADCORE_BUILD_APP)
  add_subdirectory(src/app)   # alvo: cadapp (Qt6) — ZenCAD (2D)
  add_subdirectory(src/zendo) # alvo: zendo  (Qt6) — Zendo (3D do ecossistema Zen)
endif()

enable_testing()
add_test(NAME smoke COMMAND cad_smoke)
