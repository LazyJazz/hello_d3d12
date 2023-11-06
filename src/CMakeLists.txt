file(GLOB SOURCES *.cpp *.h)

add_executable(hello_d3d12 ${SOURCES})

target_link_libraries(hello_d3d12 PRIVATE d3d12 dxgi d3dcompiler dxguid)
find_package(glfw3 CONFIG REQUIRED)
target_include_directories(hello_d3d12 PRIVATE ${D3DX12_INCLUDE_DIRS})
target_link_libraries(hello_d3d12 PRIVATE glfw)
find_package(directxmath CONFIG REQUIRED)
target_link_libraries(hello_d3d12 PRIVATE Microsoft::DirectXMath)
find_package(glm CONFIG REQUIRED)
target_link_libraries(hello_d3d12 PRIVATE glm::glm)
