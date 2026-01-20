cmake_minimum_required(VERSION 3.20)
project(alkv_vm LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

Set(ASMJIT_STATIC TRUE)

add_subdirectory(libs/asmjit asmjit)

add_executable(alkv_vm
    src/main.cpp
    src/vm.cpp
    src/loader.cpp
    src/compiler.cpp
)

target_include_directories(alkv_vm PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(alkv_vm asmjit::asmjit)

if (MSVC)
    target_compile_options(alkv_vm PRIVATE /W4)
else()
    target_compile_options(alkv_vm PRIVATE -Wall -Wextra -Wpedantic)
endif()