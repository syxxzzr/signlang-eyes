set(ENV_SOUND_DET_MODEL_DIR
        ${SIGNLANG_MODEL_ROOT}/yamnet
        CACHE PATH "Directory containing YAMNet RKNN model assets")
set(ENV_SOUND_DET_MODEL_FILE ${ENV_SOUND_DET_MODEL_DIR}/yamnet_3s.rknn)
set(ENV_SOUND_DET_CLASS_MAP_FILE ${ENV_SOUND_DET_MODEL_DIR}/yamnet_class_map.txt)
set(ENV_SOUND_DET_INSTALL_MODEL_DIR models/yamnet)

signlang_require_target(signlang::rknn_runtime "env_sound_det requires the RKNN runtime package")

add_executable(signlang_eyes_env_sound_det
        iceoryx_gateway.cpp
        program_options.cpp
        yamnet_model.cpp
        main.cpp)

set_target_properties(signlang_eyes_env_sound_det PROPERTIES
        OUTPUT_NAME env_sound_det)

target_include_directories(signlang_eyes_env_sound_det PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(signlang_eyes_env_sound_det PRIVATE
        cxxopts::cxxopts
        spdlog::spdlog
        iceoryx2_runtime
        signlang::rknn_runtime
        signlang::common
        Threads::Threads)

signlang_add_runtime_asset(signlang_eyes_env_sound_det
        "${ENV_SOUND_DET_MODEL_FILE}"
        "$<TARGET_FILE_DIR:signlang_eyes_env_sound_det>/${ENV_SOUND_DET_INSTALL_MODEL_DIR}"
        yamnet_3s.rknn)
signlang_add_runtime_asset(signlang_eyes_env_sound_det
        "${ENV_SOUND_DET_CLASS_MAP_FILE}"
        "$<TARGET_FILE_DIR:signlang_eyes_env_sound_det>/${ENV_SOUND_DET_INSTALL_MODEL_DIR}"
        yamnet_class_map.txt)

install(TARGETS signlang_eyes_env_sound_det DESTINATION ${CMAKE_INSTALL_BINDIR})
signlang_install_runtime_files(${ENV_SOUND_DET_INSTALL_MODEL_DIR}
        ${ENV_SOUND_DET_MODEL_FILE}
        ${ENV_SOUND_DET_CLASS_MAP_FILE})
