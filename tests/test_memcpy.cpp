#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <ostream>

#include <gtest/gtest.h>

#ifdef isotpc_USE_INCLUDE_DIR
    #include "isotp_c/isotp.h"
#else
    #include "isotp.h"
#endif

namespace {

    constexpr std::size_t MAX_SIZE = std::numeric_limits<std::size_t>::max();
    using Buffer                   = std::array<std::uint8_t, 8>;

    struct GuardCase {
            const char*       m_name;
            bool              m_nullDestination;
            bool              m_nullSource;
            std::size_t       m_destinationSize;
            std::size_t       m_sourceSize;
            std::size_t       m_count;
            IsoTpMemCpyResult m_result;
    };

    void PrintTo(const GuardCase& test, std::ostream* stream) { *stream << test.m_name; }

    class MemcpyGuardTest: public testing::TestWithParam<GuardCase> { };

    TEST_P(MemcpyGuardTest, ReturnsExpectedResultWithoutChangingEitherBuffer) {
        const auto& TEST = GetParam();
        Buffer      destination{0xA5, 0xA5, 0xA5, 0xA5, 0xA5, 0xA5, 0xA5, 0xA5};
        Buffer      source{0, 1, 0x7F, 0x80, 0xFE, 0xFF, 6, 7};
        const auto  ORIGINAL_DESTINATION = destination;
        const auto  ORIGINAL_SOURCE      = source;

        EXPECT_EQ(isotp_memcpy(TEST.m_nullDestination ? nullptr : destination.data(), TEST.m_destinationSize, TEST.m_nullSource ? nullptr : source.data(),
                               TEST.m_sourceSize, TEST.m_count),
                  TEST.m_result);
        EXPECT_EQ(destination, ORIGINAL_DESTINATION);
        EXPECT_EQ(source, ORIGINAL_SOURCE);
    }

    // The null cases exercise both operands of || separately, as well as their
    // precedence over size errors. Rejected SIZE_MAX counts must never be copied.
    INSTANTIATE_TEST_SUITE_P(
        InputChecks, MemcpyGuardTest,
        testing::Values(GuardCase{"ZeroCount", false, false, 8, 8, 0, ISOTP_MEMCPY_OK},
                        GuardCase{"ZeroCountAndCapacities", false, false, 0, 0, 0, ISOTP_MEMCPY_OK},
                        GuardCase{"ZeroCountNullDestination", true, false, 0, 8, 0, ISOTP_MEMCPY_OK},
                        GuardCase{"ZeroCountNullSource", false, true, 8, 0, 0, ISOTP_MEMCPY_OK},
                        GuardCase{"ZeroCountBothNull", true, true, 0, 0, 0, ISOTP_MEMCPY_OK},
                        GuardCase{"ZeroCountBothNullNonzeroCapacities", true, true, MAX_SIZE, MAX_SIZE, 0, ISOTP_MEMCPY_OK},
                        GuardCase{"NullDestination", true, false, 8, 8, 1, ISOTP_MEMCPY_NULLPTR},
                        GuardCase{"NullSource", false, true, 8, 8, 1, ISOTP_MEMCPY_NULLPTR}, GuardCase{"BothNull", true, true, 8, 8, 1, ISOTP_MEMCPY_NULLPTR},
                        GuardCase{"NullDestinationBeforeSizeChecks", true, false, 0, 0, 1, ISOTP_MEMCPY_NULLPTR},
                        GuardCase{"NullSourceBeforeSizeChecks", false, true, 0, 0, 1, ISOTP_MEMCPY_NULLPTR},
                        GuardCase{"BothNullBeforeSizeChecks", true, true, 0, 0, MAX_SIZE, ISOTP_MEMCPY_NULLPTR},
                        GuardCase{"EmptyDestination", false, false, 0, 8, 1, ISOTP_MEMCPY_DEST_TOO_SMALL},
                        GuardCase{"DestinationOneByteShort", false, false, 7, 8, 8, ISOTP_MEMCPY_DEST_TOO_SMALL},
                        GuardCase{"DestinationBeforeSourceCheck", false, false, 7, 6, 8, ISOTP_MEMCPY_DEST_TOO_SMALL},
                        GuardCase{"BothCapacitiesZero", false, false, 0, 0, 1, ISOTP_MEMCPY_DEST_TOO_SMALL},
                        GuardCase{"MaximumCountExceedsDestination", false, false, MAX_SIZE - 1, MAX_SIZE, MAX_SIZE, ISOTP_MEMCPY_DEST_TOO_SMALL},
                        GuardCase{"MaximumCountExceedsBoth", false, false, 8, 8, MAX_SIZE, ISOTP_MEMCPY_DEST_TOO_SMALL},
                        GuardCase{"EmptySource", false, false, 8, 0, 1, ISOTP_MEMCPY_SRC_TOO_SMALL},
                        GuardCase{"SourceOneByteShort", false, false, 8, 7, 8, ISOTP_MEMCPY_SRC_TOO_SMALL},
                        GuardCase{"MaximumCountExceedsSource", false, false, MAX_SIZE, MAX_SIZE - 1, MAX_SIZE, ISOTP_MEMCPY_SRC_TOO_SMALL}),
        [](const testing::TestParamInfo<GuardCase>& info) { return info.param.m_name; });

