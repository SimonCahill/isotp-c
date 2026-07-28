###
# Streaming tests
#
# The library is compiled into a dedicated static library, so that the tests may
# use their own set of configuration macros. The maximum frame size follows the
# configuration of the project, so that streaming is also covered on CAN FD builds.
###
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
    ISO_TP_MAX_CAN_FRAME_SIZE=${isotpc_MAX_CAN_FRAME_SIZE}
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

###
# Framing tests
#
# These cover the frame formats of Classical CAN and CAN FD. The library sources
# are compiled into every test binary, so that each of them may use its own set
# of configuration macros.
###
set(isotpc_FRAMING_TEST_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/test_framing.c ${PROJECT_SOURCE_DIR}/isotp.c)

###
# Adds a framing test binary using the given configuration macros
###
function(isotpc_add_framing_test test_name)
    add_executable(${test_name} ${isotpc_FRAMING_TEST_SOURCES})
    target_include_directories(${test_name} PRIVATE ${PROJECT_SOURCE_DIR})
    target_compile_definitions(${test_name} PRIVATE ${ARGN})

    if (MSVC)
        target_compile_options(${test_name} PRIVATE /W4)
    else()
        target_compile_options(${test_name} PRIVATE -Werror -Wall -Wpedantic -Wextra)
    endif()

    add_test(NAME ${test_name} COMMAND ${test_name})
endfunction()

isotpc_add_framing_test(isotp_test_classic          -DISO_TP_MAX_CAN_FRAME_SIZE=8  -DISO_TP_FRAME_PADDING)
isotpc_add_framing_test(isotp_test_classic_unpadded -DISO_TP_MAX_CAN_FRAME_SIZE=8)
isotpc_add_framing_test(isotp_test_can_fd           -DISO_TP_MAX_CAN_FRAME_SIZE=64 -DISO_TP_FRAME_PADDING)
isotpc_add_framing_test(isotp_test_can_fd_unpadded  -DISO_TP_MAX_CAN_FRAME_SIZE=64)

# the optional features are covered independently of the options this project was configured with
isotpc_add_framing_test(isotp_test_can_fd_events    -DISO_TP_MAX_CAN_FRAME_SIZE=64 -DISO_TP_FRAME_PADDING -DISO_TP_TRANSMIT_COMPLETE_CALLBACK -DISO_TP_RECEIVE_COMPLETE_CALLBACK)
isotpc_add_framing_test(isotp_test_can_fd_send_arg  -DISO_TP_MAX_CAN_FRAME_SIZE=64 -DISO_TP_FRAME_PADDING -DISO_TP_USER_SEND_CAN_ARG)
isotpc_add_framing_test(isotp_test_classic_events   -DISO_TP_MAX_CAN_FRAME_SIZE=8  -DISO_TP_FRAME_PADDING -DISO_TP_TRANSMIT_COMPLETE_CALLBACK -DISO_TP_RECEIVE_COMPLETE_CALLBACK)
isotpc_add_framing_test(isotp_test_can_fd_flags     -DISO_TP_MAX_CAN_FRAME_SIZE=64 -DISO_TP_FRAME_PADDING -DISO_TP_USER_SEND_CAN_FLAGS)
isotpc_add_framing_test(isotp_test_can_fd_flags_brs -DISO_TP_MAX_CAN_FRAME_SIZE=64 -DISO_TP_USER_SEND_CAN_FLAGS -DISO_TP_CAN_FD_USE_BRS -DISO_TP_USER_SEND_CAN_ARG)
isotpc_add_framing_test(isotp_test_classic_flags    -DISO_TP_MAX_CAN_FRAME_SIZE=8  -DISO_TP_FRAME_PADDING -DISO_TP_USER_SEND_CAN_FLAGS)
isotpc_add_framing_test(isotp_test_can_fd_streaming -DISO_TP_MAX_CAN_FRAME_SIZE=64 -DISO_TP_FRAME_PADDING -DISO_TP_ENABLE_STREAMING)
isotpc_add_framing_test(isotp_test_classic_streaming -DISO_TP_MAX_CAN_FRAME_SIZE=8 -DISO_TP_FRAME_PADDING -DISO_TP_ENABLE_STREAMING)
