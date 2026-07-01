cmake_minimum_required(VERSION 3.20)

project(signlang_eyes LANGUAGES CXX C)

if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_VERSION VERSION_LESS 12.3)
    message(FATAL_ERROR
        "This project requires GCC 12.3 or newer.\n"
        "Current C++ compiler: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
endif()

# This project ONLY supports aarch64 target platform
if(NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)$")
    message(FATAL_ERROR
        "This project only supports aarch64 target platform.\n"
        "Current CMAKE_SYSTEM_PROCESSOR: ${CMAKE_SYSTEM_PROCESSOR}\n"
        "Please configure with: -DCMAKE_SYSTEM_PROCESSOR=aarch64")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-pedantic-errors>)

# RPATH settings for runtime library search
set(CMAKE_INSTALL_RPATH "$ORIGIN/../lib")
set(CMAKE_BUILD_WITH_INSTALL_RPATH FALSE)
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH FALSE)
set(CMAKE_SKIP_BUILD_RPATH FALSE)
set(CMAKE_BUILD_RPATH_USE_ORIGIN TRUE)

# Disable PIE to match the project's aarch64 runtime/linking assumptions.
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -no-pie")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fno-pie")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-pie")

set(SIGNLANG_MODEL_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/models CACHE PATH "Local root directory for model assets")
set(SIGNLANG_BILSTM_MODEL_FILE ${SIGNLANG_MODEL_ROOT}/bilstm/biltsm.rknn CACHE FILEPATH
    "Shared BiLSTM gesture encoder RKNN model")
set(SIGNLANG_PROTOTYPES_FILE ${CMAKE_CURRENT_SOURCE_DIR}/conf/prototypes.sqlite CACHE FILEPATH
    "Shared sign language prototype SQLite database")

# Add src directory to include path for cross-module headers
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src)

# Conan-managed dependencies
find_package(cxxopts REQUIRED CONFIG)
find_package(cpp-pinyin REQUIRED CONFIG)
find_package(tomlplusplus REQUIRED CONFIG)
find_package(Boost REQUIRED CONFIG)
find_package(minmea REQUIRED CONFIG)
find_package(SQLiteCpp REQUIRED CONFIG)
find_package(spdlog REQUIRED CONFIG)
find_package(FFTW3 REQUIRED CONFIG)
find_package(iceoryx2-cxx REQUIRED CONFIG)
find_package(onnxruntime REQUIRED CONFIG)
find_package(rknn-runtime REQUIRED CONFIG)
find_package(librga REQUIRED CONFIG)

add_library(boost_json_runtime INTERFACE)
target_include_directories(boost_json_runtime INTERFACE ${boost_INCLUDE_DIRS_RELEASE})
target_link_directories(boost_json_runtime INTERFACE ${boost_LIB_DIRS_RELEASE})
target_link_libraries(boost_json_runtime INTERFACE boost_json boost_container)

add_library(iceoryx2_runtime INTERFACE)
target_include_directories(iceoryx2_runtime INTERFACE ${iceoryx2_INCLUDE_DIRS_RELEASE})
target_link_directories(iceoryx2_runtime INTERFACE ${iceoryx2_LIB_DIRS_RELEASE})
target_link_options(iceoryx2_runtime INTERFACE ${iceoryx2_LINKER_FLAGS_RELEASE})
target_link_libraries(iceoryx2_runtime INTERFACE iceoryx2_cxx iceoryx2_ffi_c dl rt m pthread)

add_library(onnxruntime_runtime INTERFACE)
target_include_directories(onnxruntime_runtime INTERFACE ${onnxruntime_INCLUDE_DIRS_RELEASE})
target_link_directories(onnxruntime_runtime INTERFACE ${onnxruntime_LIB_DIRS_RELEASE})
target_link_libraries(onnxruntime_runtime INTERFACE onnxruntime m pthread)

# Threads
find_package(Threads REQUIRED)

# System dependencies
find_package(ALSA REQUIRED)

# GLib/GIO for BlueZ D-Bus BLE GATT server
find_path(SIGNLANG_GLIB_INCLUDE_DIR
    NAMES glib.h
    PATH_SUFFIXES glib-2.0)
find_path(SIGNLANG_GLIB_CONFIG_INCLUDE_DIR
    NAMES glibconfig.h
    PATH_SUFFIXES lib/glib-2.0/include glib-2.0/include)
find_path(SIGNLANG_GIO_INCLUDE_DIR
    NAMES gio/gio.h
    PATH_SUFFIXES glib-2.0)
