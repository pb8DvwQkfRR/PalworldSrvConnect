cmake_minimum_required(VERSION 3.22)

set(TARGET PalworldSrvConnect)

add_library(${TARGET} SHARED
    src/dllmain.cpp
    src/Endpoint.cpp
    src/SrvResolver.cpp
)

target_compile_features(${TARGET} PRIVATE cxx_std_20)
target_compile_definitions(${TARGET} PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)
target_include_directories(${TARGET} PRIVATE include)

target_link_libraries(${TARGET} PUBLIC UE4SS)
target_link_libraries(${TARGET} PRIVATE dnsapi ws2_32)

set_target_properties(${TARGET} PROPERTIES
    OUTPUT_NAME "main"
)

set(MOD_DIST_DIR "${CMAKE_SOURCE_DIR}/dist/PalworldSrvConnect")
add_custom_command(TARGET ${TARGET} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${MOD_DIST_DIR}/dlls"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:${TARGET}>" "${MOD_DIST_DIR}/dlls/main.dll"
    COMMAND ${CMAKE_COMMAND} -E touch "${MOD_DIST_DIR}/enabled.txt"
    COMMENT "Packaging PalworldSrvConnect for UE4SS"
)

