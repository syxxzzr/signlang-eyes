add_executable(signlang_eyes_launcher
        main.cpp
        program_options.cpp)

set_target_properties(signlang_eyes_launcher PROPERTIES
        OUTPUT_NAME launcher)

target_include_directories(signlang_eyes_launcher PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(signlang_eyes_launcher PRIVATE
        cxxopts::cxxopts
        tomlplusplus::tomlplusplus
        spdlog::spdlog)

install(TARGETS signlang_eyes_launcher RUNTIME DESTINATION .)
