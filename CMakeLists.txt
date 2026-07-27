cmake_minimum_required(VERSION 3.22)

project(PalworldSrvConnectMods LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "The UE4SS mod must be built on Windows. Core-only tests live in tests/.")
endif()

if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/RE-UE4SS/CMakeLists.txt")
    message(FATAL_ERROR "RE-UE4SS is missing. Run setup.ps1 first.")
endif()

add_subdirectory(RE-UE4SS)
add_subdirectory(PalworldSrvConnect)

