set(SIGNLANG_HANDPOSE_MODEL_PATH
        "/mnt/f/temp/yolov8n-handpose/yolov8_handpose.rknn"
        CACHE FILEPATH "Path to the external RKNN handpose model used for install packaging")

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

install(TARGETS signlang_eyes_edgeai_handpose_det DESTINATION .)

if(EXISTS "${SIGNLANG_HANDPOSE_MODEL_PATH}")
        install(FILES "${SIGNLANG_HANDPOSE_MODEL_PATH}" DESTINATION model)
else()
        message(WARNING "SIGNLANG_HANDPOSE_MODEL_PATH does not exist: ${SIGNLANG_HANDPOSE_MODEL_PATH}")
endif()
