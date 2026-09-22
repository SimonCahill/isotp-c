#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

#include <gtest/gtest.h>

#ifdef isotpc_USE_INCLUDE_DIR
    #include "isotp_c/isotp.h"
#else
    #include "isotp.h"
#endif

extern "C" int isotp_test_pad_frame(IsoTpCanMessage* message, std::uint8_t usedLength);

namespace {

    constexpr int          CAPACITY = ISO_TP_MAX_CAN_FRAME_SIZE;
    constexpr std::uint8_t SENTINEL = static_cast<std::uint8_t>(ISO_TP_FRAME_PADDING_VALUE ^ 0xFF);

    struct GuardedFrame {
            std::array<std::uint8_t, 16> m_before;
            IsoTpCanMessage              m_message;
            std::array<std::uint8_t, 64> m_after;
    };

    class PaddingTest: public testing::TestWithParam<int> {
        protected:
            void         SetUp() override { std::memset(&m_frame, SENTINEL, sizeof(m_frame)); }

            GuardedFrame m_frame{};
    };

    class ValidPaddingLengthTest: public PaddingTest { };
    class InvalidPaddingLengthTest: public PaddingTest { };

    int expectedFrameLength(int usedLength) {
        // Independent lookup table, indexed by the number of occupied bytes.
        constexpr std::array<int, 65> EXPECTED_LENGTHS{0,  1,  2,  3,  4,  5,  6,  7,  8,  12, 12, 12, 12, 16, 16, 16, 16, 20, 20, 20, 20, 24,
                                                       24, 24, 24, 32, 32, 32, 32, 32, 32, 32, 32, 48, 48, 48, 48, 48, 48, 48, 48, 48, 48, 48,
                                                       48, 48, 48, 48, 48, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64};
#ifdef ISO_TP_FRAME_PADDING
        if (usedLength < 8) { return 8; }
#endif
        return EXPECTED_LENGTHS[static_cast<std::size_t>(usedLength)];
    }

    TEST_P(ValidPaddingLengthTest, PadsOnlyUnusedBytesWithinCapacity) {
        const int    USED_LENGTH     = GetParam();
        const int    EXPECTED_LENGTH = expectedFrameLength(USED_LENGTH);
        GuardedFrame expected;
        // Preserve the complete object representation, including any struct padding.
        std::memcpy(&expected, &m_frame, sizeof(expected));
        std::fill(expected.m_message.as.data_array.ptr + USED_LENGTH, expected.m_message.as.data_array.ptr + EXPECTED_LENGTH, ISO_TP_FRAME_PADDING_VALUE);

        const int RESULT = isotp_test_pad_frame(&m_frame.m_message, static_cast<std::uint8_t>(USED_LENGTH));

        EXPECT_EQ(RESULT, EXPECTED_LENGTH);
        EXPECT_GE(RESULT, USED_LENGTH);
        EXPECT_LE(RESULT, CAPACITY);
        EXPECT_EQ(m_frame.m_before, expected.m_before);
        EXPECT_EQ(m_frame.m_after, expected.m_after);
        EXPECT_EQ(std::memcmp(&m_frame.m_message, &expected.m_message, sizeof(expected.m_message)), 0);
        EXPECT_EQ(std::memcmp(&m_frame, &expected, sizeof(expected)), 0);
    }

    TEST_P(InvalidPaddingLengthTest, RejectsOversizedLengthWithoutWriting) {
        const int                                       USED_LENGTH = GetParam();
        std::array<unsigned char, sizeof(GuardedFrame)> original{};
        std::memcpy(original.data(), &m_frame, sizeof(m_frame));

        EXPECT_EQ(isotp_test_pad_frame(&m_frame.m_message, static_cast<std::uint8_t>(USED_LENGTH)), ISOTP_RET_LENGTH);
        EXPECT_EQ(std::memcmp(&m_frame, original.data(), original.size()), 0);
    }

    std::string lengthCaseName(const testing::TestParamInfo<int>& info) {
        if (CAPACITY == 8 && info.param == 9) { return "NineBytesInEightByteBuffer"; }
        if (info.param == 0) { return "ZeroLength"; }
        if (info.param == CAPACITY) { return "ExactCapacity"; }
        if (info.param == CAPACITY + 1) { return "OneByteBeyondCapacity"; }
        if (info.param == 255) { return "MaximumUint8Length"; }
        return "Length" + std::to_string(info.param);
    }

    INSTANTIATE_TEST_SUITE_P(ValidLengths, ValidPaddingLengthTest, testing::Range(0, CAPACITY + 1), lengthCaseName);
    INSTANTIATE_TEST_SUITE_P(InvalidLengths, InvalidPaddingLengthTest, testing::Range(CAPACITY + 1, 256), lengthCaseName);

} // namespace
