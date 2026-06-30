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
        spdlog::spdlog
        iceoryx2-cxx::shared-lib-cxx
        signlang::common
        libjpeg-turbo::turbojpeg
        librga::rga)

install(TARGETS signlang_eyes_video_frontend DESTINATION bin)
