if(NOT TARGET signlang::gio)
    message(STATUS "Skipping signlang_eyes_signlang_manager: GLib/GIO target not available")
    return()
endif()

add_executable(signlang_eyes_signlang_manager
        bluetooth_gatt_server.cpp
        iceoryx_gateway.cpp
        manager_service.cpp
        program_options.cpp
        protocol.cpp
        wire_handpose.cpp
        main.cpp)

set_target_properties(signlang_eyes_signlang_manager PROPERTIES
        OUTPUT_NAME signlang_manager)

target_include_directories(signlang_eyes_signlang_manager PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(signlang_eyes_signlang_manager PRIVATE
        cxxopts::cxxopts
        spdlog::spdlog
        iceoryx2-cxx::shared-lib-cxx
        signlang::gio
        Threads::Threads)

install(TARGETS signlang_eyes_signlang_manager DESTINATION bin)
