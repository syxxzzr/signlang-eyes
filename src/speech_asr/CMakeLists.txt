set(SPEECH_ASR_MODEL_DIR
        ${SIGNLANG_MODEL_ROOT}/whisper
        CACHE PATH "Directory containing Whisper RKNN model assets")
set(SPEECH_ASR_ENCODER_MODEL_FILE ${SPEECH_ASR_MODEL_DIR}/whisper_encoder_base_15s.onnx)
set(SPEECH_ASR_DECODER_MODEL_FILE ${SPEECH_ASR_MODEL_DIR}/whisper_decoder_base_15s.rknn)
set(SPEECH_ASR_MEL_FILTERS_FILE ${SPEECH_ASR_MODEL_DIR}/mel_80_filters.txt)
set(SPEECH_ASR_VOCAB_EN_FILE ${SPEECH_ASR_MODEL_DIR}/vocab_en.txt)
set(SPEECH_ASR_VOCAB_ZH_FILE ${SPEECH_ASR_MODEL_DIR}/vocab_zh.txt)
set(SPEECH_ASR_INSTALL_MODEL_DIR models/whisper)

signlang_require_target(signlang::rknn_runtime "speech_asr requires the RKNN runtime package")

if(NOT TARGET FFTW3::fftw3f)
    message(FATAL_ERROR "Missing FFTW target; configure FFTW from the root CMakeLists.txt")
endif()

add_executable(signlang_eyes_speech_asr
        iceoryx_gateway.cpp
        program_options.cpp
        speech_asr_result.cpp
        whisper_model.cpp
        main.cpp)

set_target_properties(signlang_eyes_speech_asr PROPERTIES
        OUTPUT_NAME speech_asr)

target_include_directories(signlang_eyes_speech_asr PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(signlang_eyes_speech_asr PRIVATE
        cxxopts::cxxopts
        spdlog::spdlog
        iceoryx2_runtime
        onnxruntime_runtime
        signlang::rknn_runtime
        signlang::common
        FFTW3::fftw3f
        Threads::Threads
        m)

signlang_add_runtime_asset(signlang_eyes_speech_asr
        "${SPEECH_ASR_ENCODER_MODEL_FILE}"
        "$<TARGET_FILE_DIR:signlang_eyes_speech_asr>/${SPEECH_ASR_INSTALL_MODEL_DIR}"
        whisper_encoder_base_15s.onnx)
signlang_add_runtime_asset(signlang_eyes_speech_asr
        "${SPEECH_ASR_DECODER_MODEL_FILE}"
        "$<TARGET_FILE_DIR:signlang_eyes_speech_asr>/${SPEECH_ASR_INSTALL_MODEL_DIR}"
        whisper_decoder_base_15s.rknn)
signlang_add_runtime_asset(signlang_eyes_speech_asr
        "${SPEECH_ASR_MEL_FILTERS_FILE}"
        "$<TARGET_FILE_DIR:signlang_eyes_speech_asr>/${SPEECH_ASR_INSTALL_MODEL_DIR}"
        mel_80_filters.txt)
signlang_add_runtime_asset(signlang_eyes_speech_asr
        "${SPEECH_ASR_VOCAB_EN_FILE}"
        "$<TARGET_FILE_DIR:signlang_eyes_speech_asr>/${SPEECH_ASR_INSTALL_MODEL_DIR}"
        vocab_en.txt)
signlang_add_runtime_asset(signlang_eyes_speech_asr
        "${SPEECH_ASR_VOCAB_ZH_FILE}"
        "$<TARGET_FILE_DIR:signlang_eyes_speech_asr>/${SPEECH_ASR_INSTALL_MODEL_DIR}"
        vocab_zh.txt)

install(TARGETS signlang_eyes_speech_asr DESTINATION ${CMAKE_INSTALL_BINDIR})
signlang_install_runtime_files(${SPEECH_ASR_INSTALL_MODEL_DIR}
        ${SPEECH_ASR_ENCODER_MODEL_FILE}
        ${SPEECH_ASR_DECODER_MODEL_FILE}
        ${SPEECH_ASR_MEL_FILTERS_FILE}
        ${SPEECH_ASR_VOCAB_EN_FILE}
        ${SPEECH_ASR_VOCAB_ZH_FILE})
