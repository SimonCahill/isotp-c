#include <cstdint>

#include <gtest/gtest.h>

extern "C" {
std::uint8_t isotp_test_ceil_can_dl(std::uint8_t length);
std::uint8_t isotp_test_us_to_st_min(std::uint32_t us);
std::uint32_t isotp_test_st_min_to_us(std::uint8_t stMin);
}

TEST(ProtocolHelpersTest, RoundsFrameLengthsAtEveryCanFdBoundary) {
    const struct {
        std::uint8_t length;
        std::uint8_t expected;
    } CASES[] = {{0, 0}, {8, 8}, {9, 12}, {12, 12}, {13, 16}, {16, 16}, {17, 20}, {20, 20},
                 {21, 24}, {24, 24}, {25, 32}, {32, 32}, {33, 48}, {48, 48}, {49, 64}, {64, 64},
                 {65, 64}, {UINT8_MAX, 64}};

    for (const auto& CASE : CASES) {
        SCOPED_TRACE(static_cast<unsigned>(CASE.length));
        EXPECT_EQ(isotp_test_ceil_can_dl(CASE.length), CASE.expected);
    }
}

TEST(ProtocolHelpersTest, EncodesSeparationTimeBoundaries) {
    const struct {
        std::uint32_t microseconds;
        std::uint8_t code;
    } CASES[] = {{0, 0}, {99, 0}, {100, 0xF1}, {199, 0xF1}, {200, 0xF2}, {900, 0xF9},
                 {901, 0}, {999, 0}, {1000, 1}, {127000, 0x7F}, {127001, 0}, {UINT32_MAX, 0}};

    for (const auto& CASE : CASES) {
        SCOPED_TRACE(CASE.microseconds);
        EXPECT_EQ(isotp_test_us_to_st_min(CASE.microseconds), CASE.code);
    }
}

TEST(ProtocolHelpersTest, DecodesSeparationTimeAndReservedCodes) {
    const struct {
        std::uint8_t code;
        std::uint32_t microseconds;
    } CASES[] = {{0, 0}, {1, 1000}, {0x7F, 127000}, {0x80, 0}, {0xF0, 0},
                 {0xF1, 100}, {0xF9, 900}, {0xFA, 0}, {0xFF, 0}};

    for (const auto& CASE : CASES) {
        SCOPED_TRACE(static_cast<unsigned>(CASE.code));
        EXPECT_EQ(isotp_test_st_min_to_us(CASE.code), CASE.microseconds);
    }
}
