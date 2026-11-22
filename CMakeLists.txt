cmake_minimum_required(VERSION 3.22)
project(VSTBridge VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Two separate builds are needed because the architectures differ:
#   1) cmake -S . -B build32 -A Win32 -DBRIDGE_TARGET=host32   -> Bridge32Host.exe (x86)
#   2) cmake -S . -B build64 -A x64   -DBRIDGE_TARGET=plugin -DBRIDGE32_EXE=<path to the exe above>
set(BRIDGE_TARGET "plugin" CACHE STRING "host32 or plugin")
set_property(CACHE BRIDGE_TARGET PROPERTY STRINGS host32 plugin)

if(BRIDGE_TARGET STREQUAL "host32")
    add_subdirectory(host32)
else()
    add_subdirectory(plugin)
endif()
