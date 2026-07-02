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

string(TOUPPER "${CMAKE_BUILD_TYPE}" _SPEECH_TTS_CMAKE_BUILD_TYPE)
set(_SPEECH_TTS_CPP_PINYIN_PACKAGE_FOLDER_VAR "cpp-pinyin_PACKAGE_FOLDER_${_SPEECH_TTS_CMAKE_BUILD_TYPE}")
if(DEFINED ${_SPEECH_TTS_CPP_PINYIN_PACKAGE_FOLDER_VAR})
    set(SPEECH_TTS_CPP_PINYIN_PREFIX "${${_SPEECH_TTS_CPP_PINYIN_PACKAGE_FOLDER_VAR}}")
else()
    get_target_property(SPEECH_TTS_CPP_PINYIN_INCLUDE_DIRS cpp-pinyin::cpp-pinyin INTERFACE_INCLUDE_DIRECTORIES)
    list(GET SPEECH_TTS_CPP_PINYIN_INCLUDE_DIRS 0 SPEECH_TTS_CPP_PINYIN_INCLUDE_DIR)
    get_filename_component(SPEECH_TTS_CPP_PINYIN_PREFIX "${SPEECH_TTS_CPP_PINYIN_INCLUDE_DIR}" DIRECTORY)
endif()
unset(_SPEECH_TTS_CPP_PINYIN_PACKAGE_FOLDER_VAR)
unset(_SPEECH_TTS_CMAKE_BUILD_TYPE)
set(_SPEECH_TTS_CPP_PINYIN_DEFAULT_DICT_DIR "${SPEECH_TTS_CPP_PINYIN_PREFIX}/share/cpp-pinyin/dict")
if(NOT DEFINED SPEECH_TTS_CPP_PINYIN_DICT_DIR OR SPEECH_TTS_CPP_PINYIN_DICT_DIR MATCHES "^\\$<")
    set(SPEECH_TTS_CPP_PINYIN_DICT_DIR
            "${_SPEECH_TTS_CPP_PINYIN_DEFAULT_DICT_DIR}"
            CACHE PATH "cpp-pinyin dictionary directory" FORCE)
else()
    set(SPEECH_TTS_CPP_PINYIN_DICT_DIR
            "${SPEECH_TTS_CPP_PINYIN_DICT_DIR}"
            CACHE PATH "cpp-pinyin dictionary directory")
endif()
unset(_SPEECH_TTS_CPP_PINYIN_DEFAULT_DICT_DIR)

if(NOT EXISTS "${SPEECH_TTS_CPP_PINYIN_DICT_DIR}/mandarin")
    message(FATAL_ERROR "Missing cpp-pinyin mandarin dictionary: ${SPEECH_TTS_CPP_PINYIN_DICT_DIR}/mandarin")
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
        SIGNLANG_CPP_PINYIN_DICT_DIR="${SPEECH_TTS_CPP_PINYIN_DICT_DIR}")

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

add_optional_post_build_asset_runtime(signlang_eyes_speech_tts
        "${SPEECH_TTS_ENCODER_MODEL_FILE}"
        "$<TARGET_FILE_DIR:signlang_eyes_speech_tts>/${SPEECH_TTS_INSTALL_MODEL_DIR}"
        zh_CN-huayan-x_low.encoder.onnx)
add_optional_post_build_asset_runtime(signlang_eyes_speech_tts
        "${SPEECH_TTS_DECODER_MODEL_FILE}"
        "$<TARGET_FILE_DIR:signlang_eyes_speech_tts>/${SPEECH_TTS_INSTALL_MODEL_DIR}"
        zh_CN-huayan-x_low.decoder.int8.rknn)
add_optional_post_build_asset_runtime(signlang_eyes_speech_tts
        "${SPEECH_TTS_CONFIG_FILE}"
        "$<TARGET_FILE_DIR:signlang_eyes_speech_tts>/${SPEECH_TTS_INSTALL_MODEL_DIR}"
        zh_CN-huayan-x_low.json)

install(TARGETS signlang_eyes_speech_tts DESTINATION bin)
install_optional_files_runtime(${SPEECH_TTS_INSTALL_MODEL_DIR}
        ${SPEECH_TTS_ENCODER_MODEL_FILE}
        ${SPEECH_TTS_DECODER_MODEL_FILE}
        ${SPEECH_TTS_CONFIG_FILE})
install(DIRECTORY "${SPEECH_TTS_CPP_PINYIN_DICT_DIR}/" DESTINATION share/cpp-pinyin/dict)
