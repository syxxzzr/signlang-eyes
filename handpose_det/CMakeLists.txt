set(HANDPOSE_DET_MODEL_DIR
        ${SIGNLANG_MODEL_ROOT}/yolov8n-handpose
        CACHE PATH "Directory containing YOLOv8 hand pose RKNN model assets")
set(HANDPOSE_DET_MODEL_FILE ${HANDPOSE_DET_MODEL_DIR}/yolov8_handpose.rknn)
set(HANDPOSE_DET_INSTALL_MODEL_DIR models/yolov8n-handpose)

add_executable(signlang_eyes_edgeai_handpose_det
        handpose_model.cpp
        handpose_preprocessor.cpp
        handpose_transport.cpp
        program_options.cpp
        rknn_runtime.cpp
        main.cpp)

target_include_directories(signlang_eyes_edgeai_handpose_det PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR})

target_link_libraries(signlang_eyes_edgeai_handpose_det PRIVATE
        cxxopts::cxxopts
        iceoryx2-cxx::static-lib-cxx
        signlang::rknn_headers
        dl)

if(SIGNLANG_ENABLE_TESTS)
        add_executable(signlang_eyes_edgeai_handpose_preprocessor_test
                handpose_preprocessor.cpp
                handpose_preprocessor_test.cpp)

        target_include_directories(signlang_eyes_edgeai_handpose_preprocessor_test PRIVATE
                ${CMAKE_CURRENT_SOURCE_DIR}
                ${CMAKE_SOURCE_DIR})

        add_test(NAME signlang_eyes_edgeai_handpose_preprocessor_test
                COMMAND signlang_eyes_edgeai_handpose_preprocessor_test)
endif()

install(TARGETS signlang_eyes_edgeai_handpose_det DESTINATION bin)

if(EXISTS "${HANDPOSE_DET_MODEL_FILE}")
        add_custom_command(TARGET signlang_eyes_edgeai_handpose_det POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_FILE_DIR:signlang_eyes_edgeai_handpose_det>/${HANDPOSE_DET_INSTALL_MODEL_DIR}"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${HANDPOSE_DET_MODEL_FILE}"
                "$<TARGET_FILE_DIR:signlang_eyes_edgeai_handpose_det>/${HANDPOSE_DET_INSTALL_MODEL_DIR}/yolov8_handpose.rknn")
        install(FILES "${HANDPOSE_DET_MODEL_FILE}" DESTINATION ${HANDPOSE_DET_INSTALL_MODEL_DIR})
else()
        message(WARNING "HANDPOSE_DET_MODEL_FILE does not exist: ${HANDPOSE_DET_MODEL_FILE}")
endif()
