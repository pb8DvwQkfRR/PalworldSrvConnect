cmake_minimum_required(VERSION 3.22)

project(PalworldSrvConnectCoreTests LANGUAGES CXX)

enable_testing()

add_executable(PalworldSrvConnectCoreTests
    EndpointTests.cpp
    ../PalworldSrvConnect/src/Endpoint.cpp
)
target_compile_features(PalworldSrvConnectCoreTests PRIVATE cxx_std_20)
target_include_directories(PalworldSrvConnectCoreTests PRIVATE ../PalworldSrvConnect/include)

add_test(NAME PalworldSrvConnectCoreTests COMMAND PalworldSrvConnectCoreTests)

