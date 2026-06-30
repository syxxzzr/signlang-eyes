if(NOT TARGET Boost::asio OR NOT TARGET Boost::json OR NOT TARGET Boost::mqtt5 OR NOT TARGET minmea::minmea)
    message(STATUS "Skipping signlang_eyes_position_service: Boost.Asio, Boost.JSON, Boost.MQTT5, or minmea target not available")
    return()
endif()

add_executable(signlang_eyes_position_service
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
        signlang::common
        Boost::asio
        Boost::json
        Boost::mqtt5
        minmea::minmea
        Threads::Threads)

install(TARGETS signlang_eyes_position_service DESTINATION bin)
