project(isotp-c-tests VERSION 0.0.1 LANGUAGES CXX DESCRIPTION "GTest suites for isotp-c")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(GTest REQUIRED)

file(GLOB_RECURSE TEST_SOURCES CONFIGURE_DEPENDS "src/*.cpp")

add_executable(${PROJECT_NAME} ${TEST_SOURCES})

target_compile_options(${PROJECT_NAME} PRIVATE -O0)

target_link_libraries(
    ${PROJECT_NAME} PRIVATE

    ${GTEST_LIBRARIES}
    simon_cahill::isotpc
)

gtest_discover_tests(${PROJECT_NAME})