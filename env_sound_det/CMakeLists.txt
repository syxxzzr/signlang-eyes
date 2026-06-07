set(ENV_SOUND_DET_MODEL_DIR
        ${SIGNLANG_MODEL_ROOT}/yamnet
        CACHE PATH "Directory containing YAMNet RKNN model assets")
set(ENV_SOUND_DET_MODEL_FILE ${ENV_SOUND_DET_MODEL_DIR}/yamnet_3s.rknn)
set(ENV_SOUND_DET_CLASS_MAP_FILE ${ENV_SOUND_DET_MODEL_DIR}/yamnet_class_map.txt)
set(ENV_SOUND_DET_INSTALL_MODEL_DIR models/yamnet)

if(NOT SIGNLANG_IS_AARCH64)
    message(STATUS "Skipping signlang_eyes_edgeai_env_sound_det: RKNN runtime is project-local aarch64 only")
    return()
endif()

if(NOT TARGET signlang::rknn_runtime)
    message(FATAL_ERROR "Missing project-local RKNN runtime: ${SIGNLANG_RKNN_LIBRARY}")
endif()

foreach(required_file
        ${ENV_SOUND_DET_MODEL_FILE}
        ${ENV_SOUND_DET_CLASS_MAP_FILE})
    if(NOT EXISTS ${required_file})
        message(FATAL_ERROR "Missing required environment sound detector asset: ${required_file}")
    endif()
endforeach()

add_executable(signlang_eyes_edgeai_env_sound_det
        audio_ring_buffer.cpp
        iceoryx_gateway.cpp
        program_options.cpp
        yamnet_model.cpp
        main.cpp)

target_include_directories(signlang_eyes_edgeai_env_sound_det PRIVATE
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(signlang_eyes_edgeai_env_sound_det PRIVATE
        cxxopts::cxxopts
        iceoryx2-cxx::static-lib-cxx
        signlang::rknn_runtime
        Threads::Threads
        dl)

set_target_properties(signlang_eyes_edgeai_env_sound_det PROPERTIES
        BUILD_RPATH ${SIGNLANG_RKNN_RUNTIME_ROOT}/lib/aarch64
        INSTALL_RPATH "$ORIGIN/../lib")

add_custom_command(TARGET signlang_eyes_edgeai_env_sound_det POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:signlang_eyes_edgeai_env_sound_det>/${ENV_SOUND_DET_INSTALL_MODEL_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${ENV_SOUND_DET_MODEL_FILE}
        "$<TARGET_FILE_DIR:signlang_eyes_edgeai_env_sound_det>/${ENV_SOUND_DET_INSTALL_MODEL_DIR}/yamnet_3s.rknn"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${ENV_SOUND_DET_CLASS_MAP_FILE}
        "$<TARGET_FILE_DIR:signlang_eyes_edgeai_env_sound_det>/${ENV_SOUND_DET_INSTALL_MODEL_DIR}/yamnet_class_map.txt")

install(TARGETS signlang_eyes_edgeai_env_sound_det DESTINATION bin)
install(FILES ${SIGNLANG_RKNN_LIBRARY} DESTINATION lib)
install(FILES
        ${ENV_SOUND_DET_MODEL_FILE}
        ${ENV_SOUND_DET_CLASS_MAP_FILE}
        DESTINATION ${ENV_SOUND_DET_INSTALL_MODEL_DIR})
