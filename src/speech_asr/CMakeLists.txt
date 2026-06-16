set(SPEECH_ASR_MODEL_DIR
        ${SIGNLANG_MODEL_ROOT}/whisper
        CACHE PATH "Directory containing Whisper RKNN model assets")
set(SPEECH_ASR_ENCODER_MODEL_FILE ${SPEECH_ASR_MODEL_DIR}/whisper_encoder_base_15s.rknn)
set(SPEECH_ASR_DECODER_MODEL_FILE ${SPEECH_ASR_MODEL_DIR}/whisper_decoder_base_15s.rknn)
set(SPEECH_ASR_MEL_FILTERS_FILE ${SPEECH_ASR_MODEL_DIR}/mel_80_filters.txt)
set(SPEECH_ASR_VOCAB_EN_FILE ${SPEECH_ASR_MODEL_DIR}/vocab_en.txt)
set(SPEECH_ASR_VOCAB_ZH_FILE ${SPEECH_ASR_MODEL_DIR}/vocab_zh.txt)
set(SPEECH_ASR_INSTALL_MODEL_DIR models/whisper)

if(NOT TARGET signlang::rknn_runtime)
    message(STATUS "Skipping signlang_eyes_edgeai_speech_asr: RKNN runtime target not available")
    return()
endif()

foreach(required_file
        ${SPEECH_ASR_ENCODER_MODEL_FILE}
        ${SPEECH_ASR_DECODER_MODEL_FILE}
        ${SPEECH_ASR_MEL_FILTERS_FILE}
        ${SPEECH_ASR_VOCAB_EN_FILE}
        ${SPEECH_ASR_VOCAB_ZH_FILE})
    if(NOT EXISTS ${required_file})
        message(FATAL_ERROR "Missing required speech ASR asset: ${required_file}")
    endif()
endforeach()

if(NOT TARGET fftw3f)
    message(FATAL_ERROR "Missing FFTW target; configure FFTW from the root CMakeLists.txt")
endif()

add_executable(signlang_eyes_edgeai_speech_asr
        audio_ring_buffer.cpp
        iceoryx_gateway.cpp
        program_options.cpp
        speech_asr_result.cpp
        whisper_model.cpp
        main.cpp)

target_include_directories(signlang_eyes_edgeai_speech_asr PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(signlang_eyes_edgeai_speech_asr PRIVATE
        cxxopts::cxxopts
        iceoryx2-cxx::shared-lib-cxx
        signlang::rknn_runtime
        fftw3f
        Threads::Threads
        m
        dl)

add_custom_command(TARGET signlang_eyes_edgeai_speech_asr POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:signlang_eyes_edgeai_speech_asr>/${SPEECH_ASR_INSTALL_MODEL_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${SPEECH_ASR_ENCODER_MODEL_FILE}
        "$<TARGET_FILE_DIR:signlang_eyes_edgeai_speech_asr>/${SPEECH_ASR_INSTALL_MODEL_DIR}/whisper_encoder_base_15s.rknn"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${SPEECH_ASR_DECODER_MODEL_FILE}
        "$<TARGET_FILE_DIR:signlang_eyes_edgeai_speech_asr>/${SPEECH_ASR_INSTALL_MODEL_DIR}/whisper_decoder_base_15s.rknn"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${SPEECH_ASR_MEL_FILTERS_FILE}
        "$<TARGET_FILE_DIR:signlang_eyes_edgeai_speech_asr>/${SPEECH_ASR_INSTALL_MODEL_DIR}/mel_80_filters.txt"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${SPEECH_ASR_VOCAB_EN_FILE}
        "$<TARGET_FILE_DIR:signlang_eyes_edgeai_speech_asr>/${SPEECH_ASR_INSTALL_MODEL_DIR}/vocab_en.txt"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${SPEECH_ASR_VOCAB_ZH_FILE}
        "$<TARGET_FILE_DIR:signlang_eyes_edgeai_speech_asr>/${SPEECH_ASR_INSTALL_MODEL_DIR}/vocab_zh.txt")

install(TARGETS signlang_eyes_edgeai_speech_asr DESTINATION bin)
install(FILES
        ${SPEECH_ASR_ENCODER_MODEL_FILE}
        ${SPEECH_ASR_DECODER_MODEL_FILE}
        ${SPEECH_ASR_MEL_FILTERS_FILE}
        ${SPEECH_ASR_VOCAB_EN_FILE}
        ${SPEECH_ASR_VOCAB_ZH_FILE}
        DESTINATION ${SPEECH_ASR_INSTALL_MODEL_DIR})
