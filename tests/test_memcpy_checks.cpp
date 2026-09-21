#include <array>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>

#include <gtest/gtest.h>

#ifdef isotpc_USE_INCLUDE_DIR
    #include "isotp_c/isotp.h"
#else
    #include "isotp.h"
#endif

namespace {
    std::size_t   moveCalls;
    void*         lastDestination;
    const void*   lastSource;
    std::size_t   lastCount;
    bool          overrideMoveResult;
    void*         moveResult;
    constexpr int ASSERTION_EXIT_CODE = 86;

    class MemcpyChecksTest: public testing::Test {
        protected:
            void SetUp() override {
                moveCalls           = 0;
                lastDestination     = nullptr;
                lastSource          = nullptr;
                lastCount           = 0;
                overrideMoveResult = false;
                moveResult          = nullptr;
            }
    };

    using MemcpyChecksDeathTest = MemcpyChecksTest;
} // namespace

// Only the C test subject redirects memmove; the ordinary suites use libc.
extern "C" void* isotp_test_memmove(void* destination, const void* source, std::size_t count) {
    ++moveCalls;
    lastDestination = destination;
    lastSource      = source;
    lastCount       = count;
    
    if (overrideMoveResult) { return moveResult; }

    return std::memmove(destination, source, count);
}

// Keep libc's assert macro and its condition intact, but exit normally on
// failure so gcov can save the failing branch from the death-test child.
extern "C" [[noreturn]] void isotp_test_assert_fail(const char* expression, const char* file, unsigned int line, const char* function) noexcept {
    std::fprintf(stderr, "%s:%u: %s: assertion failed: %s\n", file, line, function, expression);
    std::exit(ASSERTION_EXIT_CODE);
}

TEST_F(MemcpyChecksTest, GuardsNeverCallMemmove) {
    std::array<unsigned char, 2>       destination{0xA5, 0xA5};
    const std::array<unsigned char, 2> SOURCE{0x12, 0x34};

    EXPECT_EQ(isotp_memcpy(nullptr, 0, nullptr, 0, 0), ISOTP_MEMCPY_OK);
    EXPECT_EQ(isotp_memcpy(nullptr, 2, SOURCE.data(), 2, 1), ISOTP_MEMCPY_NULLPTR);
    EXPECT_EQ(isotp_memcpy(destination.data(), 2, nullptr, 2, 1), ISOTP_MEMCPY_NULLPTR);
    EXPECT_EQ(isotp_memcpy(destination.data(), 0, SOURCE.data(), 2, 1), ISOTP_MEMCPY_DEST_TOO_SMALL);
    EXPECT_EQ(isotp_memcpy(destination.data(), 2, SOURCE.data(), 0, 1), ISOTP_MEMCPY_SRC_TOO_SMALL);
    EXPECT_EQ(moveCalls, 0U);
}

TEST_F(MemcpyChecksTest, ForwardsPointersAndCountExactlyOnce) {
    std::array<unsigned char, 4>       destination{};
    const std::array<unsigned char, 4> SOURCE{1, 2, 3, 4};

    ASSERT_EQ(isotp_memcpy(destination.data() + 1, 3, SOURCE.data() + 1, 3, 2), ISOTP_MEMCPY_OK);
    EXPECT_EQ(moveCalls, 1U);
    EXPECT_EQ(lastDestination, destination.data() + 1);
    EXPECT_EQ(lastSource, SOURCE.data() + 1);
    EXPECT_EQ(lastCount, 2U);
    EXPECT_EQ(destination, (std::array<unsigned char, 4>{0, 2, 3, 0}));
}

TEST_F(MemcpyChecksTest, MaximumCountEqualToBothCapacitiesPassesGuardsWithoutTruncation) {
    unsigned char       destination = 0xA5;
    const unsigned char SOURCE      = 0x12;
    const auto          MAXIMUM     = std::numeric_limits<std::size_t>::max();
    // A stub is essential: no real object can hold SIZE_MAX bytes.
    overrideMoveResult = true;
    moveResult          = &destination;

    ASSERT_EQ(isotp_memcpy(&destination, MAXIMUM, &SOURCE, MAXIMUM, MAXIMUM), ISOTP_MEMCPY_OK);
    EXPECT_EQ(moveCalls, 1U);
    EXPECT_EQ(lastDestination, &destination);
    EXPECT_EQ(lastSource, &SOURCE);
    EXPECT_EQ(lastCount, MAXIMUM);
    EXPECT_EQ(destination, 0xA5);
}

TEST_F(MemcpyChecksDeathTest, AssertsWhenMemmoveReturnsNull) {
    unsigned char       destination = 0;
    const unsigned char SOURCE      = 1;
    overrideMoveResult            = true;
    moveResult                     = nullptr;

    EXPECT_EXIT((void)isotp_memcpy(&destination, 1, &SOURCE, 1, 1), testing::ExitedWithCode(ASSERTION_EXIT_CODE),
                "isotp_memcpy.*assertion failed:.*== destPtr");
}

TEST_F(MemcpyChecksDeathTest, AssertsWhenMemmoveReturnsADifferentNonNullPointer) {
    unsigned char       destination = 0;
    const unsigned char SOURCE      = 1;
    unsigned char       other       = 2;
    overrideMoveResult            = true;
    moveResult                     = &other;

    EXPECT_EXIT((void)isotp_memcpy(&destination, 1, &SOURCE, 1, 1), testing::ExitedWithCode(ASSERTION_EXIT_CODE),
                "isotp_memcpy.*assertion failed:.*== destPtr");
}
