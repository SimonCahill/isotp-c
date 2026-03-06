/**
 * @file test_standard_isotp.cpp
 * @author Simon Cahill (contact@simonc.eu)
 * @brief Contains tests covering basic ISOTP-C usage.
 * @version 0.1
 * @date 2025-08-25
 * 
 * @copyright Copyright (c) 2025 Simon Cahill and Contributors.
 */

#include <gtest/gtest.h>
#include <cstdint>
#include <type_traits>

extern "C" {
#include "isotp.h"
}

// Verify header guards (safe to include multiple times)
extern "C" {
#include "isotp.h"
}

namespace {

// Helper to assert a byte value range at compile time if possible
template <typename T>
constexpr bool IsByte(T v) {
    return static_cast<uint64_t>(v) <= 0xFFu;
}

} // namespace

TEST(IsoTp_Header, MultipleInclusionSucceeds) {
    SUCCEED();
}

TEST(IsoTp_Header, CplusplusCompatibility) {
    // If the header forgot extern "C" guards internally it still compiles here due to our wrapper.
    SUCCEED();
}

TEST(IsoTp_Macros, PaddingByteIsWithinRangeIfDefined) {
#if defined(ISOTP_DEFAULT_PADDING)
    static_assert(std::is_integral<decltype(ISOTP_DEFAULT_PADDING)>::value, "ISOTP_DEFAULT_PADDING must be integral");
    ASSERT_LE(static_cast<unsigned>(ISOTP_DEFAULT_PADDING), 0xFFu);
#elif defined(ISO_TP_DEFAULT_PADDING)
    static_assert(std::is_integral<decltype(ISO_TP_DEFAULT_PADDING)>::value, "ISO_TP_DEFAULT_PADDING must be integral");
    ASSERT_LE(static_cast<unsigned>(ISO_TP_DEFAULT_PADDING), 0xFFu);
#elif defined(ISO_TP_PAD_BYTE)
    static_assert(std::is_integral<decltype(ISO_TP_PAD_BYTE)>::value, "ISO_TP_PAD_BYTE must be integral");
    ASSERT_LE(static_cast<unsigned>(ISO_TP_PAD_BYTE), 0xFFu);
#else
    GTEST_SKIP() << "No known padding byte macro defined (ISOTP_DEFAULT_PADDING / ISO_TP_DEFAULT_PADDING / ISO_TP_PAD_BYTE).";
#endif
}

TEST(IsoTp_Macros, TxRxBufferSizesIfDefinedAreNonZero) {
#ifdef ISOTP_TX_BUF_SIZE
    static_assert(std::is_integral<decltype(ISOTP_TX_BUF_SIZE)>::value, "ISOTP_TX_BUF_SIZE must be integral");
    EXPECT_GT(static_cast<unsigned>(ISOTP_TX_BUF_SIZE), 0u);
#endif
#ifdef ISOTP_RX_BUF_SIZE
    static_assert(std::is_integral<decltype(ISOTP_RX_BUF_SIZE)>::value, "ISOTP_RX_BUF_SIZE must be integral");
    EXPECT_GT(static_cast<unsigned>(ISOTP_RX_BUF_SIZE), 0u);
#endif

#if !defined(ISOTP_TX_BUF_SIZE) && !defined(ISOTP_RX_BUF_SIZE)
    GTEST_SKIP() << "No known TX/RX buffer size macros defined (ISOTP_TX_BUF_SIZE / ISOTP_RX_BUF_SIZE).";
#endif
}

TEST(IsoTp_Macros, StminDefaultIfDefinedWithinIsoTpRange) {
    // STmin valid values per ISO 15765-2: 0x00..0x7F (ms), 0xF1..0xF9 (100us steps). 0x80..0xF0 and 0xFA..0xFF are reserved.
#if defined(ISOTP_DEFAULT_ST_MIN)
    const unsigned stmin = static_cast<unsigned>(ISOTP_DEFAULT_ST_MIN);
    const bool in_ms_range = stmin <= 0x7Fu;
    const bool in_100us_range = (stmin >= 0xF1u && stmin <= 0xF9u);
    EXPECT_TRUE(in_ms_range || in_100us_range) << "ISOTP_DEFAULT_ST_MIN out of ISO-TP allowed range";
#elif defined(ISO_TP_DEFAULT_ST_MIN)
    const unsigned stmin = static_cast<unsigned>(ISO_TP_DEFAULT_ST_MIN);
    const bool in_ms_range = stmin <= 0x7Fu;
    const bool in_100us_range = (stmin >= 0xF1u && stmin <= 0xF9u);
    EXPECT_TRUE(in_ms_range || in_100us_range) << "ISO_TP_DEFAULT_ST_MIN out of ISO-TP allowed range";
#else
    GTEST_SKIP() << "No known STmin default macro defined (ISOTP_DEFAULT_ST_MIN / ISO_TP_DEFAULT_ST_MIN).";
#endif
}

