cmake_minimum_required(VERSION 3.20)

project(signlang_eyes LANGUAGES CXX C)

include(GNUInstallDirs)

if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_VERSION VERSION_LESS 11)
    message(FATAL_ERROR
        "This project requires GCC 11 or newer.\n"
        "Current C++ compiler: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
endif()

# This project ONLY supports aarch64 target platform
set(SIGNLANG_TARGET_PROCESSOR "${CMAKE_SYSTEM_PROCESSOR}" CACHE STRING
    "Target processor name; supported aliases are aarch64, arm64, and armv8")
if(NOT SIGNLANG_TARGET_PROCESSOR MATCHES "^(aarch64|arm64|armv8)$")
    message(FATAL_ERROR
        "This project only supports aarch64 target platform.\n"
        "Current SIGNLANG_TARGET_PROCESSOR: ${SIGNLANG_TARGET_PROCESSOR}\n"
        "Current CMAKE_SYSTEM_PROCESSOR: ${CMAKE_SYSTEM_PROCESSOR}\n"
        "Please configure with: -DCMAKE_SYSTEM_PROCESSOR=aarch64")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-pedantic-errors>)

# RPATH settings for runtime library search
set(CMAKE_INSTALL_RPATH "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}")
set(CMAKE_BUILD_WITH_INSTALL_RPATH FALSE)
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH FALSE)
set(CMAKE_SKIP_BUILD_RPATH FALSE)
set(CMAKE_BUILD_RPATH_USE_ORIGIN TRUE)

option(SIGNLANG_ENABLE_NO_PIE "Build executables without PIE for target runtimes that require it" ON)
option(SIGNLANG_INSTALL_CONAN_RUNTIME_LIBS "Install shared libraries from Conan packages into the runtime lib directory" ON)
option(SIGNLANG_RUNTIME_ASSETS_REQUIRED "Fail configure when runtime model/config assets are missing" OFF)

if(SIGNLANG_ENABLE_NO_PIE)
    add_compile_options($<$<COMPILE_LANGUAGE:C>:-fno-pie> $<$<COMPILE_LANGUAGE:CXX>:-fno-pie>)
    add_link_options($<$<STREQUAL:$<TARGET_PROPERTY:TYPE>,EXECUTABLE>:-no-pie>)
endif()

set(SIGNLANG_MODEL_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/models CACHE PATH "Local root directory for model assets")
set(SIGNLANG_TEMPORAL_ENCODER_FILE ${SIGNLANG_MODEL_ROOT}/signlang/temporal_encoder.rknn CACHE FILEPATH
    "Shared temporal gesture encoder RKNN model")
set(SIGNLANG_PROTOTYPES_FILE ${CMAKE_CURRENT_SOURCE_DIR}/conf/prototypes.sqlite CACHE FILEPATH
    "Shared sign language prototype SQLite database")

function(signlang_report_missing_runtime_asset SOURCE_FILE)
    if(SIGNLANG_RUNTIME_ASSETS_REQUIRED)
        message(FATAL_ERROR "Required runtime asset not found: ${SOURCE_FILE}")
    else()
        message(WARNING "Runtime asset not found; skipping copy/install: ${SOURCE_FILE}")
    endif()
endfunction()

function(signlang_add_runtime_asset TARGET_NAME SOURCE_FILE DESTINATION_DIR DESTINATION_NAME)
    if(EXISTS "${SOURCE_FILE}")
        add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "${DESTINATION_DIR}"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${SOURCE_FILE}"
            "${DESTINATION_DIR}/${DESTINATION_NAME}")
    else()
        signlang_report_missing_runtime_asset("${SOURCE_FILE}")
    endif()
endfunction()

function(signlang_install_runtime_files DESTINATION_DIR)
    foreach(RUNTIME_FILE ${ARGN})
        if(EXISTS "${RUNTIME_FILE}")
            install(FILES "${RUNTIME_FILE}" DESTINATION "${DESTINATION_DIR}")
        else()
            signlang_report_missing_runtime_asset("${RUNTIME_FILE}")
        endif()
    endforeach()
endfunction()

function(signlang_require_target TARGET_NAME DESCRIPTION)
    if(NOT TARGET ${TARGET_NAME})
        message(FATAL_ERROR "Missing required target ${TARGET_NAME}: ${DESCRIPTION}")
    endif()
endfunction()

