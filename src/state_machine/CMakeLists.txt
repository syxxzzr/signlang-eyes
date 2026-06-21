add_executable(signlang_eyes_state_machine
        app_state.cpp
        state_control.cpp
        iceoryx_gateway.cpp
        program_options.cpp
        main.cpp)

set_target_properties(signlang_eyes_state_machine PROPERTIES
        OUTPUT_NAME state_machine)

target_include_directories(signlang_eyes_state_machine PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(signlang_eyes_state_machine PRIVATE
        cxxopts::cxxopts
        spdlog::spdlog
        iceoryx2-cxx::shared-lib-cxx)

install(TARGETS signlang_eyes_state_machine DESTINATION bin)
