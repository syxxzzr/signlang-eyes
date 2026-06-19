add_executable(signlang_eyes_video_frontend
        program_options.cpp
        v4l2_capture_device.cpp
        video_processor.cpp
        video_publisher.cpp
        main.cpp)

set_target_properties(signlang_eyes_video_frontend PROPERTIES
        OUTPUT_NAME video_frontend)

target_include_directories(signlang_eyes_video_frontend PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(signlang_eyes_video_frontend PRIVATE
        cxxopts::cxxopts
        iceoryx2-cxx::shared-lib-cxx)

install(TARGETS signlang_eyes_video_frontend DESTINATION bin)

if(SIGNLANG_ENABLE_TESTS)
        add_executable(signlang_eyes_video_frontend_snapshot_test
                video_snapshot_test.cpp)

        set_target_properties(signlang_eyes_video_frontend_snapshot_test PROPERTIES
                OUTPUT_NAME video_frontend_snapshot_test)

        target_include_directories(signlang_eyes_video_frontend_snapshot_test PRIVATE
                ${CMAKE_CURRENT_SOURCE_DIR})

        target_link_libraries(signlang_eyes_video_frontend_snapshot_test PRIVATE
                cxxopts::cxxopts
                iceoryx2-cxx::shared-lib-cxx)
endif()
