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

# Disable PIE to allow linking with prebuilt FFTW library
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

# cxxopts
add_library(cxxopts INTERFACE)
target_include_directories(cxxopts INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/third_party/cxxopts/include)
add_library(cxxopts::cxxopts ALIAS cxxopts)

# toml++
add_library(tomlplusplus INTERFACE)
target_include_directories(tomlplusplus INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/third_party/tomlplusplus/include)
add_library(tomlplusplus::tomlplusplus ALIAS tomlplusplus)

# Boost
set(SIGNLANG_BOOST_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/third_party/boost)
if(EXISTS ${SIGNLANG_BOOST_ROOT}/libs/asio/include/boost/asio.hpp
        AND EXISTS ${SIGNLANG_BOOST_ROOT}/libs/json/include/boost/json.hpp
        AND EXISTS ${SIGNLANG_BOOST_ROOT}/libs/mqtt5/include/boost/mqtt5.hpp)
    file(GLOB SIGNLANG_BOOST_LIB_INCLUDE_DIRS
        LIST_DIRECTORIES true
        ${SIGNLANG_BOOST_ROOT}/libs/*/include)

    add_library(signlang_boost_asio INTERFACE)
    target_include_directories(signlang_boost_asio INTERFACE ${SIGNLANG_BOOST_LIB_INCLUDE_DIRS})
    add_library(Boost::asio ALIAS signlang_boost_asio)

    add_library(signlang_boost_json INTERFACE)
    target_include_directories(signlang_boost_json INTERFACE ${SIGNLANG_BOOST_LIB_INCLUDE_DIRS})
    target_compile_definitions(signlang_boost_json INTERFACE BOOST_JSON_HEADER_ONLY)
    add_library(Boost::json ALIAS signlang_boost_json)

    add_library(signlang_boost_mqtt5 INTERFACE)
    target_include_directories(signlang_boost_mqtt5 INTERFACE ${SIGNLANG_BOOST_LIB_INCLUDE_DIRS})
    add_library(Boost::mqtt5 ALIAS signlang_boost_mqtt5)
else()
    find_package(Boost CONFIG QUIET)
    include(CheckIncludeFileCXX)
    set(CMAKE_REQUIRED_QUIET TRUE)
    check_include_file_cxx(boost/asio.hpp SIGNLANG_HAVE_BOOST_ASIO)
    check_include_file_cxx(boost/json.hpp SIGNLANG_HAVE_BOOST_JSON)
    check_include_file_cxx(boost/mqtt5.hpp SIGNLANG_HAVE_BOOST_MQTT5)
    unset(CMAKE_REQUIRED_QUIET)
    if(Boost_FOUND AND SIGNLANG_HAVE_BOOST_ASIO AND SIGNLANG_HAVE_BOOST_JSON AND SIGNLANG_HAVE_BOOST_MQTT5)
        if(NOT TARGET Boost::asio AND TARGET Boost::headers)
            add_library(signlang_boost_asio INTERFACE)
            target_link_libraries(signlang_boost_asio INTERFACE Boost::headers)
            add_library(Boost::asio ALIAS signlang_boost_asio)
        endif()
        if(NOT TARGET Boost::json AND TARGET Boost::headers)
            add_library(signlang_boost_json INTERFACE)
            target_link_libraries(signlang_boost_json INTERFACE Boost::headers)
            target_compile_definitions(signlang_boost_json INTERFACE BOOST_JSON_HEADER_ONLY)
            add_library(Boost::json ALIAS signlang_boost_json)
        endif()
        if(NOT TARGET Boost::mqtt5 AND TARGET Boost::headers)
            add_library(signlang_boost_mqtt5 INTERFACE)
            target_link_libraries(signlang_boost_mqtt5 INTERFACE Boost::headers)
            add_library(Boost::mqtt5 ALIAS signlang_boost_mqtt5)
        endif()
    endif()
endif()

# minmea
set(SIGNLANG_MINMEA_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/minmea/include)
set(SIGNLANG_MINMEA_LIB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/minmea/lib/aarch64)
if(EXISTS ${SIGNLANG_MINMEA_INCLUDE_DIR}/minmea.h AND EXISTS ${SIGNLANG_MINMEA_LIB_DIR}/libminmea.a)
    add_library(minmea STATIC IMPORTED GLOBAL)
    set_target_properties(minmea PROPERTIES
        IMPORTED_LOCATION ${SIGNLANG_MINMEA_LIB_DIR}/libminmea.a
        INTERFACE_INCLUDE_DIRECTORIES ${SIGNLANG_MINMEA_INCLUDE_DIR})
    add_library(minmea::minmea ALIAS minmea)
endif()

# Threads
find_package(Threads REQUIRED)

# SQLiteCpp
add_library(sqlite3 STATIC IMPORTED GLOBAL)
set_target_properties(sqlite3 PROPERTIES
    IMPORTED_LOCATION ${CMAKE_CURRENT_SOURCE_DIR}/third_party/SQLiteCpp/lib/aarch64/libsqlite3.a
    INTERFACE_INCLUDE_DIRECTORIES ${CMAKE_CURRENT_SOURCE_DIR}/third_party/SQLiteCpp/include)
add_library(SQLite::SQLite3 ALIAS sqlite3)

add_library(SQLiteCpp STATIC IMPORTED GLOBAL)
set_target_properties(SQLiteCpp PROPERTIES
    IMPORTED_LOCATION ${CMAKE_CURRENT_SOURCE_DIR}/third_party/SQLiteCpp/lib/aarch64/libSQLiteCpp.a
    INTERFACE_INCLUDE_DIRECTORIES ${CMAKE_CURRENT_SOURCE_DIR}/third_party/SQLiteCpp/include)
target_link_libraries(SQLiteCpp INTERFACE SQLite::SQLite3 Threads::Threads ${CMAKE_DL_LIBS})
add_library(SQLiteCpp::SQLiteCpp ALIAS SQLiteCpp)


# spdlog
add_library(spdlog SHARED IMPORTED GLOBAL)
set_target_properties(spdlog PROPERTIES
    IMPORTED_LOCATION ${CMAKE_CURRENT_SOURCE_DIR}/third_party/spdlog/lib/aarch64/libspdlog.so
    INTERFACE_INCLUDE_DIRECTORIES ${CMAKE_CURRENT_SOURCE_DIR}/third_party/spdlog/include
    INTERFACE_COMPILE_DEFINITIONS "SPDLOG_COMPILED_LIB;SPDLOG_SHARED_LIB")
add_library(spdlog::spdlog ALIAS spdlog)

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

# libjpeg-turbo
set(SIGNLANG_TURBOJPEG_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/libjpeg_turbo/include)
set(SIGNLANG_TURBOJPEG_LIB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/libjpeg_turbo/lib/aarch64)
add_library(libjpeg_turbo STATIC IMPORTED GLOBAL)
set_target_properties(libjpeg_turbo PROPERTIES
    IMPORTED_LOCATION ${SIGNLANG_TURBOJPEG_LIB_DIR}/libturbojpeg.a
    INTERFACE_INCLUDE_DIRECTORIES ${SIGNLANG_TURBOJPEG_INCLUDE_DIR})
add_library(libjpeg-turbo::turbojpeg ALIAS libjpeg_turbo)

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
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/spdlog/lib/aarch64/libspdlog.so.1.17.0
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/spdlog/lib/aarch64/libspdlog.so.1.17
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/spdlog/lib/aarch64/libspdlog.so
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
