add_library(signlang_common STATIC
        audio_ring_buffer.cpp)

target_include_directories(signlang_common PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/..)

add_library(signlang::common ALIAS signlang_common)
