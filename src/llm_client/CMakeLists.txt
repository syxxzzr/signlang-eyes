add_executable(signlang_eyes_llm_client
        iceoryx_gateway.cpp
        openai_client.cpp
        program_options.cpp
        main.cpp)

set_target_properties(signlang_eyes_llm_client PROPERTIES
        OUTPUT_NAME llm_client)

target_include_directories(signlang_eyes_llm_client PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(signlang_eyes_llm_client PRIVATE
        cxxopts::cxxopts
        spdlog::spdlog
        iceoryx2_runtime
        signlang::common
        Boost::headers
        boost_json_runtime
        OpenSSL::SSL
        OpenSSL::Crypto
        Threads::Threads)

signlang_add_runtime_asset(signlang_eyes_llm_client
        "${CMAKE_CURRENT_SOURCE_DIR}/../../conf/system_prompt.txt"
        "$<TARGET_FILE_DIR:signlang_eyes_llm_client>/conf"
        system_prompt.txt)

install(TARGETS signlang_eyes_llm_client DESTINATION ${CMAKE_INSTALL_BINDIR})