set(SIGNLANG_CONAN_CONFIG "${CMAKE_BUILD_TYPE}" CACHE STRING
    "Conan dependency configuration used by generated CMakeDeps files")
if(NOT SIGNLANG_CONAN_CONFIG)
    set(SIGNLANG_CONAN_CONFIG Release)
endif()
string(TOUPPER "${SIGNLANG_CONAN_CONFIG}" SIGNLANG_CONAN_CONFIG_UPPER)

function(signlang_get_conan_package_folder PACKAGE_NAME OUT_VAR)
    set(PACKAGE_FOLDER_VARIABLE "${PACKAGE_NAME}_PACKAGE_FOLDER_${SIGNLANG_CONAN_CONFIG_UPPER}")
    if(NOT DEFINED ${PACKAGE_FOLDER_VARIABLE})
        message(FATAL_ERROR
            "Conan package folder variable is not defined: ${PACKAGE_FOLDER_VARIABLE}\n"
            "Run conan install with CMakeDeps for configuration ${SIGNLANG_CONAN_CONFIG}.")
    endif()
    set(${OUT_VAR} "${${PACKAGE_FOLDER_VARIABLE}}" PARENT_SCOPE)
endfunction()

function(signlang_get_conan_config_var VARIABLE_PREFIX VARIABLE_NAME OUT_VAR)
    set(CONFIG_VARIABLE "${VARIABLE_PREFIX}_${VARIABLE_NAME}_${SIGNLANG_CONAN_CONFIG_UPPER}")
    if(NOT DEFINED ${CONFIG_VARIABLE})
        message(FATAL_ERROR
            "Conan configuration variable is not defined: ${CONFIG_VARIABLE}\n"
            "Run conan install with CMakeDeps for configuration ${SIGNLANG_CONAN_CONFIG}.")
    endif()
    set(${OUT_VAR} "${${CONFIG_VARIABLE}}" PARENT_SCOPE)
endfunction()

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
find_package(libjpeg-turbo REQUIRED CONFIG)
find_package(onnxruntime REQUIRED CONFIG)
find_package(rknn-runtime REQUIRED CONFIG)
find_package(librga REQUIRED CONFIG)

add_library(boost_json_runtime INTERFACE)
target_link_libraries(boost_json_runtime INTERFACE Boost::json Boost::container)

add_library(iceoryx2_runtime INTERFACE)
signlang_get_conan_config_var(iceoryx2 INCLUDE_DIRS ICEORYX2_INCLUDE_DIRS)
signlang_get_conan_config_var(iceoryx2 LIB_DIRS ICEORYX2_LIB_DIRS)
signlang_get_conan_config_var(iceoryx2 LINKER_FLAGS ICEORYX2_LINKER_FLAGS)
target_include_directories(iceoryx2_runtime INTERFACE ${ICEORYX2_INCLUDE_DIRS})
target_link_directories(iceoryx2_runtime INTERFACE ${ICEORYX2_LIB_DIRS})
target_link_options(iceoryx2_runtime INTERFACE ${ICEORYX2_LINKER_FLAGS})
target_link_libraries(iceoryx2_runtime INTERFACE iceoryx2_cxx iceoryx2_ffi_c dl rt m pthread)

add_library(onnxruntime_runtime INTERFACE)
target_link_libraries(onnxruntime_runtime INTERFACE onnxruntime::onnxruntime)

# Threads
find_package(Threads REQUIRED)

# System dependencies
find_package(ALSA REQUIRED)
find_package(OpenSSL REQUIRED)

# GLib/GIO for BlueZ D-Bus BLE GATT server
set(SIGNLANG_GLIB_CONFIG_INCLUDE_PATH_SUFFIXES
    lib/glib-2.0/include
    glib-2.0/include)
if(CMAKE_LIBRARY_ARCHITECTURE)
    list(PREPEND SIGNLANG_GLIB_CONFIG_INCLUDE_PATH_SUFFIXES
        lib/${CMAKE_LIBRARY_ARCHITECTURE}/glib-2.0/include)
endif()
find_path(SIGNLANG_GLIB_INCLUDE_DIR
    NAMES glib.h
    PATH_SUFFIXES glib-2.0)
find_path(SIGNLANG_GLIB_CONFIG_INCLUDE_DIR
    NAMES glibconfig.h
    PATH_SUFFIXES ${SIGNLANG_GLIB_CONFIG_INCLUDE_PATH_SUFFIXES})
