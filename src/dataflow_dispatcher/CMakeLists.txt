add_library(signlang_dataflow_dispatcher_core STATIC
        core.cpp)
target_include_directories(signlang_dataflow_dispatcher_core PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(signlang_dataflow_dispatcher_core PUBLIC
        signlang::common)

add_executable(signlang_eyes_dataflow_dispatcher
        ../state_machine/app_state.cpp
        iceoryx_gateway.cpp
        program_options.cpp
        main.cpp)

set_target_properties(signlang_eyes_dataflow_dispatcher PROPERTIES
        OUTPUT_NAME dataflow_dispatcher)

target_include_directories(signlang_eyes_dataflow_dispatcher PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(signlang_eyes_dataflow_dispatcher PRIVATE
        signlang_dataflow_dispatcher_core
        cxxopts::cxxopts
        spdlog::spdlog
        iceoryx2_runtime
        signlang::common)

install(TARGETS signlang_eyes_dataflow_dispatcher DESTINATION bin)
