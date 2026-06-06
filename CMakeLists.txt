cmake_minimum_required(VERSION 3.20)

project(signlang_eyes_edgeai LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(SIGNLANG_ENABLE_TESTS "Build signlang-eyes-edgeai test executables" ON)

if(SIGNLANG_ENABLE_TESTS)
    enable_testing()
endif()

include(FetchContent)
set(FETCHCONTENT_UPDATES_DISCONNECTED ON CACHE BOOL "Do not update already-populated FetchContent dependencies")

set(BUILD_TESTING OFF CACHE BOOL "Disable third-party dependency tests" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "Disable third-party dependency examples" FORCE)
set(IOX2_CXX_STD_VERSION 20 CACHE STRING "C++ standard used by iceoryx2" FORCE)

if(NOT TARGET cxxopts)
    FetchContent_Declare(
            cxxopts
            GIT_REPOSITORY https://github.com/jarro2783/cxxopts.git
            GIT_TAG v3.3.1
            GIT_SHALLOW TRUE
    )

    FetchContent_MakeAvailable(cxxopts)
endif()

if(NOT TARGET iceoryx2-cxx)

    FetchContent_Declare(
            iceoryx2
            GIT_REPOSITORY https://github.com/eclipse-iceoryx/iceoryx2.git
            GIT_TAG 2e64dd8c18d7abe2065760d3d31addfedcd17161
    )

    FetchContent_MakeAvailable(iceoryx2)

endif()

find_package(ALSA REQUIRED)
find_package(Threads REQUIRED)

set(SIGNLANG_IS_AARCH64 OFF)
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)$")
    set(SIGNLANG_IS_AARCH64 ON)
endif()

set(SIGNLANG_RKNN_RUNTIME_ROOT
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/rknn_runtime
        CACHE PATH "Project-local RKNN runtime root")
set(SIGNLANG_RKNN_INCLUDE_DIR ${SIGNLANG_RKNN_RUNTIME_ROOT}/include)
set(SIGNLANG_RKNN_LIBRARY ${SIGNLANG_RKNN_RUNTIME_ROOT}/lib/aarch64/librknnrt.so)

add_library(signlang_rknn_headers INTERFACE)
target_include_directories(signlang_rknn_headers INTERFACE
        ${SIGNLANG_RKNN_INCLUDE_DIR})
add_library(signlang::rknn_headers ALIAS signlang_rknn_headers)

if(SIGNLANG_IS_AARCH64
        AND NOT TARGET signlang::rknn_runtime
        AND EXISTS "${SIGNLANG_RKNN_INCLUDE_DIR}/rknn_api.h"
        AND EXISTS "${SIGNLANG_RKNN_LIBRARY}")
    add_library(signlang_rknn_runtime SHARED IMPORTED GLOBAL)
    set_target_properties(signlang_rknn_runtime PROPERTIES
            IMPORTED_LOCATION ${SIGNLANG_RKNN_LIBRARY}
            INTERFACE_INCLUDE_DIRECTORIES ${SIGNLANG_RKNN_INCLUDE_DIR})
    target_link_libraries(signlang_rknn_runtime INTERFACE
            signlang::rknn_headers)
    add_library(signlang::rknn_runtime ALIAS signlang_rknn_runtime)
endif()

if(SIGNLANG_IS_AARCH64 AND NOT TARGET signlang::fftw3f)
    enable_language(C)

    set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build third-party dependencies as static libraries" FORCE)
    set(BUILD_TESTS OFF CACHE BOOL "Disable third-party dependency tests" FORCE)
    set(ENABLE_FLOAT ON CACHE BOOL "Build FFTW in single precision" FORCE)
    set(ENABLE_THREADS OFF CACHE BOOL "Disable FFTW threading support" FORCE)
    set(DISABLE_FORTRAN ON CACHE BOOL "Disable FFTW Fortran wrappers" FORCE)

    FetchContent_Declare(
            fftw
            GIT_REPOSITORY https://github.com/FFTW/fftw3.git
            GIT_TAG fftw-3.3.10
            GIT_SHALLOW TRUE
    )

    FetchContent_GetProperties(fftw)
    if(NOT fftw_POPULATED)
        FetchContent_Populate(fftw)
        add_subdirectory(${fftw_SOURCE_DIR} ${fftw_BINARY_DIR} EXCLUDE_FROM_ALL)
    endif()

    if(TARGET fftw3f)
        set(SIGNLANG_FFTW3F_TARGET fftw3f)
    elseif(TARGET fftw3)
        set(SIGNLANG_FFTW3F_TARGET fftw3)
    else()
        message(FATAL_ERROR "FFTW FetchContent did not define an fftw3f-compatible target")
    endif()

    add_library(signlang_fftw3f INTERFACE)
    target_link_libraries(signlang_fftw3f INTERFACE ${SIGNLANG_FFTW3F_TARGET})
    target_include_directories(signlang_fftw3f INTERFACE
            ${fftw_SOURCE_DIR}/api
            ${fftw_BINARY_DIR}/api)
    add_library(signlang::fftw3f ALIAS signlang_fftw3f)
endif()

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/audio_frontend)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/speech_asr)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/env_sound_det)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/video_frontend)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/handpose_det)
