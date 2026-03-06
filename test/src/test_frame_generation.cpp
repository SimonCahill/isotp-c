/**
 * @file test_frame_generation.cpp
 * @author Simon Cahill (contact@simonc.eu)
 * @brief Contains unit tests for testing frame generation.
 * @version 0.1
 * @date 2025-08-25
 * * @copyright Copyright (c) 2025 Simon Cahill and Contributors.
 */

#include <gtest/gtest.h>

#include <cstring>
#include <memory>
#include <vector>

// ---- isotp-c (C) ----
extern "C" {
#include "isotp.h"
#include "isotp_config.h"
#include "isotp_defines.h"
}

// -------------------------------
// Test Fixture
// -------------------------------
class IsotpTestFixture : public ::testing::Test {
    public:
        struct TxFrame {
            uint32_t id{};
            uint8_t data[8]{};
            uint8_t len{};
        };

        std::vector<TxFrame> tx_frames;
        uint64_t fake_us = 0;

        // Helpers
        inline void TickUs(uint32_t us) {
            fake_us += us;
        }

        inline void Poll(IsoTpLink* link, uint32_t steps, uint32_t step_us) {
            for (uint32_t i = 0; i < steps; ++i) {
                isotp_poll(link);
                TickUs(step_us);
            }
        }

    protected:

        void SetUp() override {
            // Reset state for each test
            tx_frames.clear();
            fake_us = 0;
        }

        void TearDown() override {
            // No cleanup needed
        }
};

// -------------------------------
// Required user shims (C linkage)
// -------------------------------
extern "C" {

// Shims need to access the test fixture's state.
// Since shims are C-style functions, we can't make them class members directly.
// We use a thread-local pointer to the current fixture instance.
static thread_local IsotpTestFixture* s_currentFixture = nullptr;

// Capture a raw CAN frame sent by isotp-c
int isotp_user_send_can(const uint32_t arbitration_id, const uint8_t* data, const uint8_t size) {
    if (!s_currentFixture) {
        return ISOTP_RET_ERROR; // Should not happen in a properly set-up test
    }
    if (s_currentFixture->tx_frames.size() >= 4096) {
        return ISOTP_RET_NOSPACE;
    }

    IsotpTestFixture::TxFrame f{};
    f.id = arbitration_id;
    f.len = size > 8 ? 8 : size;
    std::memcpy(f.data, data, f.len);
    s_currentFixture->tx_frames.push_back(f);
    return ISOTP_RET_OK;
}

// Monotonic microsecond clock
uint32_t isotp_user_get_us(void) {
    if (!s_currentFixture) {
        return 0; // Or handle as an error
    }
    return static_cast<uint32_t>(s_currentFixture->fake_us & 0xFFFFFFFFu);
}

// Optional debug (kept quiet to avoid noisy test logs)
void isotp_user_debug(const char* /*fmt*/, ...) {
    // Uncomment for troubleshooting:
    // va_list ap; va_start(ap, fmt); vfprintf(stderr, fmt, ap); fputc('\n', stderr); va_end(ap);
}

} // extern "C"

// -------------------------------
// Frame builders (PCI helpers)
// -------------------------------
static void MakeSF(uint8_t out[8], const uint8_t* payload, uint8_t len) {
    std::memset(out, 0, 8);
    out[0] = 0x00 | (len & 0x0F);
    std::memcpy(&out[1], payload, len);
}

static void MakeFF(uint8_t out[8], uint16_t total_len, const uint8_t* first6) {
    std::memset(out, 0, 8);
    out[0] = 0x10 | ((total_len >> 8) & 0x0F);
    out[1] = static_cast<uint8_t>(total_len & 0xFF);
    std::memcpy(&out[2], first6, 6);
}

static void MakeCF(uint8_t out[8], uint8_t sn, const uint8_t* chunk, uint8_t chunk_len) {
    std::memset(out, 0, 8);
    out[0] = 0x20 | (sn & 0x0F);
    std::memcpy(&out[1], chunk, chunk_len);
}

