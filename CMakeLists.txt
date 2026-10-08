cmake_minimum_required(VERSION 3.24)

project(brolink VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(MSVC)
    add_compile_options(
        $<$<COMPILE_LANGUAGE:CXX>:/MP>
        $<$<COMPILE_LANGUAGE:CXX>:/permissive->
        $<$<COMPILE_LANGUAGE:CXX>:/utf-8>
    )
    # A top-level build links the CRT statically, like the siblings; inside a
    # superbuild the consumer's choice wins.
    if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
        set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endif()
endif()

find_package(Threads REQUIRED)

# ---- library -------------------------------------------------------------------
# Platform APIs only: no dependencies.
set(BROLINK_SOURCES
    src/wire.cpp
    src/cmdline.cpp
    src/paths_common.cpp
    src/proxy.cpp
    src/lanes.cpp
)
if(WIN32)
    list(APPEND BROLINK_SOURCES
        src/win/loop_win.cpp
        src/win/paths_win.cpp
        src/win/random_win.cpp
        src/win/spawn_win.cpp
        src/win/stream_win.cpp
    )
else()
    list(APPEND BROLINK_SOURCES
        src/posix/loop_posix.cpp
        src/posix/paths_posix.cpp
        src/posix/random_posix.cpp
        src/posix/spawn_posix.cpp
        src/posix/stream_posix.cpp
    )
endif()

add_library(brolink STATIC ${BROLINK_SOURCES})
add_library(brolink::brolink ALIAS brolink)
target_include_directories(brolink
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(brolink PUBLIC Threads::Threads)
if(WIN32)
    target_link_libraries(brolink PRIVATE advapi32 bcrypt)
    target_compile_definitions(brolink PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE _CRT_SECURE_NO_WARNINGS)
endif()
if(MSVC)
    target_compile_options(brolink PRIVATE /W4)
else()
    target_compile_options(brolink PRIVATE -Wall -Wextra)
endif()

option(BROLINK_BUILD_TESTS "Build the brolink test suite" ON)
if(BROLINK_BUILD_TESTS AND PROJECT_IS_TOP_LEVEL)
    include(CTest)
    add_subdirectory(tests)
endif()
