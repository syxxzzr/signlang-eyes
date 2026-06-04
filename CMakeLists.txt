cmake_minimum_required(VERSION 3.20)

project(signlang_eyes_edgeai LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)


include(FetchContent)

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

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/audio_frontend)
