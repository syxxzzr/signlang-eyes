find_package(OpenSSL REQUIRED)

add_executable(signlang_eyes_llm_client
        iceoryx_gateway.cpp
        openai_client.cpp
        program_options.cpp
        main.cpp)

set_target_properties(signlang_eyes_llm_client PROPERTIES
        OUTPUT_NAME llm_client)

target_include_directories(signlang_eyes_llm_client PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR})

target_compile_definitions(signlang_eyes_llm_client PRIVATE
        BOOST_JSON_HEADER_ONLY)

target_link_libraries(signlang_eyes_llm_client PRIVATE
        cxxopts::cxxopts
        spdlog::spdlog
        iceoryx2-cxx::shared-lib-cxx
        signlang::common
        Boost::headers
        OpenSSL::SSL
        OpenSSL::Crypto
        Threads::Threads)

add_custom_command(TARGET signlang_eyes_llm_client POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:signlang_eyes_llm_client>/conf"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/../../conf/system_prompt.txt
        "$<TARGET_FILE_DIR:signlang_eyes_llm_client>/conf/system_prompt.txt")

install(TARGETS signlang_eyes_llm_client DESTINATION bin)
