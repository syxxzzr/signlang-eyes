add_executable(signlang_eyes_edgeai_video_frontend
        program_options.cpp
        v4l2_capture_device.cpp
        video_processor.cpp
        video_publisher.cpp
        main.cpp)

target_include_directories(signlang_eyes_edgeai_video_frontend PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(signlang_eyes_edgeai_video_frontend PRIVATE
        cxxopts::cxxopts
        iceoryx2-cxx::shared-lib-cxx)

install(TARGETS signlang_eyes_edgeai_video_frontend DESTINATION bin)

if(SIGNLANG_ENABLE_TESTS)
        add_executable(signlang_eyes_edgeai_video_processor_test
                video_processor.cpp
                video_processor_test.cpp)

        target_include_directories(signlang_eyes_edgeai_video_processor_test PRIVATE
                ${CMAKE_CURRENT_SOURCE_DIR})

        target_link_libraries(signlang_eyes_edgeai_video_processor_test PRIVATE
                iceoryx2-cxx::shared-lib-cxx)

        add_test(NAME signlang_eyes_edgeai_video_processor_test
                COMMAND signlang_eyes_edgeai_video_processor_test)
endif()
