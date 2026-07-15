cmake_minimum_required(VERSION 3.20)

project(signlang_eyes_host_tests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(SIGNLANG_TEST_WARNINGS_AS_ERRORS "Treat host-test compiler warnings as errors" OFF)
option(SIGNLANG_TEST_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

include(FetchContent)
FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.7.1
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(Catch2)

find_package(Threads REQUIRED)

add_executable(signlang_unit_tests
    audio_ring_buffer_test.cpp
    fixed_string_test.cpp
    protocol_test.cpp
    serial_protocol_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/common/audio_ring_buffer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/signlang_manager/protocol.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/peripheral_service/serial_protocol.cpp)

target_include_directories(signlang_unit_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
target_link_libraries(signlang_unit_tests PRIVATE Catch2::Catch2WithMain Threads::Threads)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(signlang_unit_tests PRIVATE -Wall -Wextra -Wpedantic)
    if(SIGNLANG_TEST_WARNINGS_AS_ERRORS)
        target_compile_options(signlang_unit_tests PRIVATE -Werror)
    endif()
endif()

if(SIGNLANG_TEST_SANITIZERS)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        message(FATAL_ERROR "SIGNLANG_TEST_SANITIZERS requires GCC or Clang")
    endif()
    target_compile_options(signlang_unit_tests PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(signlang_unit_tests PRIVATE -fsanitize=address,undefined)
endif()

include(CTest)
include(Catch)
catch_discover_tests(signlang_unit_tests)
