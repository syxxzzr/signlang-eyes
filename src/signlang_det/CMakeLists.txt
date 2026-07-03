set(SIGNLANG_DET_MODEL_FILE ${SIGNLANG_BILSTM_MODEL_FILE})
set(SIGNLANG_DET_PROTOTYPES_FILE ${SIGNLANG_PROTOTYPES_FILE})
set(SIGNLANG_DET_INSTALL_MODEL_DIR models/bilstm)
set(SIGNLANG_DET_INSTALL_CONF_DIR conf)

signlang_require_target(signlang::rknn_runtime "signlang_det requires the RKNN runtime package")

add_executable(signlang_eyes_signlang_det
        keypoint_ring_buffer.cpp
        feature_extractor.cpp
        signlang_model.cpp
        prototype_database.cpp
        gesture_management_service.cpp
        iceoryx_gateway.cpp
        program_options.cpp
        main.cpp)

set_target_properties(signlang_eyes_signlang_det PROPERTIES
        OUTPUT_NAME signlang_det)

target_include_directories(signlang_eyes_signlang_det PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(signlang_eyes_signlang_det PRIVATE
        cxxopts::cxxopts
        SQLiteCpp
        spdlog::spdlog
        iceoryx2_runtime
        signlang::common
        signlang::rknn_runtime
        Threads::Threads
        m)

signlang_add_runtime_asset(signlang_eyes_signlang_det
        "${SIGNLANG_DET_MODEL_FILE}"
        "$<TARGET_FILE_DIR:signlang_eyes_signlang_det>/${SIGNLANG_DET_INSTALL_MODEL_DIR}"
        biltsm.rknn)
signlang_add_runtime_asset(signlang_eyes_signlang_det
        "${SIGNLANG_DET_PROTOTYPES_FILE}"
        "$<TARGET_FILE_DIR:signlang_eyes_signlang_det>/${SIGNLANG_DET_INSTALL_CONF_DIR}"
        prototypes.sqlite)

install(TARGETS signlang_eyes_signlang_det DESTINATION ${CMAKE_INSTALL_BINDIR})