find_path(SIGNLANG_GIO_INCLUDE_DIR
    NAMES gio/gio.h
    PATH_SUFFIXES glib-2.0)
find_library(SIGNLANG_GIO_LIBRARY NAMES gio-2.0)
find_library(SIGNLANG_GOBJECT_LIBRARY NAMES gobject-2.0)
find_library(SIGNLANG_GLIB_LIBRARY NAMES glib-2.0)

if(NOT SIGNLANG_GLIB_INCLUDE_DIR OR NOT SIGNLANG_GLIB_CONFIG_INCLUDE_DIR OR NOT SIGNLANG_GIO_INCLUDE_DIR
        OR NOT SIGNLANG_GIO_LIBRARY OR NOT SIGNLANG_GOBJECT_LIBRARY OR NOT SIGNLANG_GLIB_LIBRARY)
    message(FATAL_ERROR
        "GLib/GIO development files were not found in the target environment.\n"
        "glib.h: ${SIGNLANG_GLIB_INCLUDE_DIR}\n"
        "glibconfig.h: ${SIGNLANG_GLIB_CONFIG_INCLUDE_DIR}\n"
        "gio/gio.h: ${SIGNLANG_GIO_INCLUDE_DIR}\n"
        "gio-2.0: ${SIGNLANG_GIO_LIBRARY}\n"
        "gobject-2.0: ${SIGNLANG_GOBJECT_LIBRARY}\n"
        "glib-2.0: ${SIGNLANG_GLIB_LIBRARY}")
endif()

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

add_library(turbojpeg_runtime INTERFACE)
target_link_libraries(turbojpeg_runtime INTERFACE libjpeg-turbo::turbojpeg-static)

# Modules
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/src/common)
file(GLOB MODULE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/src/*)
foreach(MODULE_DIR ${MODULE_DIRS})
    if (IS_DIRECTORY ${MODULE_DIR} AND EXISTS ${MODULE_DIR}/CMakeLists.txt
            AND NOT "${MODULE_DIR}" STREQUAL "${CMAKE_CURRENT_SOURCE_DIR}/src/common")
        add_subdirectory(${MODULE_DIR})
    endif()
endforeach()

function(signlang_install_conan_shared_libs PACKAGE_NAME)
    if(NOT SIGNLANG_INSTALL_CONAN_RUNTIME_LIBS)
        return()
    endif()

    signlang_get_conan_package_folder("${PACKAGE_NAME}" PACKAGE_FOLDER)
    if(NOT EXISTS "${PACKAGE_FOLDER}/lib")
        message(FATAL_ERROR "Conan package ${PACKAGE_NAME} does not contain a lib directory: ${PACKAGE_FOLDER}/lib")
    endif()

    set(SIGNLANG_SHARED_LIB_PATTERNS ${ARGN})
    if(NOT SIGNLANG_SHARED_LIB_PATTERNS)
        set(SIGNLANG_SHARED_LIB_PATTERNS
            PATTERN "*.so"
            PATTERN "*.so.*")
    endif()

    install(DIRECTORY "${PACKAGE_FOLDER}/lib/"
        DESTINATION ${CMAKE_INSTALL_LIBDIR}
        FILES_MATCHING
        ${SIGNLANG_SHARED_LIB_PATTERNS}
        PATTERN "cmake" EXCLUDE
        PATTERN "pkgconfig" EXCLUDE)
endfunction()

signlang_install_conan_shared_libs(boost
    PATTERN "libboost_json.so*"
    PATTERN "libboost_container.so*")

foreach(SIGNLANG_RUNTIME_PACKAGE
        iceoryx2
        librga
        onnxruntime
        rknn-runtime
        spdlog)
    signlang_install_conan_shared_libs("${SIGNLANG_RUNTIME_PACKAGE}")
endforeach()

signlang_install_runtime_files(conf
    ${CMAKE_CURRENT_SOURCE_DIR}/conf/conf.toml
    ${CMAKE_CURRENT_SOURCE_DIR}/conf/system_prompt.txt
    ${CMAKE_CURRENT_SOURCE_DIR}/conf/unifont-17.0.05.hex)

# Install shared sign language assets used by signlang_det and signlang_manager
signlang_install_runtime_files(models/signlang
    ${SIGNLANG_TEMPORAL_ENCODER_FILE})

signlang_install_runtime_files(conf
    ${SIGNLANG_PROTOTYPES_FILE})
