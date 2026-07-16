# Executavel de teste headless do kernel.
add_executable(cad_smoke smoke_main.cpp)
target_link_libraries(cad_smoke PRIVATE cadcore)
