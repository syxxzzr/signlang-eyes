set(SIGNLANG_DET_MODEL_DIR
        ${SIGNLANG_MODEL_ROOT}/signlang
        CACHE PATH "Directory containing sign language LSTM RKNN model assets")
set(SIGNLANG_DET_MODEL_FILE ${SIGNLANG_DET_MODEL_DIR}/signlang.rknn)
set(SIGNLANG_DET_LABEL_MAP_FILE ${SIGNLANG_DET_MODEL_DIR}/labels.txt)
set(SIGNLANG_DET_INSTALL_MODEL_DIR models/signlang)

if(NOT TARGET signlang::rknn_runtime)
    message(STATUS "Skipping signlang_eyes_signlang_det: RKNN runtime target not available")
    return()
endif()

foreach(required_file
        ${SIGNLANG_DET_MODEL_FILE}
        ${SIGNLANG_DET_LABEL_MAP_FILE})
    if(NOT EXISTS ${required_file})
        message(WARNING "Missing signlang_det asset (will skip): ${required_file}")
        return()
    endif()
endforeach()

add_executable(signlang_eyes_signlang_det
        signlang_result.cpp
        keypoint_ring_buffer.cpp
        feature_extractor.cpp
        signlang_model.cpp
        iceoryx_gateway.cpp
        program_options.cpp
        main.cpp)

set_target_properties(signlang_eyes_signlang_det PROPERTIES
        OUTPUT_NAME signlang_det)

target_include_directories(signlang_eyes_signlang_det PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(signlang_eyes_signlang_det PRIVATE
        cxxopts::cxxopts
        spdlog::spdlog
        iceoryx2-cxx::shared-lib-cxx
        signlang::rknn_runtime
        Threads::Threads
        m)

add_custom_command(TARGET signlang_eyes_signlang_det POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:signlang_eyes_signlang_det>/${SIGNLANG_DET_INSTALL_MODEL_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${SIGNLANG_DET_MODEL_FILE}
        "$<TARGET_FILE_DIR:signlang_eyes_signlang_det>/${SIGNLANG_DET_INSTALL_MODEL_DIR}/signlang.rknn"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${SIGNLANG_DET_LABEL_MAP_FILE}
        "$<TARGET_FILE_DIR:signlang_eyes_signlang_det>/${SIGNLANG_DET_INSTALL_MODEL_DIR}/labels.txt")

install(TARGETS signlang_eyes_signlang_det DESTINATION bin)
install(FILES
        ${SIGNLANG_DET_MODEL_FILE}
        ${SIGNLANG_DET_LABEL_MAP_FILE}
        DESTINATION ${SIGNLANG_DET_INSTALL_MODEL_DIR})
