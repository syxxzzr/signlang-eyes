cmake_minimum_required(VERSION 3.20)

project(signlang_eyes_edgeai_speech_asr LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(SPEECH_ASR_RUNTIME_ROOT ${CMAKE_SOURCE_DIR}/third_party/rknn_runtime)
set(SPEECH_ASR_RKNN_INCLUDE_DIR ${SPEECH_ASR_RUNTIME_ROOT}/include)
set(SPEECH_ASR_RKNN_LIBRARY ${SPEECH_ASR_RUNTIME_ROOT}/lib/aarch64/librknnrt.so)
set(SPEECH_ASR_FFTW_ROOT ${CMAKE_SOURCE_DIR}/third_party/fftw)
set(SPEECH_ASR_FFTW_INCLUDE_DIR ${SPEECH_ASR_FFTW_ROOT}/include)
set(SPEECH_ASR_FFTW_LIBRARY ${SPEECH_ASR_FFTW_ROOT}/lib/aarch64/libfftw3f.a)
set(SPEECH_ASR_MODEL_DIR ${CMAKE_CURRENT_SOURCE_DIR}/model)
set(SPEECH_ASR_ENCODER_MODEL_FILE ${SPEECH_ASR_MODEL_DIR}/whisper_encoder_base_15s.rknn)
set(SPEECH_ASR_DECODER_MODEL_FILE ${SPEECH_ASR_MODEL_DIR}/whisper_decoder_base_15s.rknn)
set(SPEECH_ASR_MEL_FILTERS_FILE ${SPEECH_ASR_MODEL_DIR}/mel_80_filters.txt)
set(SPEECH_ASR_VOCAB_EN_FILE ${SPEECH_ASR_MODEL_DIR}/vocab_en.txt)
set(SPEECH_ASR_VOCAB_ZH_FILE ${SPEECH_ASR_MODEL_DIR}/vocab_zh.txt)

if(NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)$")
    message(STATUS "Skipping signlang_eyes_edgeai_speech_asr: RKNN runtime is project-local aarch64 only")
    return()
endif()

foreach(required_file
        ${SPEECH_ASR_RKNN_INCLUDE_DIR}/rknn_api.h
        ${SPEECH_ASR_RKNN_LIBRARY}
        ${SPEECH_ASR_FFTW_INCLUDE_DIR}/fftw3.h
        ${SPEECH_ASR_FFTW_LIBRARY}
        ${SPEECH_ASR_ENCODER_MODEL_FILE}
        ${SPEECH_ASR_DECODER_MODEL_FILE}
        ${SPEECH_ASR_MEL_FILTERS_FILE}
        ${SPEECH_ASR_VOCAB_EN_FILE}
        ${SPEECH_ASR_VOCAB_ZH_FILE})
    if(NOT EXISTS ${required_file})
        message(FATAL_ERROR "Missing required speech ASR asset: ${required_file}")
    endif()
endforeach()

add_library(rknnrt SHARED IMPORTED GLOBAL)
set_target_properties(rknnrt PROPERTIES
        IMPORTED_LOCATION ${SPEECH_ASR_RKNN_LIBRARY}
        INTERFACE_INCLUDE_DIRECTORIES ${SPEECH_ASR_RKNN_INCLUDE_DIR})

add_library(fftw3f STATIC IMPORTED GLOBAL)
set_target_properties(fftw3f PROPERTIES
        IMPORTED_LOCATION ${SPEECH_ASR_FFTW_LIBRARY}
        INTERFACE_INCLUDE_DIRECTORIES ${SPEECH_ASR_FFTW_INCLUDE_DIR})

find_package(Threads REQUIRED)

add_executable(signlang_eyes_edgeai_speech_asr
        audio_ring_buffer.cpp
        iceoryx_gateway.cpp
        program_options.cpp
        speech_asr_result.cpp
        whisper_model.cpp
        main.cpp)

target_include_directories(signlang_eyes_edgeai_speech_asr PRIVATE
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${SPEECH_ASR_RKNN_INCLUDE_DIR}
        ${SPEECH_ASR_FFTW_INCLUDE_DIR})

target_link_libraries(signlang_eyes_edgeai_speech_asr PRIVATE
        cxxopts::cxxopts
        iceoryx2-cxx::static-lib-cxx
        rknnrt
        fftw3f
        Threads::Threads
        dl)

set_target_properties(signlang_eyes_edgeai_speech_asr PROPERTIES
        BUILD_RPATH ${SPEECH_ASR_RUNTIME_ROOT}/lib/aarch64
        INSTALL_RPATH "$ORIGIN/lib")

add_custom_command(TARGET signlang_eyes_edgeai_speech_asr POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:signlang_eyes_edgeai_speech_asr>/model"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${SPEECH_ASR_ENCODER_MODEL_FILE}
        "$<TARGET_FILE_DIR:signlang_eyes_edgeai_speech_asr>/model/whisper_encoder_base_15s.rknn"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${SPEECH_ASR_DECODER_MODEL_FILE}
        "$<TARGET_FILE_DIR:signlang_eyes_edgeai_speech_asr>/model/whisper_decoder_base_15s.rknn"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${SPEECH_ASR_MEL_FILTERS_FILE}
        "$<TARGET_FILE_DIR:signlang_eyes_edgeai_speech_asr>/model/mel_80_filters.txt"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${SPEECH_ASR_VOCAB_EN_FILE}
        "$<TARGET_FILE_DIR:signlang_eyes_edgeai_speech_asr>/model/vocab_en.txt"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${SPEECH_ASR_VOCAB_ZH_FILE}
        "$<TARGET_FILE_DIR:signlang_eyes_edgeai_speech_asr>/model/vocab_zh.txt")

install(TARGETS signlang_eyes_edgeai_speech_asr DESTINATION .)
install(FILES ${SPEECH_ASR_RKNN_LIBRARY} DESTINATION lib)
install(FILES
        ${SPEECH_ASR_ENCODER_MODEL_FILE}
        ${SPEECH_ASR_DECODER_MODEL_FILE}
        ${SPEECH_ASR_MEL_FILTERS_FILE}
        ${SPEECH_ASR_VOCAB_EN_FILE}
        ${SPEECH_ASR_VOCAB_ZH_FILE}
        DESTINATION model)
