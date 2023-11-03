file(GLOB SOURCES *.cpp *.h)

add_executable(hello_d3d12 WIN32 ${SOURCES})

target_link_libraries(hello_d3d12 d3d12.lib dxgi.lib d3dcompiler.lib)
target_include_directories(hello_d3d12 PRIVATE ${D3DX12_INCLUDE_DIRS})
