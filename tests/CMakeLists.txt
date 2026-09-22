include(FetchContent)

set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.17.0
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(googletest)

include(GoogleTest)

if (isotpc_ENABLE_COVERAGE AND NOT CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    message(FATAL_ERROR "[isotp-c] Coverage instrumentation requires GCC or Clang")
endif()

function(isotpc_set_test_warnings target)
    if (MSVC)
        target_compile_options(${target} PRIVATE /W4 /WX)
    else()
        target_compile_options(${target} PRIVATE -Werror -Wall -Wpedantic -Wextra)
    endif()
endfunction()

function(isotpc_add_unit_test test_name source)
    set(subject_name "${test_name}_subject")

    add_library(${subject_name} STATIC ${PROJECT_SOURCE_DIR}/isotp.c)
    target_include_directories(${subject_name} PUBLIC ${PROJECT_SOURCE_DIR})
    target_compile_definitions(${subject_name} PUBLIC ${ARGN})
    isotpc_set_test_warnings(${subject_name})
    if (NOT MSVC)
        target_compile_options(${subject_name} PRIVATE -Wno-unknown-pragmas)
    endif()

    add_executable(
        ${test_name}
        ${source}
        ${CMAKE_CURRENT_SOURCE_DIR}/mocks/isotp_user_mock.cpp
    )
    target_include_directories(${test_name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_features(${test_name} PRIVATE cxx_std_17)
    target_link_libraries(${test_name} PRIVATE ${subject_name} GTest::gmock_main)
    isotpc_set_test_warnings(${test_name})

    if (isotpc_USE_INCLUDE_DIR)
        target_include_directories(${test_name} PRIVATE ${PROJECT_BINARY_DIR}/include)
        target_compile_definitions(${test_name} PRIVATE isotpc_USE_INCLUDE_DIR=1)
    endif()

    if (isotpc_ENABLE_COVERAGE)
        target_compile_options(${subject_name} PRIVATE --coverage -O0 -g)
        target_link_options(${test_name} PRIVATE --coverage)
    endif()

    gtest_discover_tests(
        ${test_name}
        TEST_PREFIX "${test_name}."
        NO_PRETTY_VALUES
        DISCOVERY_MODE PRE_TEST
    )
endfunction()

# Compile the actual padding helper for every supported capacity and padding mode.
foreach(frameCapacity IN ITEMS 8 12 16 20 24 32 48 64)
    foreach(paddingMode IN ITEMS padded unpadded)
        set(testName "isotp_test_padding_${frameCapacity}_${paddingMode}")
        set(paddingDefinitions ISO_TP_MAX_CAN_FRAME_SIZE=${frameCapacity})
        if (paddingMode STREQUAL "padded")
            list(APPEND paddingDefinitions ISO_TP_FRAME_PADDING)
        endif()
        isotpc_add_unit_test(${testName} ${CMAKE_CURRENT_SOURCE_DIR}/test_padding.cpp ${paddingDefinitions})
        # The bridge includes isotp.c, so do not also compile it separately.
        set_property(TARGET ${testName}_subject PROPERTY SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/support/padding_subject.c)
    endforeach()
endforeach()

isotpc_add_unit_test(isotp_test_memcpy ${CMAKE_CURRENT_SOURCE_DIR}/test_memcpy.cpp)
isotpc_add_unit_test(isotp_test_memcpy_ndebug ${CMAKE_CURRENT_SOURCE_DIR}/test_memcpy.cpp NDEBUG)

# Exercise the assertion regardless of the selected CMake build configuration.
if (MSVC)
    set(isotpc_UNDEFINE_NDEBUG /UNDEBUG)
else()
    set(isotpc_UNDEFINE_NDEBUG -UNDEBUG)
endif()
target_compile_options(isotp_test_memcpy_subject PRIVATE ${isotpc_UNDEFINE_NDEBUG})

# Fault injection needs the libc assertion hook. The ordinary copy and NDEBUG
# suites above remain available on platforms without this hook.
include(CheckSymbolExists)
include(CMakePushCheckState)
cmake_push_check_state(RESET)
set(CMAKE_REQUIRED_DEFINITIONS ${isotpc_UNDEFINE_NDEBUG})
check_symbol_exists(__assert_fail "assert.h" isotpc_HAVE_ASSERT_FAIL)
cmake_pop_check_state()
if (isotpc_HAVE_ASSERT_FAIL)
    isotpc_add_unit_test(isotp_test_memcpy_checks ${CMAKE_CURRENT_SOURCE_DIR}/test_memcpy_checks.cpp)
    # Load libc headers before redirecting memmove, so fortified inline wrappers
    # cannot replace the test stub with a real copy in optimized builds.
    set_property(TARGET isotp_test_memcpy_checks_subject PROPERTY SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/support/memcpy_checks_subject.c)
    target_compile_options(isotp_test_memcpy_checks_subject PRIVATE ${isotpc_UNDEFINE_NDEBUG})
    target_compile_definitions(isotp_test_memcpy_checks_subject PRIVATE
        __assert_fail=isotp_test_assert_fail
    )
endif()

if (isotpc_ENABLE_STREAMING)
    isotpc_add_unit_test(
        isotp_streaming_tests
        ${CMAKE_CURRENT_SOURCE_DIR}/test_streaming.cpp
        ISO_TP_ENABLE_STREAMING
        ISO_TP_FRAME_PADDING
        ISO_TP_RECEIVE_COMPLETE_CALLBACK
        ISO_TP_MAX_CAN_FRAME_SIZE=${isotpc_MAX_CAN_FRAME_SIZE}
    )
endif()

isotpc_add_unit_test(
    isotp_test_classic
    ${CMAKE_CURRENT_SOURCE_DIR}/test_framing.cpp
    ISO_TP_MAX_CAN_FRAME_SIZE=8
    ISO_TP_FRAME_PADDING
)
isotpc_add_unit_test(
    isotp_test_classic_unpadded
    ${CMAKE_CURRENT_SOURCE_DIR}/test_framing.cpp
    ISO_TP_MAX_CAN_FRAME_SIZE=8
)
isotpc_add_unit_test(
    isotp_test_can_fd
    ${CMAKE_CURRENT_SOURCE_DIR}/test_framing.cpp
    ISO_TP_MAX_CAN_FRAME_SIZE=64 ISO_TP_FRAME_PADDING
)
isotpc_add_unit_test(
    isotp_test_can_fd_unpadded
    ${CMAKE_CURRENT_SOURCE_DIR}/test_framing.cpp
    ISO_TP_MAX_CAN_FRAME_SIZE=64
)

# Optional features are covered independently of the project's selected options.
isotpc_add_unit_test(
    isotp_test_can_fd_events
    ${CMAKE_CURRENT_SOURCE_DIR}/test_framing.cpp
    ISO_TP_MAX_CAN_FRAME_SIZE=64 ISO_TP_FRAME_PADDING
    ISO_TP_TRANSMIT_COMPLETE_CALLBACK
    ISO_TP_RECEIVE_COMPLETE_CALLBACK
)
isotpc_add_unit_test(
    isotp_test_can_fd_send_arg
    ${CMAKE_CURRENT_SOURCE_DIR}/test_framing.cpp
    ISO_TP_MAX_CAN_FRAME_SIZE=64 ISO_TP_FRAME_PADDING
    ISO_TP_USER_SEND_CAN_ARG
)
isotpc_add_unit_test(
    isotp_test_classic_events
    ${CMAKE_CURRENT_SOURCE_DIR}/test_framing.cpp
    ISO_TP_MAX_CAN_FRAME_SIZE=8 ISO_TP_FRAME_PADDING
    ISO_TP_TRANSMIT_COMPLETE_CALLBACK
    ISO_TP_RECEIVE_COMPLETE_CALLBACK
)
isotpc_add_unit_test(
    isotp_test_can_fd_flags
    ${CMAKE_CURRENT_SOURCE_DIR}/test_framing.cpp
    ISO_TP_MAX_CAN_FRAME_SIZE=64
    ISO_TP_FRAME_PADDING
    ISO_TP_USER_SEND_CAN_FLAGS
)
isotpc_add_unit_test(
    isotp_test_can_fd_flags_brs
    ${CMAKE_CURRENT_SOURCE_DIR}/test_framing.cpp
    ISO_TP_MAX_CAN_FRAME_SIZE=64
    ISO_TP_USER_SEND_CAN_FLAGS
    ISO_TP_CAN_FD_USE_BRS
    ISO_TP_USER_SEND_CAN_ARG
)
isotpc_add_unit_test(
    isotp_test_classic_flags
    ${CMAKE_CURRENT_SOURCE_DIR}/test_framing.cpp
    ISO_TP_MAX_CAN_FRAME_SIZE=8
    ISO_TP_FRAME_PADDING
    ISO_TP_USER_SEND_CAN_FLAGS
)
isotpc_add_unit_test(
    isotp_test_can_fd_streaming
    ${CMAKE_CURRENT_SOURCE_DIR}/test_framing.cpp
    ISO_TP_MAX_CAN_FRAME_SIZE=64
    ISO_TP_FRAME_PADDING
    ISO_TP_ENABLE_STREAMING
)
isotpc_add_unit_test(
    isotp_test_classic_streaming
    ${CMAKE_CURRENT_SOURCE_DIR}/test_framing.cpp
    ISO_TP_MAX_CAN_FRAME_SIZE=8
    ISO_TP_FRAME_PADDING
    ISO_TP_ENABLE_STREAMING
)
