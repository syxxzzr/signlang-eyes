add_executable(signlang_eyes_peripheral_service
        hex_font.cpp
        iceoryx_gateway.cpp
        oled_framebuffer.cpp
        program_options.cpp
        scrolling_display.cpp
        serial_protocol.cpp
        serial_transport.cpp
        text_renderer.cpp
        main.cpp)

set_target_properties(signlang_eyes_peripheral_service PROPERTIES
        OUTPUT_NAME peripheral_service)

target_include_directories(signlang_eyes_peripheral_service PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(signlang_eyes_peripheral_service PRIVATE
        cxxopts::cxxopts
        spdlog::spdlog
        iceoryx2_runtime
        signlang::common
        Boost::headers
        Threads::Threads)

add_optional_post_build_asset_runtime(signlang_eyes_peripheral_service
        "${CMAKE_CURRENT_SOURCE_DIR}/../../conf/unifont-17.0.05.hex"
        "$<TARGET_FILE_DIR:signlang_eyes_peripheral_service>/conf"
        unifont-17.0.05.hex)

install(TARGETS signlang_eyes_peripheral_service DESTINATION bin)
install_optional_files_runtime(conf
        ${CMAKE_CURRENT_SOURCE_DIR}/../../conf/unifont-17.0.05.hex)