static void MakeFC_CTS(uint8_t out[8], uint8_t bs, uint8_t stmin) {
    std::memset(out, 0, 8);
    out[0] = 0x30;
    out[1] = bs;
    out[2] = stmin;
}

// ============================================================
// Tests
// ============================================================

TEST_F(IsotpTestFixture, Receive_SingleFrame) {
    s_currentFixture = this; // Set the fixture pointer for this test

    static IsoTpLink link{};
    static uint8_t recvbuf[64]{};
    static uint8_t sendbuf[64]{};

    isotp_init_link(&link, 0x700u, sendbuf, sizeof(sendbuf), recvbuf, sizeof(recvbuf));
    link.receive_arbitration_id = 0x701u;

    const uint8_t payload[3] = {0x11, 0x22, 0x33};
    uint8_t sf[8];
    MakeSF(sf, payload, 3);

    isotp_on_can_message(&link, sf, 1 + 3);

    uint8_t out[8]{};
    uint32_t out_size = 0;
    int ret = isotp_receive(&link, out, sizeof(out), &out_size);
    ASSERT_EQ(ISOTP_RET_OK, ret);
    EXPECT_EQ(3u, out_size);
    EXPECT_EQ(0, std::memcmp(payload, out, 3));
    EXPECT_TRUE(tx_frames.empty());
}

TEST_F(IsotpTestFixture, Receive_MultiFrame_SendsFC_And_Reassembles) {
    s_currentFixture = this;

    static IsoTpLink link{};
    static uint8_t recvbuf[128]{};
    static uint8_t sendbuf[64]{};

    isotp_init_link(&link, 0x700u, sendbuf, sizeof(sendbuf), recvbuf, sizeof(recvbuf));
    link.receive_arbitration_id = 0x701u;

    const uint8_t msg[12] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0xAA, 0xBB};
    uint8_t ff[8];
    MakeFF(ff, sizeof(msg), &msg[0]);
    isotp_on_can_message(&link, ff, 8);

    ASSERT_EQ(1u, tx_frames.size());
    EXPECT_EQ(0x30, tx_frames[0].data[0]);
    EXPECT_EQ(ISO_TP_DEFAULT_BLOCK_SIZE, tx_frames[0].data[1]);
    EXPECT_GE(tx_frames[0].len, 3);

    uint8_t cf1[8];
    MakeCF(cf1, 0x1, &msg[6], 6);
    isotp_on_can_message(&link, cf1, 1 + 6);

    uint8_t out[16]{};
    uint32_t out_size = 0;
    int ret = isotp_receive(&link, out, sizeof(out), &out_size);
    ASSERT_EQ(ISOTP_RET_OK, ret);
    EXPECT_EQ(sizeof(msg), out_size);
    EXPECT_EQ(0, std::memcmp(msg, out, sizeof(msg)));
}

TEST_F(IsotpTestFixture, Send_SingleFrame) {
    s_currentFixture = this;

    static IsoTpLink link{};
    static uint8_t recvbuf[32]{};
    static uint8_t sendbuf[32]{};

    isotp_init_link(&link, 0x700u, sendbuf, sizeof(sendbuf), recvbuf, sizeof(recvbuf));

    const uint8_t payload[4] = {0xC0, 0xFF, 0xEE, 0x00};
    int ret = isotp_send(&link, payload, 4);
    ASSERT_EQ(ISOTP_RET_OK, ret);

    ASSERT_EQ(1u, tx_frames.size());
    EXPECT_EQ(0x700u, tx_frames[0].id);
    ASSERT_EQ(4, tx_frames[0].len);
    EXPECT_EQ((0x00 | sizeof(payload)), tx_frames[0].data[0]);
    EXPECT_EQ(0, std::memcmp(payload, &tx_frames[0].data[1], sizeof(payload)));
}

