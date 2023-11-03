cmake_minimum_required(VERSION 3.22)

set(CMAKE_TOOLCHAIN_FILE ${CMAKE_CURRENT_SOURCE_DIR}/external/vcpkg/scripts/buildsystems/vcpkg.cmake)

project(hello_d3d12)

set(CMAKE_CXX_STANDARD 17)

find_path(D3DX12_INCLUDE_DIRS "d3dx12.h")

add_subdirectory(src)
