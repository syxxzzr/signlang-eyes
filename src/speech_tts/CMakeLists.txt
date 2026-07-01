set(SPEECH_TTS_MODEL_DIR
        ${SIGNLANG_MODEL_ROOT}/piper
        CACHE PATH "Directory containing Piper TTS model assets")
set(SPEECH_TTS_ENCODER_MODEL_FILE
        ${SPEECH_TTS_MODEL_DIR}/zh_CN-huayan-x_low.encoder.onnx
        CACHE FILEPATH "Piper encoder ONNX model")
set(SPEECH_TTS_DECODER_MODEL_FILE
        ${SPEECH_TTS_MODEL_DIR}/zh_CN-huayan-x_low.decoder.int8.rknn
        CACHE FILEPATH "Piper decoder RKNN model")
set(SPEECH_TTS_CONFIG_FILE
        ${SPEECH_TTS_MODEL_DIR}/zh_CN-huayan-x_low.json
        CACHE FILEPATH "Piper voice JSON config")
set(SPEECH_TTS_INSTALL_MODEL_DIR models/piper)

foreach(REQUIRED_TTS_ASSET
        ${SPEECH_TTS_ENCODER_MODEL_FILE}
        ${SPEECH_TTS_DECODER_MODEL_FILE}
        ${SPEECH_TTS_CONFIG_FILE})
    if(NOT EXISTS ${REQUIRED_TTS_ASSET})
        message(FATAL_ERROR "Missing required speech TTS asset: ${REQUIRED_TTS_ASSET}")
    endif()
endforeach()

get_target_property(SPEECH_TTS_CPP_PINYIN_INCLUDE_DIRS cpp-pinyin::cpp-pinyin INTERFACE_INCLUDE_DIRECTORIES)
list(GET SPEECH_TTS_CPP_PINYIN_INCLUDE_DIRS 0 SPEECH_TTS_CPP_PINYIN_INCLUDE_DIR)
get_filename_component(SPEECH_TTS_CPP_PINYIN_PREFIX "${SPEECH_TTS_CPP_PINYIN_INCLUDE_DIR}" DIRECTORY)
set(SPEECH_TTS_CPP_PINYIN_DICT_DIR
        "${SPEECH_TTS_CPP_PINYIN_PREFIX}/share/cpp-pinyin/dict"
        CACHE PATH "cpp-pinyin dictionary directory")

if(NOT EXISTS ${SPEECH_TTS_CPP_PINYIN_DICT_DIR}/mandarin)
    message(FATAL_ERROR "Missing cpp-pinyin mandarin dictionary: ${SPEECH_TTS_CPP_PINYIN_DICT_DIR}/mandarin")
endif()

if(TARGET onnxruntime::onnxruntime)
    set(SPEECH_TTS_ONNXRUNTIME_TARGET onnxruntime::onnxruntime)
elseif(TARGET onnxruntime::onnxruntime_lib)
    set(SPEECH_TTS_ONNXRUNTIME_TARGET onnxruntime::onnxruntime_lib)
elseif(TARGET onnxruntime)
    set(SPEECH_TTS_ONNXRUNTIME_TARGET onnxruntime)
else()
    message(FATAL_ERROR "onnxruntime package was found, but no known CMake target is available")
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
        iceoryx2-cxx::shared-lib-cxx
        cpp-pinyin::cpp-pinyin
        ${SPEECH_TTS_ONNXRUNTIME_TARGET}
        signlang::rknn_runtime
        Boost::json
        ALSA::ALSA
        Threads::Threads
        m)

add_custom_command(TARGET signlang_eyes_speech_tts POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:signlang_eyes_speech_tts>/${SPEECH_TTS_INSTALL_MODEL_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${SPEECH_TTS_ENCODER_MODEL_FILE}
        "$<TARGET_FILE_DIR:signlang_eyes_speech_tts>/${SPEECH_TTS_INSTALL_MODEL_DIR}/zh_CN-huayan-x_low.encoder.onnx"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${SPEECH_TTS_DECODER_MODEL_FILE}
        "$<TARGET_FILE_DIR:signlang_eyes_speech_tts>/${SPEECH_TTS_INSTALL_MODEL_DIR}/zh_CN-huayan-x_low.decoder.int8.rknn"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${SPEECH_TTS_CONFIG_FILE}
        "$<TARGET_FILE_DIR:signlang_eyes_speech_tts>/${SPEECH_TTS_INSTALL_MODEL_DIR}/zh_CN-huayan-x_low.json")

install(TARGETS signlang_eyes_speech_tts DESTINATION bin)
install(FILES
        ${SPEECH_TTS_ENCODER_MODEL_FILE}
        ${SPEECH_TTS_DECODER_MODEL_FILE}
        ${SPEECH_TTS_CONFIG_FILE}
        DESTINATION ${SPEECH_TTS_INSTALL_MODEL_DIR})
install(DIRECTORY ${SPEECH_TTS_CPP_PINYIN_DICT_DIR}/ DESTINATION share/cpp-pinyin/dict)