    struct CopyCase {
            const char* m_name;
            std::size_t m_destinationSize;
            std::size_t m_sourceSize;
            std::size_t m_count;
    };

    void PrintTo(const CopyCase& test, std::ostream* stream) { *stream << test.m_name; }

    class MemcpyCopyTest: public testing::TestWithParam<CopyCase> { };

    TEST_P(MemcpyCopyTest, CopiesExactlyTheRequestedBytes) {
        const auto& TEST = GetParam();
        // Offset both pointers to catch accidental writes before the destination;
        // the remaining sentinel bytes catch writes beyond the requested count.
        std::array<std::uint8_t, 10> destination{};
        destination.fill(0xA5);
        std::array<std::uint8_t, 10> source{0xDB, 0, 1, 0x7F, 0x80, 0xFE, 0xFF, 6, 7, 0xDB};
        const auto                   ORIGINAL_SOURCE = source;
        auto                         expected        = destination;
        for (std::size_t i = 0; i < TEST.m_count; ++i) { expected[i + 1] = source[i + 1]; }

        ASSERT_EQ(isotp_memcpy(destination.data() + 1, TEST.m_destinationSize, source.data() + 1, TEST.m_sourceSize, TEST.m_count), ISOTP_MEMCPY_OK);
        EXPECT_EQ(destination, expected);
        EXPECT_EQ(source, ORIGINAL_SOURCE);
    }

    INSTANTIATE_TEST_SUITE_P(CopyBoundaries, MemcpyCopyTest,
                             testing::Values(CopyCase{"OneByte", 1, 1, 1}, CopyCase{"ExactFit", 8, 8, 8}, CopyCase{"PartialCopy", 8, 8, 3},
                                             CopyCase{"ExactDestinationWithLargerSource", 3, 8, 3}, CopyCase{"ExactSourceWithLargerDestination", 8, 3, 3},
                                             CopyCase{"OneByteBelowBothCapacities", 8, 8, 7}),
                             [](const testing::TestParamInfo<CopyCase>& info) { return info.param.m_name; });

    TEST(MemcpyTest, CopiesEveryByteValue) {
        std::array<std::uint8_t, 256> source{};
        std::array<std::uint8_t, 256> destination{};
        for (std::size_t i = 0; i < source.size(); ++i) {
            source[i]      = static_cast<std::uint8_t>(i);
            destination[i] = static_cast<std::uint8_t>(~source[i]);
        }
        const auto ORIGINAL_SOURCE = source;

        ASSERT_EQ(isotp_memcpy(destination.data(), destination.size(), source.data(), source.size(), source.size()), ISOTP_MEMCPY_OK);
        EXPECT_EQ(destination, ORIGINAL_SOURCE);
        EXPECT_EQ(source, ORIGINAL_SOURCE);
    }

    TEST(MemcpyTest, SupportsEveryValidOverlapAndOffsetWithinABuffer) {
        const Buffer ORIGINAL{0, 1, 0x7F, 0x80, 0xFE, 0xFF, 6, 7};
        // Includes identical pointers, overlap in each direction, adjacent and
        // disjoint ranges, and zero-length operations at the one-past-end pointer.
        for (std::size_t destinationOffset = 0; destinationOffset <= ORIGINAL.size(); ++destinationOffset) {
            for (std::size_t sourceOffset = 0; sourceOffset <= ORIGINAL.size(); ++sourceOffset) {
                for (std::size_t count = 0; count <= ORIGINAL.size() - destinationOffset && count <= ORIGINAL.size() - sourceOffset; ++count) {
                    SCOPED_TRACE(testing::Message() << "destination=" << destinationOffset << ", source=" << sourceOffset << ", count=" << count);
                    auto buffer   = ORIGINAL;
                    auto expected = ORIGINAL;
                    // Use a snapshot as the oracle, independent of memcpy/memmove.
                    for (std::size_t i = 0; i < count; ++i) { expected[destinationOffset + i] = ORIGINAL[sourceOffset + i]; }

                    ASSERT_EQ(isotp_memcpy(buffer.data() + destinationOffset, buffer.size() - destinationOffset, buffer.data() + sourceOffset,
                                           buffer.size() - sourceOffset, count),
                              ISOTP_MEMCPY_OK);
                    EXPECT_EQ(buffer, expected);
                }
            }
        }
    }

} // namespace
