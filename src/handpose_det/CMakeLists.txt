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
        iceoryx2-cxx::shared-lib-cxx
        signlang::rknn_runtime
        librga::rga)

install(TARGETS signlang_eyes_handpose_det DESTINATION bin)

if(EXISTS "${HANDPOSE_DET_PALM_MODEL_FILE}" AND EXISTS "${HANDPOSE_DET_LANDMARK_MODEL_FILE}")
        add_custom_command(TARGET signlang_eyes_handpose_det POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_FILE_DIR:signlang_eyes_handpose_det>/${HANDPOSE_DET_INSTALL_MODEL_DIR}"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${HANDPOSE_DET_PALM_MODEL_FILE}"
                "$<TARGET_FILE_DIR:signlang_eyes_handpose_det>/${HANDPOSE_DET_INSTALL_MODEL_DIR}/hand_detector.rknn"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${HANDPOSE_DET_LANDMARK_MODEL_FILE}"
                "$<TARGET_FILE_DIR:signlang_eyes_handpose_det>/${HANDPOSE_DET_INSTALL_MODEL_DIR}/hand_landmarks_detector.rknn")
        install(FILES "${HANDPOSE_DET_PALM_MODEL_FILE}" "${HANDPOSE_DET_LANDMARK_MODEL_FILE}"
                DESTINATION ${HANDPOSE_DET_INSTALL_MODEL_DIR})
else()
        message(WARNING "MediaPipe handpose model files do not exist in ${HANDPOSE_DET_MODEL_DIR}")
endif()
