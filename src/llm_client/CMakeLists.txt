find_path(SIGNLANG_OPENSSL_INCLUDE_DIR
        NAMES openssl/ssl.h)
find_library(SIGNLANG_SSL_LIBRARY
        NAMES ssl)
find_library(SIGNLANG_CRYPTO_LIBRARY
        NAMES crypto)

if(NOT SIGNLANG_OPENSSL_INCLUDE_DIR OR NOT SIGNLANG_SSL_LIBRARY OR NOT SIGNLANG_CRYPTO_LIBRARY)
    message(FATAL_ERROR "OpenSSL headers/libraries were not found in the target sysroot")
endif()

add_executable(signlang_eyes_llm_client
        iceoryx_gateway.cpp
        openai_client.cpp
        program_options.cpp
        main.cpp)

set_target_properties(signlang_eyes_llm_client PROPERTIES
        OUTPUT_NAME llm_client)

target_include_directories(signlang_eyes_llm_client PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${SIGNLANG_OPENSSL_INCLUDE_DIR})

target_link_libraries(signlang_eyes_llm_client PRIVATE
        cxxopts::cxxopts
        spdlog::spdlog
        iceoryx2_runtime
        signlang::common
        Boost::headers
        boost_json_runtime
        ${SIGNLANG_SSL_LIBRARY}
        ${SIGNLANG_CRYPTO_LIBRARY}
        Threads::Threads)

add_optional_post_build_asset_runtime(signlang_eyes_llm_client
        "${CMAKE_CURRENT_SOURCE_DIR}/../../conf/system_prompt.txt"
        "$<TARGET_FILE_DIR:signlang_eyes_llm_client>/conf"
        system_prompt.txt)

install(TARGETS signlang_eyes_llm_client DESTINATION bin)
