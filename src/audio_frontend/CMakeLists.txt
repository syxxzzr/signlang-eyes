add_executable(signlang_eyes_edgeai_audio_frontend
        alsa_capture_device.cpp
        audio_processor.cpp
        audio_publisher.cpp
        program_options.cpp
        main.cpp)

target_include_directories(signlang_eyes_edgeai_audio_frontend PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(signlang_eyes_edgeai_audio_frontend PRIVATE
        cxxopts::cxxopts
        iceoryx2-cxx::shared-lib-cxx
        ALSA::ALSA
        fftw3f
        m)

install(TARGETS signlang_eyes_edgeai_audio_frontend DESTINATION bin)

if(SIGNLANG_ENABLE_TESTS)
        add_executable(signlang_eyes_edgeai_audio_processor_test
                audio_processor.cpp
                audio_processor_test.cpp)

        target_include_directories(signlang_eyes_edgeai_audio_processor_test PRIVATE
                ${CMAKE_CURRENT_SOURCE_DIR})

        target_link_libraries(signlang_eyes_edgeai_audio_processor_test PRIVATE
                fftw3f
                m)

        add_test(NAME signlang_eyes_edgeai_audio_processor_test
                COMMAND signlang_eyes_edgeai_audio_processor_test)
endif()
