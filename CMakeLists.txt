cmake_minimum_required(VERSION 3.20)

project(signlang_eyes_edgeai LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)


include(FetchContent)
set(FETCHCONTENT_UPDATES_DISCONNECTED ON CACHE BOOL "Do not update already-populated FetchContent dependencies")

if(NOT TARGET cxxopts)
    FetchContent_Declare(
            cxxopts
            GIT_REPOSITORY https://github.com/jarro2783/cxxopts.git
            GIT_TAG v3.3.1
            GIT_SHALLOW TRUE
    )

    FetchContent_MakeAvailable(cxxopts)
endif()

set(BUILD_TESTING OFF CACHE INTERNAL "")
set(BUILD_EXAMPLES OFF CACHE INTERNAL "")

set(IOX2_CXX_STD_VERSION 20 CACHE INTERNAL "")

if(NOT TARGET iceoryx2-cxx)

    FetchContent_Declare(
            iceoryx2
            GIT_REPOSITORY https://github.com/eclipse-iceoryx/iceoryx2.git
            GIT_TAG main
            GIT_SHALLOW TRUE
    )

    FetchContent_MakeAvailable(iceoryx2)

endif()

if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)$" AND NOT TARGET signlang::fftw3f)
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
