/**
 * @file test_main.cpp
 * @author Simon Cahill (contact@simonc.eu)
 * @brief Contains the entry point for the test application.
 * @version 0.1
 * @date 2025-08-25
 * 
 * @copyright Copyright (c) 2025 Simon Cahill and Contributors.
 */

#include <cstdint>
#include <gtest/gtest.h>

int main(int32_t argc, char** const argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}