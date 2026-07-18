add_library(isotp_streaming_test_subject STATIC
    ${PROJECT_SOURCE_DIR}/isotp.c
)

target_include_directories(isotp_streaming_test_subject PUBLIC
    ${PROJECT_SOURCE_DIR}
)

target_compile_definitions(isotp_streaming_test_subject PUBLIC
    ISO_TP_ENABLE_STREAMING
    ISO_TP_FRAME_PADDING
    ISO_TP_RECEIVE_COMPLETE_CALLBACK
)

add_executable(isotp_streaming_tests
    test_streaming.c
    mocks/isotp_user_mock.c
)

target_include_directories(isotp_streaming_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(isotp_streaming_tests PRIVATE
    isotp_streaming_test_subject
)

if (MSVC)
    target_compile_options(isotp_streaming_test_subject PRIVATE /W4)
    target_compile_options(isotp_streaming_tests PRIVATE /W4)
else()
    target_compile_options(isotp_streaming_test_subject PRIVATE
        -Werror -Wall -Wpedantic -Wextra -Wno-unknown-pragmas
    )
    target_compile_options(isotp_streaming_tests PRIVATE
        -Werror -Wall -Wpedantic -Wextra
    )
endif()

add_test(NAME isotp_streaming_tests COMMAND isotp_streaming_tests)
