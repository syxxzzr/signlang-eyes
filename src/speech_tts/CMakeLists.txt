set(SPEECH_TTS_MODEL_DIR
        ${SIGNLANG_MODEL_ROOT}/piper
        CACHE PATH "Directory containing Piper TTS model assets")
set(SPEECH_TTS_ENCODER_MODEL_FILE
        ${SPEECH_TTS_MODEL_DIR}/zh_CN-huayan-x_low.encoder.onnx
        CACHE FILEPATH "Piper encoder ONNX model")
set(SPEECH_TTS_DECODER_MODEL_FILE
        ${SPEECH_TTS_MODEL_DIR}/zh_CN-huayan-x_low.decoder.rknn
        CACHE FILEPATH "Piper decoder RKNN model")
set(SPEECH_TTS_CONFIG_FILE
        ${SPEECH_TTS_MODEL_DIR}/zh_CN-huayan-x_low.json
        CACHE FILEPATH "Piper voice JSON config")
set(SPEECH_TTS_INSTALL_MODEL_DIR models/piper)

signlang_get_conan_package_folder(cpp-pinyin SPEECH_TTS_CPP_PINYIN_PACKAGE_FOLDER)
set(_SPEECH_TTS_CPP_PINYIN_PACKAGE_DICT_DIR "${SPEECH_TTS_CPP_PINYIN_PACKAGE_FOLDER}/share/cpp-pinyin/dict")
if(NOT DEFINED SPEECH_TTS_CPP_PINYIN_SOURCE_DICT_DIR OR SPEECH_TTS_CPP_PINYIN_SOURCE_DICT_DIR MATCHES "^\\$<")
    set(SPEECH_TTS_CPP_PINYIN_SOURCE_DICT_DIR
            "${_SPEECH_TTS_CPP_PINYIN_PACKAGE_DICT_DIR}"
            CACHE PATH "Source cpp-pinyin dictionary directory copied into the runtime tree" FORCE)
else()
    set(SPEECH_TTS_CPP_PINYIN_SOURCE_DICT_DIR
            "${SPEECH_TTS_CPP_PINYIN_SOURCE_DICT_DIR}"
            CACHE PATH "Source cpp-pinyin dictionary directory copied into the runtime tree")
endif()
unset(_SPEECH_TTS_CPP_PINYIN_PACKAGE_DICT_DIR)

set(SPEECH_TTS_CPP_PINYIN_RUNTIME_DICT_DIR
        share/cpp-pinyin/dict
        CACHE STRING "Runtime cpp-pinyin dictionary directory relative to the installation root")

if(NOT EXISTS "${SPEECH_TTS_CPP_PINYIN_SOURCE_DICT_DIR}/mandarin")
    message(FATAL_ERROR "Missing cpp-pinyin mandarin dictionary: ${SPEECH_TTS_CPP_PINYIN_SOURCE_DICT_DIR}/mandarin")
endif()

add_library(signlang_speech_tts_core STATIC
        speech_tts_service.cpp)
target_include_directories(signlang_speech_tts_core PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(signlang_speech_tts_core PUBLIC
        signlang::common
        Threads::Threads)

add_executable(signlang_eyes_speech_tts
        alsa_playback_device.cpp
        iceoryx_gateway.cpp
        piper_phonemizer.cpp
        piper_synthesizer.cpp
        program_options.cpp
        main.cpp)

set_target_properties(signlang_eyes_speech_tts PROPERTIES
        OUTPUT_NAME speech_tts)

target_include_directories(signlang_eyes_speech_tts PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(signlang_eyes_speech_tts PRIVATE
        SIGNLANG_CPP_PINYIN_DICT_DIR="${SPEECH_TTS_CPP_PINYIN_RUNTIME_DICT_DIR}")

target_link_libraries(signlang_eyes_speech_tts PRIVATE
        signlang_speech_tts_core
        cxxopts::cxxopts
        spdlog::spdlog
        iceoryx2_runtime
        cpp-pinyin::cpp-pinyin
        onnxruntime_runtime
        signlang::rknn_runtime
        boost_json_runtime
        ALSA::ALSA
        Threads::Threads
        m)

signlang_add_runtime_asset(signlang_eyes_speech_tts
        "${SPEECH_TTS_ENCODER_MODEL_FILE}"
        "$<TARGET_FILE_DIR:signlang_eyes_speech_tts>/${SPEECH_TTS_INSTALL_MODEL_DIR}"
        zh_CN-huayan-x_low.encoder.onnx)
signlang_add_runtime_asset(signlang_eyes_speech_tts
        "${SPEECH_TTS_DECODER_MODEL_FILE}"
        "$<TARGET_FILE_DIR:signlang_eyes_speech_tts>/${SPEECH_TTS_INSTALL_MODEL_DIR}"
        zh_CN-huayan-x_low.decoder.rknn)
signlang_add_runtime_asset(signlang_eyes_speech_tts
        "${SPEECH_TTS_CONFIG_FILE}"
        "$<TARGET_FILE_DIR:signlang_eyes_speech_tts>/${SPEECH_TTS_INSTALL_MODEL_DIR}"
        zh_CN-huayan-x_low.json)
add_custom_command(TARGET signlang_eyes_speech_tts POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:signlang_eyes_speech_tts>/${SPEECH_TTS_CPP_PINYIN_RUNTIME_DICT_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${SPEECH_TTS_CPP_PINYIN_SOURCE_DICT_DIR}"
        "$<TARGET_FILE_DIR:signlang_eyes_speech_tts>/${SPEECH_TTS_CPP_PINYIN_RUNTIME_DICT_DIR}")

install(TARGETS signlang_eyes_speech_tts DESTINATION ${CMAKE_INSTALL_BINDIR})
signlang_install_runtime_files(${SPEECH_TTS_INSTALL_MODEL_DIR}
        ${SPEECH_TTS_ENCODER_MODEL_FILE}
        ${SPEECH_TTS_DECODER_MODEL_FILE}
        ${SPEECH_TTS_CONFIG_FILE})
install(DIRECTORY "${SPEECH_TTS_CPP_PINYIN_SOURCE_DICT_DIR}/" DESTINATION share/cpp-pinyin/dict)
