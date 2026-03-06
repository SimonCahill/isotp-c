project(isotp-c-tests VERSION 0.0.1 LANGUAGES CXX DESCRIPTION "GTest suites for isotp-c")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(GTest QUIET)

if (NOT GTest_FOUND)
    if (CMAKE_VERSION VERSION_LESS 3.11)
        message(FATAL_ERROR "GTest not found and FetchContent requires CMake >= 3.11. Install GTest or upgrade CMake.")
    endif()

    include(FetchContent)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/archive/refs/tags/v1.17.0.tar.gz
    )
    FetchContent_MakeAvailable(googletest)
endif()

file(GLOB_RECURSE TEST_SOURCES CONFIGURE_DEPENDS "src/*.cpp")

add_executable(${PROJECT_NAME} ${TEST_SOURCES})

target_compile_options(${PROJECT_NAME} PRIVATE -O0)

if (isotpc_PAD_CAN_FRAMES)
    target_compile_definitions(
        ${PROJECT_NAME} PRIVATE
        ISO_TP_FRAME_PADDING
        ISO_TP_FRAME_PADDING_VALUE=${isotpc_CAN_FRAME_PAD_VALUE}
    )
endif()

target_link_libraries(
    ${PROJECT_NAME} PRIVATE

    GTest::gtest
    GTest::gtest_main
    simon_cahill::isotpc
)

gtest_discover_tests(${PROJECT_NAME})