find_library(SIGNLANG_GIO_LIBRARY NAMES gio-2.0)
find_library(SIGNLANG_GOBJECT_LIBRARY NAMES gobject-2.0)
find_library(SIGNLANG_GLIB_LIBRARY NAMES glib-2.0)
find_path(SIGNLANG_TURBOJPEG_INCLUDE_DIR NAMES turbojpeg.h)
find_library(SIGNLANG_TURBOJPEG_LIBRARY NAMES turbojpeg)

if(SIGNLANG_GLIB_INCLUDE_DIR AND SIGNLANG_GLIB_CONFIG_INCLUDE_DIR AND SIGNLANG_GIO_INCLUDE_DIR
        AND SIGNLANG_GIO_LIBRARY AND SIGNLANG_GOBJECT_LIBRARY AND SIGNLANG_GLIB_LIBRARY)
    add_library(signlang_gio INTERFACE)
    target_include_directories(signlang_gio INTERFACE
        ${SIGNLANG_GLIB_INCLUDE_DIR}
        ${SIGNLANG_GLIB_CONFIG_INCLUDE_DIR}
        ${SIGNLANG_GIO_INCLUDE_DIR})
    target_link_libraries(signlang_gio INTERFACE
        ${SIGNLANG_GIO_LIBRARY}
        ${SIGNLANG_GOBJECT_LIBRARY}
        ${SIGNLANG_GLIB_LIBRARY})
    add_library(signlang::gio ALIAS signlang_gio)
else()
    message(WARNING "GLib/GIO development files were not found; signlang_manager will be skipped")
endif()

if(SIGNLANG_TURBOJPEG_INCLUDE_DIR AND SIGNLANG_TURBOJPEG_LIBRARY)
    add_library(turbojpeg_runtime INTERFACE)
    target_include_directories(turbojpeg_runtime INTERFACE ${SIGNLANG_TURBOJPEG_INCLUDE_DIR})
    target_link_libraries(turbojpeg_runtime INTERFACE ${SIGNLANG_TURBOJPEG_LIBRARY})
else()
    message(FATAL_ERROR "libjpeg-turbo headers/libraries were not found in the target sysroot")
endif()

# Modules
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/src/common)
file(GLOB MODULE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/src/*)
foreach(MODULE_DIR ${MODULE_DIRS})
    if (IS_DIRECTORY ${MODULE_DIR} AND EXISTS ${MODULE_DIR}/CMakeLists.txt
            AND NOT "${MODULE_DIR}" STREQUAL "${CMAKE_CURRENT_SOURCE_DIR}/src/common")
        add_subdirectory(${MODULE_DIR})
    endif()
endforeach()

string(TOUPPER "${CMAKE_BUILD_TYPE}" SIGNLANG_INSTALL_CONFIG)
if(NOT SIGNLANG_INSTALL_CONFIG)
    set(SIGNLANG_INSTALL_CONFIG RELEASE)
endif()

function(signlang_install_conan_shared_libs PACKAGE_FOLDER_VARIABLE)
    if(NOT DEFINED ${PACKAGE_FOLDER_VARIABLE})
        return()
    endif()

    set(PACKAGE_FOLDER "${${PACKAGE_FOLDER_VARIABLE}}")
    if(EXISTS "${PACKAGE_FOLDER}/lib")
        set(SIGNLANG_SHARED_LIB_PATTERNS ${ARGN})
        if(NOT SIGNLANG_SHARED_LIB_PATTERNS)
            set(SIGNLANG_SHARED_LIB_PATTERNS
                PATTERN "*.so"
                PATTERN "*.so.*")
        endif()

        install(DIRECTORY "${PACKAGE_FOLDER}/lib/"
            DESTINATION lib
            FILES_MATCHING
            ${SIGNLANG_SHARED_LIB_PATTERNS}
            PATTERN "cmake" EXCLUDE
            PATTERN "pkgconfig" EXCLUDE)
    endif()
endfunction()

signlang_install_conan_shared_libs(boost_PACKAGE_FOLDER_${SIGNLANG_INSTALL_CONFIG}
    PATTERN "libboost_json.so*"
    PATTERN "libboost_container.so*")

foreach(SIGNLANG_RUNTIME_PACKAGE
        iceoryx2
        librga
        onnxruntime
        rknn-runtime
        spdlog)
    signlang_install_conan_shared_libs("${SIGNLANG_RUNTIME_PACKAGE}_PACKAGE_FOLDER_${SIGNLANG_INSTALL_CONFIG}")
endforeach()

# Install default launcher configuration
install(FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/conf/conf.toml
    ${CMAKE_CURRENT_SOURCE_DIR}/conf/system_prompt.txt
    DESTINATION conf)

# Install shared sign language assets used by signlang_det and signlang_manager
install(FILES
    ${SIGNLANG_BILSTM_MODEL_FILE}
    DESTINATION models/bilstm)

install(FILES
    ${SIGNLANG_PROTOTYPES_FILE}
    DESTINATION conf)
