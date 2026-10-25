cmake_minimum_required(VERSION 3.20)
project(schetovod VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(SCHETOVOD_TESTS "Build unit tests" ON)
option(SCHETOVOD_WERROR "Treat warnings as errors" OFF)

add_library(schetovod_core
    src/amount.cpp
    src/date.cpp
    src/decimal.cpp
    src/ledger.cpp
    src/parser.cpp
    src/reports.cpp
    src/utf8.cpp
)
target_include_directories(schetovod_core PUBLIC src)

function(schetovod_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /utf-8 /permissive- /wd4996)
        if(SCHETOVOD_WERROR)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wno-sign-conversion)
        if(SCHETOVOD_WERROR)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()
endfunction()

schetovod_warnings(schetovod_core)
if(MSVC)
    target_compile_options(schetovod_core PUBLIC /utf-8)
endif()

add_executable(schetovod src/main.cpp)
target_link_libraries(schetovod PRIVATE schetovod_core)
if(WIN32)
    target_link_libraries(schetovod PRIVATE shell32)
endif()
schetovod_warnings(schetovod)

if(SCHETOVOD_TESTS)
    enable_testing()
    if(POLICY CMP0135)
        cmake_policy(SET CMP0135 NEW)
    endif()
    include(FetchContent)
    FetchContent_Declare(googletest
        URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.zip)
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)

    add_executable(schetovod_tests
        tests/test_decimal.cpp
        tests/test_amount.cpp
        tests/test_parser.cpp
        tests/test_reports.cpp
        tests/test_npd.cpp
    )
    target_link_libraries(schetovod_tests PRIVATE schetovod_core GTest::gtest_main)
    schetovod_warnings(schetovod_tests)
    include(GoogleTest)
    gtest_discover_tests(schetovod_tests)
endif()
