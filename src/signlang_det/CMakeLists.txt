set(SIGNLANG_DET_MODEL_FILE ${SIGNLANG_BILSTM_MODEL_FILE})
set(SIGNLANG_DET_PROTOTYPES_FILE ${SIGNLANG_PROTOTYPES_FILE})
set(SIGNLANG_DET_INSTALL_MODEL_DIR models/bilstm)
set(SIGNLANG_DET_INSTALL_CONF_DIR conf)

if(NOT TARGET signlang::rknn_runtime)
    message(STATUS "Skipping signlang_eyes_signlang_det: RKNN runtime target not available")
    return()
endif()

foreach(required_file
        ${SIGNLANG_DET_MODEL_FILE}
        ${SIGNLANG_DET_PROTOTYPES_FILE})
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
        SQLiteCpp::SQLiteCpp
        spdlog::spdlog
        iceoryx2-cxx::shared-lib-cxx
        signlang::rknn_runtime
        Threads::Threads
        m)

add_custom_command(TARGET signlang_eyes_signlang_det POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:signlang_eyes_signlang_det>/${SIGNLANG_DET_INSTALL_MODEL_DIR}"
        COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:signlang_eyes_signlang_det>/${SIGNLANG_DET_INSTALL_CONF_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${SIGNLANG_DET_MODEL_FILE}
        "$<TARGET_FILE_DIR:signlang_eyes_signlang_det>/${SIGNLANG_DET_INSTALL_MODEL_DIR}/biltsm.rknn"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${SIGNLANG_DET_PROTOTYPES_FILE}
        "$<TARGET_FILE_DIR:signlang_eyes_signlang_det>/${SIGNLANG_DET_INSTALL_CONF_DIR}/prototypes.sqlite")

install(TARGETS signlang_eyes_signlang_det DESTINATION bin)
