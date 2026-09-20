cmake_minimum_required(VERSION 3.16)
project(ElectroBench CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(OpenGL REQUIRED)
find_package(GLEW REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)

# Original OpenGL 2.1 / GLSL 1.2 benchmark (90 UZIs on a shadow-mapped floor)
add_executable(ElectroBench src/main.cxx)
target_include_directories(ElectroBench PRIVATE lib)
target_link_libraries(ElectroBench PRIVATE OpenGL::GL OpenGL::GLU GLEW::GLEW PkgConfig::SDL2)

# OpenGL 3.3 PS1.4-style sea benchmark (3DMark2001 SE "Nature" recreation)
add_executable(ElectroBenchPS14 src/ps14_bench.cxx)
target_link_libraries(ElectroBenchPS14 PRIVATE OpenGL::GL GLEW::GLEW PkgConfig::SDL2)