TEST_F(IsotpTestFixture, Send_MultiFrame_RespectsFC_And_STmin) {
    s_currentFixture = this;

    static IsoTpLink link{};
    static uint8_t recvbuf[256]{};
    static uint8_t sendbuf[256]{};

    isotp_init_link(&link, 0x700u, sendbuf, sizeof(sendbuf), recvbuf, sizeof(recvbuf));

    uint8_t msg[20];
    for (uint8_t i = 0; i < sizeof(msg); ++i) msg[i] = static_cast<uint8_t>(0xA0 + i);

    ASSERT_EQ(ISOTP_RET_OK, isotp_send(&link, msg, sizeof(msg)));

    ASSERT_GE(tx_frames.size(), 1u);
    EXPECT_EQ(0x700u, tx_frames[0].id);
    EXPECT_EQ(8u, tx_frames[0].len);
    EXPECT_EQ(0x10, tx_frames[0].data[0] & 0xF0);
    uint16_t len_from_ff = static_cast<uint16_t>(((tx_frames[0].data[0] & 0x0F) << 8) | tx_frames[0].data[1]);
    EXPECT_EQ(sizeof(msg), len_from_ff);
    EXPECT_EQ(0, std::memcmp(&msg[0], &tx_frames[0].data[2], 6));

    uint8_t fc[8];
    MakeFC_CTS(fc, ISO_TP_DEFAULT_BLOCK_SIZE, 0x00);
    isotp_on_can_message(&link, fc, 3);

    Poll(&link, 20, 100);

    ASSERT_GE(tx_frames.size(), 3u);
    EXPECT_EQ(0x20, tx_frames[1].data[0] & 0xF0);
    EXPECT_EQ(0x01, tx_frames[1].data[0] & 0x0F);
    EXPECT_EQ(0, std::memcmp(&msg[6], &tx_frames[1].data[1], 7));

    EXPECT_EQ(0x20, tx_frames[2].data[0] & 0xF0);
    EXPECT_EQ(0x02, tx_frames[2].data[0] & 0x0F);
    EXPECT_EQ(0, std::memcmp(&msg[13], &tx_frames[2].data[1], 7));
}

TEST_F(IsotpTestFixture, Loopback_EndToEnd_SendToRxLink) {
    s_currentFixture = this;

    static IsoTpLink tx{};
    static IsoTpLink rx{};
    static uint8_t tx_sendbuf[128]{}, tx_recvbuf[128]{};
    static uint8_t rx_sendbuf[128]{}, rx_recvbuf[128]{};

    isotp_init_link(&tx, 0x700u, tx_sendbuf, sizeof(tx_sendbuf), tx_recvbuf, sizeof(tx_recvbuf));
    isotp_init_link(&rx, 0x701u, rx_sendbuf, sizeof(rx_sendbuf), rx_recvbuf, sizeof(rx_recvbuf));

    uint8_t payload[15];
    for (uint8_t i = 0; i < sizeof(payload); ++i) payload[i] = static_cast<uint8_t>(i + 1);

    ASSERT_EQ(ISOTP_RET_OK, isotp_send(&tx, payload, sizeof(payload)));

    ASSERT_FALSE(tx_frames.empty());
    EXPECT_EQ(0x10, tx_frames[0].data[0] & 0xF0);

    uint8_t fc[8];
    MakeFC_CTS(fc, ISO_TP_DEFAULT_BLOCK_SIZE, 0x00);
    isotp_on_can_message(&tx, fc, 3);

    Poll(&tx, 20, 100);

    for (const auto& f : tx_frames) {
        isotp_on_can_message(&rx, f.data, f.len);
    }

    uint8_t out[32]{};
    uint32_t out_size = 0;
    ASSERT_EQ(ISOTP_RET_OK, isotp_receive(&rx, out, sizeof(out), &out_size));
    EXPECT_EQ(sizeof(payload), out_size);
    EXPECT_EQ(0, std::memcmp(payload, out, sizeof(payload)));
}