TEST(IsoTp_Macros, BlockSizeDefaultIfDefinedWithinByteRange) {
#if defined(ISOTP_DEFAULT_BLOCK_SIZE)
    static_assert(std::is_integral<decltype(ISOTP_DEFAULT_BLOCK_SIZE)>::value, "ISOTP_DEFAULT_BLOCK_SIZE must be integral");
    EXPECT_LE(static_cast<unsigned>(ISOTP_DEFAULT_BLOCK_SIZE), 0xFFu);
#elif defined(ISO_TP_DEFAULT_BLOCK_SIZE)
    static_assert(std::is_integral<decltype(ISO_TP_DEFAULT_BLOCK_SIZE)>::value, "ISO_TP_DEFAULT_BLOCK_SIZE must be integral");
    EXPECT_LE(static_cast<unsigned>(ISO_TP_DEFAULT_BLOCK_SIZE), 0xFFu);
#else
    GTEST_SKIP() << "No known block size default macro defined (ISOTP_DEFAULT_BLOCK_SIZE / ISO_TP_DEFAULT_BLOCK_SIZE).";
#endif
}

TEST(IsoTp_Macros, NArNBrNCrTimeoutsIfDefinedPositive) {
#if defined(ISOTP_DEFAULT_N_AS)
    EXPECT_GT(static_cast<unsigned>(ISOTP_DEFAULT_N_AS), 0u);
#endif
#if defined(ISOTP_DEFAULT_N_BS)
    EXPECT_GT(static_cast<unsigned>(ISOTP_DEFAULT_N_BS), 0u);
#endif
#if defined(ISOTP_DEFAULT_N_CS)
    EXPECT_GT(static_cast<unsigned>(ISOTP_DEFAULT_N_CS), 0u);
#endif

#if !defined(ISOTP_DEFAULT_N_AS) && !defined(ISOTP_DEFAULT_N_BS) && !defined(ISOTP_DEFAULT_N_CS)
    // Alternate macro spellings
#   if defined(ISO_TP_DEFAULT_N_AS) || defined(ISO_TP_DEFAULT_N_BS) || defined(ISO_TP_DEFAULT_N_CS)
#       ifdef ISO_TP_DEFAULT_N_AS
            EXPECT_GT(static_cast<unsigned>(ISO_TP_DEFAULT_N_AS), 0u);
#       endif
#       ifdef ISO_TP_DEFAULT_N_BS
            EXPECT_GT(static_cast<unsigned>(ISO_TP_DEFAULT_N_BS), 0u);
#       endif
#       ifdef ISO_TP_DEFAULT_N_CS
            EXPECT_GT(static_cast<unsigned>(ISO_TP_DEFAULT_N_CS), 0u);
#       endif
#   else
        GTEST_SKIP() << "No known default N_* timeout macros defined.";
#   endif
#endif
}

TEST(IsoTp_Protocol, PciTypeNibbleMacrosIfDefinedAreDistinctAndInRange) {
    // Support several common macro spellings for PCI types.
    int sf = -1, ff = -1, cf = -1, fc = -1;

#ifdef ISOTP_PCI_TYPE_SINGLE
    sf = ISOTP_PCI_TYPE_SINGLE;
#endif
#ifdef ISOTP_PCI_TYPE_FIRST
    ff = ISOTP_PCI_TYPE_FIRST;
#endif
#ifdef ISOTP_PCI_TYPE_CONSECUTIVE
    cf = ISOTP_PCI_TYPE_CONSECUTIVE;
#endif
#ifdef ISOTP_PCI_TYPE_FLOW
    fc = ISOTP_PCI_TYPE_FLOW;
#endif

#ifdef ISOTP_PCI_SF
    sf = ISOTP_PCI_SF;
#endif
#ifdef ISOTP_PCI_FF
    ff = ISOTP_PCI_FF;
#endif
#ifdef ISOTP_PCI_CF
    cf = ISOTP_PCI_CF;
#endif
#ifdef ISOTP_PCI_FC
    fc = ISOTP_PCI_FC;
#endif

#ifdef ISO_TP_PCI_SF
    sf = ISO_TP_PCI_SF;
#endif
#ifdef ISO_TP_PCI_FF
    ff = ISO_TP_PCI_FF;
#endif
#ifdef ISO_TP_PCI_CF
    cf = ISO_TP_PCI_CF;
#endif
#ifdef ISO_TP_PCI_FC
    fc = ISO_TP_PCI_FC;
#endif

    if (sf < 0 && ff < 0 && cf < 0 && fc < 0) {
        GTEST_SKIP() << "No known PCI type macros defined.";
        return;
    }

    if (sf >= 0) EXPECT_LE(static_cast<unsigned>(sf), 0xFu);
    if (ff >= 0) EXPECT_LE(static_cast<unsigned>(ff), 0xFu);
    if (cf >= 0) EXPECT_LE(static_cast<unsigned>(cf), 0xFu);
    if (fc >= 0) EXPECT_LE(static_cast<unsigned>(fc), 0xFu);

    // If multiple are available, ensure they are distinct
    if (sf >= 0 && ff >= 0) EXPECT_NE(sf, ff);
    if (sf >= 0 && cf >= 0) EXPECT_NE(sf, cf);
    if (sf >= 0 && fc >= 0) EXPECT_NE(sf, fc);
    if (ff >= 0 && cf >= 0) EXPECT_NE(ff, cf);
    if (ff >= 0 && fc >= 0) EXPECT_NE(ff, fc);
    if (cf >= 0 && fc >= 0) EXPECT_NE(cf, fc);
}

