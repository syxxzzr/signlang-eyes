cmake_minimum_required(VERSION 3.20)

project(signlang_eyes LANGUAGES CXX C)

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
find_package(tomlplusplus REQUIRED CONFIG)
find_package(Boost REQUIRED CONFIG)
find_package(minmea REQUIRED CONFIG)
find_package(SQLiteCpp REQUIRED CONFIG)
find_package(spdlog REQUIRED CONFIG)
find_package(FFTW3 REQUIRED CONFIG)
find_package(libjpeg-turbo REQUIRED CONFIG)

# Threads
find_package(Threads REQUIRED)

# iceoryx2
list(APPEND CMAKE_PREFIX_PATH ${CMAKE_CURRENT_SOURCE_DIR}/third_party/iceoryx2)
find_package(iceoryx2-cxx REQUIRED)

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

# RKNN Runtime headers
add_library(signlang_rknn_headers INTERFACE)
target_include_directories(signlang_rknn_headers INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/third_party/rknn_runtime/include)
add_library(signlang::rknn_headers ALIAS signlang_rknn_headers)

# RKNN Runtime library
add_library(signlang_rknn_runtime SHARED IMPORTED GLOBAL)
set_target_properties(signlang_rknn_runtime PROPERTIES
    IMPORTED_LOCATION ${CMAKE_CURRENT_SOURCE_DIR}/third_party/rknn_runtime/lib/aarch64/librknnrt.so
    INTERFACE_INCLUDE_DIRECTORIES ${CMAKE_CURRENT_SOURCE_DIR}/third_party/rknn_runtime/include)
target_link_libraries(signlang_rknn_runtime INTERFACE signlang::rknn_headers)
add_library(signlang::rknn_runtime ALIAS signlang_rknn_runtime)

# librga (Rockchip RGA hardware accelerator)
add_library(librga SHARED IMPORTED GLOBAL)
set_target_properties(librga PROPERTIES
    IMPORTED_LOCATION ${CMAKE_CURRENT_SOURCE_DIR}/third_party/librga/lib/aarch64/librga.so
    INTERFACE_INCLUDE_DIRECTORIES ${CMAKE_CURRENT_SOURCE_DIR}/third_party/librga/include)
add_library(librga::rga ALIAS librga)

# Modules
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/src/common)
file(GLOB MODULE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/src/*)
foreach(MODULE_DIR ${MODULE_DIRS})
    if (IS_DIRECTORY ${MODULE_DIR} AND EXISTS ${MODULE_DIR}/CMakeLists.txt
            AND NOT "${MODULE_DIR}" STREQUAL "${CMAKE_CURRENT_SOURCE_DIR}/src/common")
        add_subdirectory(${MODULE_DIR})
    endif()
endforeach()

# Install shared libraries to lib
install(FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/iceoryx2/lib/libiceoryx2_cxx.so
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/iceoryx2/lib/libiceoryx2_ffi_c.so
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/rknn_runtime/lib/aarch64/librknnrt.so
    DESTINATION lib)

install(FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/librga/lib/aarch64/librga.so
    DESTINATION lib)

# Install default launcher configuration
install(FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/conf/conf.toml
    DESTINATION conf)

# Install shared sign language assets used by signlang_det and signlang_manager
install(FILES
    ${SIGNLANG_BILSTM_MODEL_FILE}
    DESTINATION models/bilstm)

install(FILES
    ${SIGNLANG_PROTOTYPES_FILE}
    DESTINATION conf)
