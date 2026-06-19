add_executable(signlang_eyes_audio_frontend
        alsa_capture_device.cpp
        audio_processor.cpp
        audio_publisher.cpp
        sound_source_blackboard.cpp
        sound_source_localization.cpp
        program_options.cpp
        main.cpp)

set_target_properties(signlang_eyes_audio_frontend PROPERTIES
        OUTPUT_NAME audio_frontend)

target_include_directories(signlang_eyes_audio_frontend PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(signlang_eyes_audio_frontend PRIVATE
        cxxopts::cxxopts
        iceoryx2-cxx::shared-lib-cxx
        ALSA::ALSA
        fftw3f
        m)

install(TARGETS signlang_eyes_audio_frontend DESTINATION bin)

if(SIGNLANG_ENABLE_TESTS)
        add_executable(signlang_eyes_audio_frontend_record_test
                audio_record_test.cpp)

        set_target_properties(signlang_eyes_audio_frontend_record_test PROPERTIES
                OUTPUT_NAME audio_frontend_record_test)

        target_include_directories(signlang_eyes_audio_frontend_record_test PRIVATE
                ${CMAKE_CURRENT_SOURCE_DIR})

        target_link_libraries(signlang_eyes_audio_frontend_record_test PRIVATE
                cxxopts::cxxopts
                iceoryx2-cxx::shared-lib-cxx)
endif()