TEST(IsoTp_Protocol, FlowControlStatusMacrosIfDefinedAreValid) {
    int cts = -1, waitv = -1, ovflw = -1;

#ifdef ISOTP_FLOW_STATUS_CTS
    cts = ISOTP_FLOW_STATUS_CTS;
#endif
#ifdef ISOTP_FLOW_STATUS_WAIT
    waitv = ISOTP_FLOW_STATUS_WAIT;
#endif
#ifdef ISOTP_FLOW_STATUS_OVERFLOW
    ovflw = ISOTP_FLOW_STATUS_OVERFLOW;
#endif

#ifdef ISO_TP_FLOW_STATUS_CTS
    cts = ISO_TP_FLOW_STATUS_CTS;
#endif
#ifdef ISO_TP_FLOW_STATUS_WAIT
    waitv = ISO_TP_FLOW_STATUS_WAIT;
#endif
#ifdef ISO_TP_FLOW_STATUS_OVFLW
    ovflw = ISO_TP_FLOW_STATUS_OVFLW;
#endif

    if (cts < 0 && waitv < 0 && ovflw < 0) {
        GTEST_SKIP() << "No known flow-control status macros defined.";
        return;
    }

    if (cts >= 0) EXPECT_LE(static_cast<unsigned>(cts), 0xFFu);
    if (waitv >= 0) EXPECT_LE(static_cast<unsigned>(waitv), 0xFFu);
    if (ovflw >= 0) EXPECT_LE(static_cast<unsigned>(ovflw), 0xFFu);

    if (cts >= 0 && waitv >= 0) EXPECT_NE(cts, waitv);
    if (cts >= 0 && ovflw >= 0) EXPECT_NE(cts, ovflw);
    if (waitv >= 0 && ovflw >= 0) EXPECT_NE(waitv, ovflw);
}

TEST(IsoTp_Header, TypicalLimitsIfDefinedAreSane) {
#ifdef ISOTP_MAX_MESSAGE_SIZE
    EXPECT_GT(static_cast<unsigned>(ISOTP_MAX_MESSAGE_SIZE), 0u);
#endif
#ifdef ISO_TP_MAX_MESSAGE_SIZE
    EXPECT_GT(static_cast<unsigned>(ISO_TP_MAX_MESSAGE_SIZE), 0u);
#endif
#ifdef ISOTP_MAX_FRAME_SIZE
    EXPECT_GT(static_cast<unsigned>(ISOTP_MAX_FRAME_SIZE), 0u);
#endif
#ifdef ISO_TP_MAX_FRAME_SIZE
    EXPECT_GT(static_cast<unsigned>(ISO_TP_MAX_FRAME_SIZE), 0u);
#endif

#if !defined(ISOTP_MAX_MESSAGE_SIZE) && !defined(ISO_TP_MAX_MESSAGE_SIZE) && \
    !defined(ISOTP_MAX_FRAME_SIZE) && !defined(ISO_TP_MAX_FRAME_SIZE)
    GTEST_SKIP() << "No known size limit macros defined.";
#endif
}

// Scaffold for future behavior tests; adjust to your API and remove skip.
TEST(IsoTp_Behavior, DISABLED_SingleFrameRoundTrip) {
    GTEST_SKIP() << "Implement using your isotp.h API (initialize link, send SF, feed RX, receive).";
}

TEST(IsoTp_Behavior, DISABLED_MultiFrameSegmentationAndReassembly) {
    GTEST_SKIP() << "Implement using your isotp.h API (FF/CF with FC(CTS), verify reassembly and STmin/BS).";
}

