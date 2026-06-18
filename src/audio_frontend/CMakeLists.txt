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
