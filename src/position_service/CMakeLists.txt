add_executable(signlang_eyes_position_service
        event_listener.cpp
        program_options.cpp
        position_parser.cpp
        main.cpp)

set_target_properties(signlang_eyes_position_service PROPERTIES
        OUTPUT_NAME position_service)

target_include_directories(signlang_eyes_position_service PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(signlang_eyes_position_service PRIVATE
        cxxopts::cxxopts
        spdlog::spdlog
        iceoryx2_runtime
        signlang::common
        Boost::headers
        boost_json_runtime
        minmea::minmea
        Threads::Threads)

install(TARGETS signlang_eyes_position_service DESTINATION ${CMAKE_INSTALL_BINDIR})
