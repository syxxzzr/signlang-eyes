set(HANDPOSE_DET_MODEL_DIR
        ${SIGNLANG_MODEL_ROOT}/mediapipe
        CACHE PATH "Directory containing MediaPipe hand pose RKNN model assets")
set(HANDPOSE_DET_PALM_MODEL_FILE ${HANDPOSE_DET_MODEL_DIR}/hand_detector.rknn)
set(HANDPOSE_DET_LANDMARK_MODEL_FILE ${HANDPOSE_DET_MODEL_DIR}/hand_landmarks_detector.rknn)
set(HANDPOSE_DET_INSTALL_MODEL_DIR models/mediapipe)

add_executable(signlang_eyes_handpose_det
        handpose_model.cpp
        handpose_selection.cpp
        handpose_transport.cpp
        program_options.cpp
        main.cpp)

set_target_properties(signlang_eyes_handpose_det PROPERTIES
        OUTPUT_NAME handpose_det)

target_include_directories(signlang_eyes_handpose_det PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(signlang_eyes_handpose_det PRIVATE
        cxxopts::cxxopts
        spdlog::spdlog
        iceoryx2_runtime
        signlang::common
        signlang::rknn_runtime
        librga::rga)

install(TARGETS signlang_eyes_handpose_det DESTINATION ${CMAKE_INSTALL_BINDIR})

signlang_add_runtime_asset(signlang_eyes_handpose_det
        "${HANDPOSE_DET_PALM_MODEL_FILE}"
        "$<TARGET_FILE_DIR:signlang_eyes_handpose_det>/${HANDPOSE_DET_INSTALL_MODEL_DIR}"
        hand_detector.rknn)
signlang_add_runtime_asset(signlang_eyes_handpose_det
        "${HANDPOSE_DET_LANDMARK_MODEL_FILE}"
        "$<TARGET_FILE_DIR:signlang_eyes_handpose_det>/${HANDPOSE_DET_INSTALL_MODEL_DIR}"
        hand_landmarks_detector.rknn)
signlang_install_runtime_files(${HANDPOSE_DET_INSTALL_MODEL_DIR}
        "${HANDPOSE_DET_PALM_MODEL_FILE}"
        "${HANDPOSE_DET_LANDMARK_MODEL_FILE}")
