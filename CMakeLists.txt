cmake_minimum_required(VERSION 3.20)

project(signlang_eyes_edgeai LANGUAGES CXX C)

# This project ONLY supports aarch64 target platform
if(NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)$")
    message(FATAL_ERROR
        "This project only supports aarch64 target platform.\n"
        "Current CMAKE_SYSTEM_PROCESSOR: ${CMAKE_SYSTEM_PROCESSOR}\n"
        "Please configure with: -DCMAKE_SYSTEM_PROCESSOR=aarch64")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Disable PIE to allow linking with prebuilt FFTW library
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -no-pie")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fno-pie")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-pie")

option(SIGNLANG_ENABLE_TESTS "Build signlang-eyes-edgeai test executables" ON)

if(SIGNLANG_ENABLE_TESTS)
    enable_testing()
endif()

set(SIGNLANG_MODEL_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/models CACHE PATH "Local root directory for model assets")

# cxxopts
add_library(cxxopts INTERFACE)
target_include_directories(cxxopts INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/third_party/cxxopts/include)
add_library(cxxopts::cxxopts ALIAS cxxopts)

# iceoryx2
list(APPEND CMAKE_PREFIX_PATH ${CMAKE_CURRENT_SOURCE_DIR}/third_party/iceoryx2)
find_package(iceoryx2-cxx REQUIRED)

# FFTW3f
add_library(fftw3f STATIC IMPORTED GLOBAL)
set_target_properties(fftw3f PROPERTIES
    IMPORTED_LOCATION ${CMAKE_CURRENT_SOURCE_DIR}/third_party/fftw/lib/aarch64/libfftw3f.a
    INTERFACE_INCLUDE_DIRECTORIES ${CMAKE_CURRENT_SOURCE_DIR}/third_party/fftw/include)

# System dependencies
find_package(ALSA REQUIRED)
find_package(Threads REQUIRED)

# RKNN Runtime headers
add_library(signlang_rknn_headers INTERFACE)
target_include_directories(signlang_rknn_headers INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/third_party/rknn_runtime/include)
add_library(signlang::rknn_headers ALIAS signlang_rknn_headers)

# RKNN Runtime library (prebuilt shared library)
add_library(signlang_rknn_runtime SHARED IMPORTED GLOBAL)
set_target_properties(signlang_rknn_runtime PROPERTIES
    IMPORTED_LOCATION ${CMAKE_CURRENT_SOURCE_DIR}/third_party/rknn_runtime/lib/aarch64/librknnrt.so
    INTERFACE_INCLUDE_DIRECTORIES ${CMAKE_CURRENT_SOURCE_DIR}/third_party/rknn_runtime/include)
target_link_libraries(signlang_rknn_runtime INTERFACE signlang::rknn_headers)
add_library(signlang::rknn_runtime ALIAS signlang_rknn_runtime)

# Modules
add_subdirectory(audio_frontend)
add_subdirectory(speech_asr)
add_subdirectory(env_sound_det)
add_subdirectory(video_frontend)
add_subdirectory(handpose_det)
