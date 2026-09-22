////////////////////////////////////////////////////////////////////////
//                  ___ ___  ___ _____ ___      ___                   //
//                 |_ _/ __|/ _ \_   _| _ \___ / __|                  //
//                  | |\__ \ (_) || | |  _/___| (__                   //
//                 |___|___/\___/ |_| |_|      \___|                  //
//                                                                    //
//                     _____ ___ ___ _____ ___                        //
//                    |_   _| __/ __|_   _/ __|                       //
//                      | | | _|\__ \ | | \__ \                       //
//                      |_| |___|___/ |_| |___/                       //
//                                                                    //
////////////////////////////////////////////////////////////////////////

/**
 * Self-contained test suite for isotp-c. The suite is compiled once per
 * configuration (see CMakeLists.txt), so the expectations below adapt to the
 * configured frame size and padding behaviour.
 */

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#ifdef isotpc_USE_INCLUDE_DIR
    #include "isotp_c/isotp.h"
#else
    #include "isotp.h"
#endif // isotpc_USE_INCLUDE_DIR
#include "mocks/isotp_user_mock.hpp"

#define TEST_MAX_FRAMES 1024
#define TEST_BUFFER_SIZE 5000

#define TEST_TX_ID 0x123
#define TEST_RX_ID 0x456
#define TEST_FUNCTIONAL_ID 0x7DF

using test_frame_t = struct {
        uint32_t m_id;
        uint8_t  m_data[ISO_TP_MAX_CAN_FRAME_SIZE];
        uint8_t  m_len;
        uint8_t  m_flags;
};

static std::array<test_frame_t, TEST_MAX_FRAMES> gFrames{};
static uint32_t                                  gFrameCount;
static uint32_t                                  gTimeUs;
static int32_t                                   gSendResult;
static std::string                               gLastDebugMessage;

#ifdef ISO_TP_USER_SEND_CAN_ARG
/* the address of this object is handed to the library and expected back unmodified */
static int32_t gCanArgMarker;
#endif

static int capture_can_frame(uint32_t arbitrationId, const uint8_t* data, uint8_t size, uint8_t flags, void* argument) {
#ifdef ISO_TP_USER_SEND_CAN_ARG
    EXPECT_EQ(argument, &gCanArgMarker);
#else
    EXPECT_EQ(argument, nullptr);
#endif

    EXPECT_LT(gFrameCount, TEST_MAX_FRAMES);
    EXPECT_LE(size, ISO_TP_MAX_CAN_FRAME_SIZE);
    if (gFrameCount >= TEST_MAX_FRAMES || size > ISO_TP_MAX_CAN_FRAME_SIZE) { return ISOTP_RET_ERROR; }

    gFrames[gFrameCount].m_id  = arbitrationId;
    gFrames[gFrameCount].m_len = size;
    std::memcpy(gFrames[gFrameCount].m_data, data, size);
    gFrames[gFrameCount].m_flags = flags;

#ifdef ISO_TP_USER_SEND_CAN_FLAGS
    if (size > 8) { EXPECT_NE(flags & ISOTP_CAN_FRAME_FLAG_FD, 0); }
#endif

    ++gFrameCount;
    return gSendResult;
}

/* initialises a link, attaching the user CAN argument if the library was built with it */
static void test_init_link(IsoTpLink* link, uint32_t sendId, uint8_t* sendBuffer, uint32_t sendBufferSize, uint8_t* receiveBuffer,
                           uint32_t receiveBufferSize) {
    isotp_init_link(link, sendId, sendBuffer, sendBufferSize, receiveBuffer, receiveBufferSize);

#ifdef ISO_TP_USER_SEND_CAN_ARG
    link->user_send_can_arg = &gCanArgMarker;
#endif
}

static void reset_bus(void) {
    gFrameCount = 0;
    gTimeUs     = 0;
    gSendResult = ISOTP_RET_OK;
    gFrames.fill({});
    gLastDebugMessage.clear();
}

class FramingTest: public testing::Test {
    protected:
        void SetUp() override {
            reset_bus();
            isotp_set_user_mock(&m_user);
            ON_CALL(m_user, get_us()).WillByDefault([] { return gTimeUs; });
            ON_CALL(m_user, send_can(testing::_, testing::_, testing::_, testing::_, testing::_)).WillByDefault(capture_can_frame);
            ON_CALL(m_user, debug(testing::_)).WillByDefault([](const std::string_view message) { gLastDebugMessage = message; });
        }

        void TearDown() override { isotp_set_user_mock(nullptr); }

        testing::NiceMock<IsoTpUserMock> m_user;
};

/* the CAN_DL a frame carrying used bytes of payload is expected to be sent with */
static uint8_t expected_can_dl(uint8_t used) {
    static const std::array<uint8_t, 7> CAN_FD_SIZES{12, 16, 20, 24, 32, 48, 64};

#ifdef ISO_TP_FRAME_PADDING
    if (used < 8) { used = 8; }
#endif

    if (used <= 8) { return used; }

    for (const auto SIZE : CAN_FD_SIZES) {
        if (used <= SIZE) { return SIZE; }
    }

    return 64;
}

/* verifies that all bytes beyond the payload of a frame contain the padding value */
static void check_padding(const test_frame_t* frame, uint8_t used) {
    for (uint8_t i = used; i < frame->m_len; ++i) { EXPECT_EQ(frame->m_data[i], ISO_TP_FRAME_PADDING_VALUE); }
}

static void fill_pattern(uint8_t* buffer, uint32_t size) {
    for (uint32_t i = 0; i < size; ++i) { buffer[i] = (uint8_t)(i * 7u + 1u); }
}

/* hands a frame to a link, as a CAN driver would */
static void deliver(IsoTpLink* link, const uint8_t* data, uint8_t len) { isotp_on_can_message(link, data, len); }

/* delivers a flow control frame permitting the sender to continue */
static void deliver_flow_control(IsoTpLink* link, uint8_t flowStatus, uint8_t blockSize, uint8_t stMin) {
    const std::array<uint8_t, 3> FRAME{(uint8_t)(0x30u | flowStatus), blockSize, stMin};
    deliver(link, FRAME.data(), sizeof(FRAME));
}

/*
 * Runs a complete transmission between two links, routing every frame emitted
 * by one link to the other one.
 */
static void pump(IsoTpLink* sender, IsoTpLink* receiver) {
    uint32_t nextFrame = 0;
    uint32_t iterations = 0;

    while (iterations++ < 100000u) {
        while (nextFrame < gFrameCount) {
            const test_frame_t FRAME = gFrames[nextFrame++];

            if (FRAME.m_id == TEST_TX_ID) {
                deliver(receiver, FRAME.m_data, FRAME.m_len);
            } else {
                deliver(sender, FRAME.m_data, FRAME.m_len);
            }
        }

        isotp_poll(sender);
        isotp_poll(receiver);
        gTimeUs += 10;

        if (nextFrame >= gFrameCount && ISOTP_SEND_STATUS_INPROGRESS != sender->send_status) { break; }
    }
}

///////////////////////////////////////////////////////
///                      TESTS                      ///
///////////////////////////////////////////////////////

static std::array<uint8_t, TEST_BUFFER_SIZE> gSendBuffer{};
static std::array<uint8_t, TEST_BUFFER_SIZE> gReceiveBuffer{};
static std::array<uint8_t, TEST_BUFFER_SIZE> gPeerSendBuffer{};
static std::array<uint8_t, TEST_BUFFER_SIZE> gPeerReceiveBuffer{};
static std::array<uint8_t, TEST_BUFFER_SIZE> gPayload{};
static std::array<uint8_t, TEST_BUFFER_SIZE> gReceived{};

static void                                  init_links(IsoTpLink* sender, IsoTpLink* receiver, uint8_t txDl) {
    reset_bus();

    test_init_link(sender, TEST_TX_ID, gSendBuffer.data(), sizeof(gSendBuffer), gReceiveBuffer.data(), sizeof(gReceiveBuffer));
    test_init_link(receiver, TEST_RX_ID, gPeerSendBuffer.data(), sizeof(gPeerSendBuffer), gPeerReceiveBuffer.data(), sizeof(gPeerReceiveBuffer));

    EXPECT_EQ(isotp_set_tx_dl(sender, txDl), ISOTP_RET_OK);
    EXPECT_EQ(isotp_set_tx_dl(receiver, txDl), ISOTP_RET_OK);
}

static void test_default_tx_dl(void) {
    IsoTpLink link;

    printf("test_default_tx_dl\n");
    reset_bus();
    test_init_link(&link, TEST_TX_ID, gSendBuffer.data(), sizeof(gSendBuffer), gReceiveBuffer.data(), sizeof(gReceiveBuffer));

    EXPECT_EQ(isotp_get_tx_dl(&link), ISO_TP_DEFAULT_TX_DL);
    EXPECT_EQ(isotp_get_tx_dl(nullptr), 0);

    /* links which were not initialised through isotp_init_link() fall back to Classical CAN */
    memset(&link, 0, sizeof(link));
    EXPECT_EQ(isotp_get_tx_dl(&link), 8);
}

static void test_set_tx_dl(void) {
    IsoTpLink link;

    printf("test_set_tx_dl\n");
    reset_bus();
    test_init_link(&link, TEST_TX_ID, gSendBuffer.data(), sizeof(gSendBuffer), gReceiveBuffer.data(), sizeof(gReceiveBuffer));

    EXPECT_EQ(isotp_set_tx_dl(nullptr, 8), ISOTP_RET_ERROR);
    EXPECT_EQ(isotp_set_tx_dl(&link, 8), ISOTP_RET_OK);
    EXPECT_EQ(isotp_get_tx_dl(&link), 8);

    /* lengths which cannot be transmitted by a CAN(-FD) controller */
    EXPECT_EQ(isotp_set_tx_dl(&link, 0), ISOTP_RET_ERROR);
    EXPECT_EQ(isotp_set_tx_dl(&link, 7), ISOTP_RET_ERROR);
    EXPECT_EQ(isotp_set_tx_dl(&link, 9), ISOTP_RET_ERROR);
    EXPECT_EQ(isotp_set_tx_dl(&link, 63), ISOTP_RET_ERROR);
    EXPECT_EQ(isotp_set_tx_dl(&link, 255), ISOTP_RET_ERROR);

#if ISO_TP_MAX_CAN_FRAME_SIZE >= 64
    EXPECT_EQ(isotp_set_tx_dl(&link, 12), ISOTP_RET_OK);
    EXPECT_EQ(isotp_set_tx_dl(&link, 64), ISOTP_RET_OK);
    EXPECT_EQ(isotp_get_tx_dl(&link), 64);
#else
    /* frame sizes exceeding the compile time maximum are rejected */
    EXPECT_EQ(isotp_set_tx_dl(&link, 12), ISOTP_RET_ERROR);
    EXPECT_EQ(isotp_set_tx_dl(&link, 64), ISOTP_RET_ERROR);
#endif

    /* TX_DL must not change while a message is being transmitted */
    EXPECT_EQ(isotp_set_tx_dl(&link, 8), ISOTP_RET_OK);
    fill_pattern(gPayload.data(), 20);
    EXPECT_EQ(isotp_send(&link, gPayload.data(), 20), ISOTP_RET_OK);
    EXPECT_EQ(isotp_set_tx_dl(&link, 8), ISOTP_RET_INPROGRESS);
}

static void test_classic_single_frame(void) {
    IsoTpLink link;

    printf("test_classic_single_frame\n");
    reset_bus();
    test_init_link(&link, TEST_TX_ID, gSendBuffer.data(), sizeof(gSendBuffer), gReceiveBuffer.data(), sizeof(gReceiveBuffer));
    EXPECT_EQ(isotp_set_tx_dl(&link, 8), ISOTP_RET_OK);

    fill_pattern(gPayload.data(), 7);
    EXPECT_EQ(isotp_send(&link, gPayload.data(), 7), ISOTP_RET_OK);

    EXPECT_EQ(gFrameCount, 1);
    EXPECT_EQ(gFrames[0].m_id, TEST_TX_ID);
    EXPECT_EQ(gFrames[0].m_len, 8);
    EXPECT_EQ(gFrames[0].m_data[0], 0x07);
    EXPECT_EQ(memcmp(&gFrames[0].m_data[1], gPayload.data(), 7), 0);

    /* a shorter payload is only padded if padding is enabled */
    reset_bus();
    fill_pattern(gPayload.data(), 3);
    EXPECT_EQ(isotp_send(&link, gPayload.data(), 3), ISOTP_RET_OK);
    EXPECT_EQ(gFrameCount, 1);
    #ifndef ISO_TP_FRAME_PADDING
    EXPECT_EQ(gFrames[0].m_len, expected_can_dl(4));
    #else
    
    #endif // ISO_TP_FRAME_PADDING
    EXPECT_EQ(gFrames[0].m_data[0], 0x03);
    EXPECT_EQ(memcmp(&gFrames[0].m_data[1], gPayload.data(), 3), 0);
    check_padding(&gFrames[0], 4);
}

static void test_classic_multi_frame(void) {
    IsoTpLink link;

    printf("test_classic_multi_frame\n");
    reset_bus();
    test_init_link(&link, TEST_TX_ID, gSendBuffer.data(), sizeof(gSendBuffer), gReceiveBuffer.data(), sizeof(gReceiveBuffer));
    EXPECT_EQ(isotp_set_tx_dl(&link, 8), ISOTP_RET_OK);

    fill_pattern(gPayload.data(), 20);
    EXPECT_EQ(isotp_send(&link, gPayload.data(), 20), ISOTP_RET_OK);

    /* first frame: 0x1, FF_DL = 20, followed by 6 data bytes */
    EXPECT_EQ(gFrameCount, 1);
    EXPECT_EQ(gFrames[0].m_len, 8);
    EXPECT_EQ(gFrames[0].m_data[0], 0x10);
    EXPECT_EQ(gFrames[0].m_data[1], 20);
    EXPECT_EQ(memcmp(&gFrames[0].m_data[2], gPayload.data(), 6), 0);

    /* consecutive frames are only sent once flow control was received */
    isotp_poll(&link);
    EXPECT_EQ(gFrameCount, 1);

    deliver_flow_control(&link, PCI_FLOW_STATUS_CONTINUE, 0, 0);
    isotp_poll(&link);
    EXPECT_EQ(gFrameCount, 2);
    EXPECT_EQ(gFrames[1].m_len, 8);
    EXPECT_EQ(gFrames[1].m_data[0], 0x21);
    EXPECT_EQ(memcmp(&gFrames[1].m_data[1], &gPayload[6], 7), 0);

    isotp_poll(&link);
    EXPECT_EQ(gFrameCount, 3);
    EXPECT_EQ(gFrames[2].m_len, 8);
    EXPECT_EQ(gFrames[2].m_data[0], 0x22);
    EXPECT_EQ(memcmp(&gFrames[2].m_data[1], &gPayload[13], 7), 0);
    EXPECT_EQ(link.send_status, ISOTP_SEND_STATUS_IDLE);
}

static void test_classic_receive(void) {
    IsoTpLink link;
    uint32_t  outSize = 0;

    printf("test_classic_receive\n");
    reset_bus();
    test_init_link(&link, TEST_RX_ID, gSendBuffer.data(), sizeof(gSendBuffer), gReceiveBuffer.data(), sizeof(gReceiveBuffer));

    /* single frame */
    const uint8_t SINGLE_FRAME[8] = {0x04, 0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x00, 0x00};
    deliver(&link, SINGLE_FRAME, sizeof(SINGLE_FRAME));
    EXPECT_EQ(isotp_receive(&link, gReceived.data(), sizeof(gReceived), &outSize), ISOTP_RET_OK);
    EXPECT_EQ(outSize, 4);
    EXPECT_EQ(memcmp(gReceived.data(), &SINGLE_FRAME[1], 4), 0);

    /* multi frame: 10 bytes spread across a first and a consecutive frame */
    const uint8_t FIRST_FRAME[8]      = {0x10, 0x0A, 1, 2, 3, 4, 5, 6};
    const uint8_t CONSECUTIVE_FRAME[] = {0x21, 7, 8, 9, 10};
    const uint8_t EXPECTED[10]        = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};

    reset_bus();
    deliver(&link, FIRST_FRAME, sizeof(FIRST_FRAME));

    /* the receiver answers a first frame with flow control */
    EXPECT_EQ(gFrameCount, 1);
    EXPECT_EQ(gFrames[0].m_id, TEST_RX_ID);
    EXPECT_EQ(gFrames[0].m_len, expected_can_dl(3));
    EXPECT_EQ(gFrames[0].m_data[0], 0x30);
    EXPECT_EQ(gFrames[0].m_data[1], ISO_TP_DEFAULT_BLOCK_SIZE);
    check_padding(&gFrames[0], 3);

    deliver(&link, CONSECUTIVE_FRAME, sizeof(CONSECUTIVE_FRAME));
    EXPECT_EQ(isotp_receive(&link, gReceived.data(), sizeof(gReceived), &outSize), ISOTP_RET_OK);
    EXPECT_EQ(outSize, 10);
    EXPECT_EQ(memcmp(gReceived.data(), EXPECTED, sizeof(EXPECTED)), 0);
}

static void test_receive_rejects_invalid_frames(void) {
    IsoTpLink link;
    uint32_t  outSize = 0;

    printf("test_receive_rejects_invalid_frames\n");
    reset_bus();
    test_init_link(&link, TEST_RX_ID, gSendBuffer.data(), sizeof(gSendBuffer), gReceiveBuffer.data(), 16);

    /* single frame announcing more data than the frame contains */
    const uint8_t TRUNCATED_SINGLE_FRAME[3] = {0x07, 0x01, 0x02};
    deliver(&link, TRUNCATED_SINGLE_FRAME, sizeof(TRUNCATED_SINGLE_FRAME));
    EXPECT_EQ(isotp_receive(&link, gReceived.data(), sizeof(gReceived), &outSize), ISOTP_RET_NO_DATA);

    /* SF_DL of zero is only valid for CAN FD frames using the escape sequence */
    const uint8_t EMPTY_SINGLE_FRAME[8] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    deliver(&link, EMPTY_SINGLE_FRAME, sizeof(EMPTY_SINGLE_FRAME));
    EXPECT_EQ(isotp_receive(&link, gReceived.data(), sizeof(gReceived), &outSize), ISOTP_RET_NO_DATA);

    /* first frames must fill the frame of the sender */
    const uint8_t SHORT_FIRST_FRAME[7] = {0x10, 0x0A, 1, 2, 3, 4, 5};
    deliver(&link, SHORT_FIRST_FRAME, sizeof(SHORT_FIRST_FRAME));
    EXPECT_EQ(gFrameCount, 0);
    EXPECT_EQ(link.receive_status, ISOTP_RECEIVE_STATUS_IDLE);

    /* payloads which fit into a single frame must not be segmented */
    const uint8_t POINTLESS_FIRST_FRAME[8] = {0x10, 0x05, 1, 2, 3, 4, 5, 6};
    deliver(&link, POINTLESS_FIRST_FRAME, sizeof(POINTLESS_FIRST_FRAME));
    EXPECT_EQ(gFrameCount, 0);
    EXPECT_EQ(link.receive_status, ISOTP_RECEIVE_STATUS_IDLE);

    const uint8_t OVERSIZED_FIRST_FRAME[8] = {0x10, 0x64, 1, 2, 3, 4, 5, 6};
    deliver(&link, OVERSIZED_FIRST_FRAME, sizeof(OVERSIZED_FIRST_FRAME));
#ifndef ISO_TP_ENABLE_STREAMING
    /* messages exceeding the receive buffer are refused with a flow control overflow */
    EXPECT_EQ(gFrameCount, 1);
    EXPECT_EQ(gFrames[0].m_data[0], 0x32);
    EXPECT_EQ(link.receive_protocol_result, ISOTP_PROTOCOL_RESULT_BUFFER_OVFLW);
#else
    /* unless they are received in chunks, in which case reception continues */
    EXPECT_EQ(gFrameCount, 1);
    EXPECT_EQ(gFrames[0].m_data[0], 0x30);
    EXPECT_EQ(link.receive_status, ISOTP_RECEIVE_STATUS_INPROGRESS);
    EXPECT_EQ(link.receive_protocol_result, ISOTP_PROTOCOL_RESULT_OK);
#endif

    /* consecutive frames carrying an unexpected sequence number abort the message */
    reset_bus();
    const uint8_t FIRST_FRAME[8]    = {0x10, 0x0A, 1, 2, 3, 4, 5, 6};
    const uint8_t WRONG_SN_FRAME[5] = {0x22, 7, 8, 9, 10};
    deliver(&link, FIRST_FRAME, sizeof(FIRST_FRAME));
    deliver(&link, WRONG_SN_FRAME, sizeof(WRONG_SN_FRAME));
    EXPECT_EQ(link.receive_status, ISOTP_RECEIVE_STATUS_IDLE);
    EXPECT_EQ(link.receive_protocol_result, ISOTP_PROTOCOL_RESULT_WRONG_SN);

    /* consecutive frames must be full, unless they carry the end of the message */
    reset_bus();
    const uint8_t SHORT_CONSECUTIVE_FRAME[5] = {0x21, 7, 8, 9, 10};
    deliver(&link, FIRST_FRAME, sizeof(FIRST_FRAME));
    EXPECT_EQ(link.receive_status, ISOTP_RECEIVE_STATUS_INPROGRESS);
    deliver(&link, SHORT_CONSECUTIVE_FRAME, sizeof(SHORT_CONSECUTIVE_FRAME));
    EXPECT_EQ(link.receive_status, ISOTP_RECEIVE_STATUS_FULL);
    EXPECT_EQ(isotp_receive(&link, gReceived.data(), sizeof(gReceived), &outSize), ISOTP_RET_OK);
    EXPECT_EQ(outSize, 10);

    /* frames which are too short to carry protocol control information are ignored */
    reset_bus();
    const uint8_t RUNT_FRAME[1] = {0x02};
    deliver(&link, RUNT_FRAME, sizeof(RUNT_FRAME));
    EXPECT_EQ(gFrameCount, 0);
    EXPECT_EQ(isotp_receive(&link, gReceived.data(), sizeof(gReceived), &outSize), ISOTP_RET_NO_DATA);

    /* single frames which don't fit into the receive buffer are reported as overflow */
    reset_bus();
    test_init_link(&link, TEST_RX_ID, gSendBuffer.data(), sizeof(gSendBuffer), gReceiveBuffer.data(), 2);
    const uint8_t SINGLE_FRAME[8] = {0x04, 1, 2, 3, 4, 0, 0, 0};
    deliver(&link, SINGLE_FRAME, sizeof(SINGLE_FRAME));
    EXPECT_EQ(link.receive_protocol_result, ISOTP_PROTOCOL_RESULT_BUFFER_OVFLW);
    EXPECT_EQ(isotp_receive(&link, gReceived.data(), sizeof(gReceived), &outSize), ISOTP_RET_NO_DATA);
}

/* transmits a message from one link to another and verifies it arrives unchanged */
static void check_transfer(uint8_t txDl, uint32_t size) {
    IsoTpLink sender;
    IsoTpLink receiver;
    uint32_t  outSize = 0;

    init_links(&sender, &receiver, txDl);

    fill_pattern(gPayload.data(), size);
    EXPECT_EQ(isotp_send(&sender, gPayload.data(), size), ISOTP_RET_OK);

    pump(&sender, &receiver);

    EXPECT_EQ(sender.send_status, ISOTP_SEND_STATUS_IDLE);
    EXPECT_EQ(isotp_receive(&receiver, gReceived.data(), sizeof(gReceived), &outSize), ISOTP_RET_OK);
    EXPECT_EQ(outSize, size);

    ASSERT_EQ(outSize, size);
    EXPECT_EQ(memcmp(gReceived.data(), gPayload.data(), size), 0);
}

static void test_transfers(uint8_t txDl) {
    static const uint32_t SIZES[] = {1, 2, 6, 7, 8, 9, 10, 62, 63, 64, 100, 511, 4094, 4095, 4096, 4097, TEST_BUFFER_SIZE};

    printf("test_transfers (TX_DL %u)\n", (unsigned int)txDl);

    for (size_t i = 0; i < sizeof(SIZES) / sizeof(SIZES[0]); ++i) { check_transfer(txDl, SIZES[i]); }
}

static void test_long_first_frame(uint8_t txDl) {
    IsoTpLink      link;
    const uint32_t SIZE = 5000;

    printf("test_long_first_frame (TX_DL %u)\n", (unsigned int)txDl);
    reset_bus();
    test_init_link(&link, TEST_TX_ID, gSendBuffer.data(), sizeof(gSendBuffer), gReceiveBuffer.data(), sizeof(gReceiveBuffer));
    EXPECT_EQ(isotp_set_tx_dl(&link, txDl), ISOTP_RET_OK);

    fill_pattern(gPayload.data(), SIZE);
    EXPECT_EQ(isotp_send(&link, gPayload.data(), SIZE), ISOTP_RET_OK);

    /* messages of more than 4095 bytes use the escaped first frame format */
    EXPECT_EQ(gFrameCount, 1);
    EXPECT_EQ(gFrames[0].m_len, txDl);
    EXPECT_EQ(gFrames[0].m_data[0], 0x10);
    EXPECT_EQ(gFrames[0].m_data[1], 0x00);
    EXPECT_EQ(gFrames[0].m_data[2], (SIZE >> 24) & 0xFF);
    EXPECT_EQ(gFrames[0].m_data[3], (SIZE >> 16) & 0xFF);
    EXPECT_EQ(gFrames[0].m_data[4], (SIZE >> 8) & 0xFF);
    EXPECT_EQ(gFrames[0].m_data[5], SIZE & 0xFF);
    EXPECT_EQ(memcmp(&gFrames[0].m_data[6], gPayload.data(), (size_t)(txDl - 6)), 0);
}

/* a build which is not able to handle a frame of a given length must ignore it,
 * as a Classical CAN build does with the CAN FD frames of an FD capable peer
 */
static void test_oversized_frames_ignored(void) {
    IsoTpLink link;
    uint8_t   frame[ISO_TP_MAX_CAN_FRAME_SIZE + 4];
    uint32_t  outSize = 0;

    printf("test_oversized_frames_ignored\n");
    reset_bus();
    test_init_link(&link, TEST_RX_ID, gSendBuffer.data(), sizeof(gSendBuffer), gReceiveBuffer.data(), sizeof(gReceiveBuffer));

    memset(frame, 0x55, sizeof(frame));

    /* a single frame using the SF_DL escape sequence */
    frame[0] = 0x00;
    frame[1] = ISO_TP_MAX_CAN_FRAME_SIZE;
    deliver(&link, frame, (uint8_t)sizeof(frame));
    EXPECT_EQ(gFrameCount, 0);
    EXPECT_EQ(link.receive_status, ISOTP_RECEIVE_STATUS_IDLE);
    EXPECT_EQ(isotp_receive(&link, gReceived.data(), sizeof(gReceived), &outSize), ISOTP_RET_NO_DATA);

    /* a first frame, which is not answered with flow control either */
    frame[0] = 0x10;
    frame[1] = 100;
    deliver(&link, frame, (uint8_t)sizeof(frame));
    EXPECT_EQ(gFrameCount, 0);
    EXPECT_EQ(link.receive_status, ISOTP_RECEIVE_STATUS_IDLE);
}

#ifdef ISO_TP_ENABLE_STREAMING

/*
 * Transfers a message which does not fit into the receiver's buffer and collects
 * the chunks the receiver hands out, which is where a frame carrying more data
 * than the remaining buffer space has to be carried over to the next chunk.
 */
static void check_streaming_transfer(uint8_t txDl, uint32_t size, uint32_t receiveBufferSize) {
    IsoTpLink sender;
    IsoTpLink receiver;
    uint8_t   chunk[TEST_BUFFER_SIZE];
    uint32_t  received    = 0;
    uint32_t  chunkCount = 0;
    uint32_t  nextFrame  = 0;
    uint32_t  iterations  = 0;
    bool      complete    = false;

    reset_bus();
    test_init_link(&sender, TEST_TX_ID, gSendBuffer.data(), sizeof(gSendBuffer), gReceiveBuffer.data(), sizeof(gReceiveBuffer));
    test_init_link(&receiver, TEST_RX_ID, gPeerSendBuffer.data(), sizeof(gPeerSendBuffer), gPeerReceiveBuffer.data(), receiveBufferSize);
    EXPECT_EQ(isotp_set_tx_dl(&sender, txDl), ISOTP_RET_OK);
    EXPECT_EQ(isotp_set_tx_dl(&receiver, txDl), ISOTP_RET_OK);

    fill_pattern(gPayload.data(), size);
    EXPECT_EQ(isotp_send(&sender, gPayload.data(), size), ISOTP_RET_OK);

    while (!complete && iterations++ < 200000u) {
        while (nextFrame < gFrameCount) {
            const test_frame_t FRAME = gFrames[nextFrame++];

            if (FRAME.m_id == TEST_TX_ID) {
                deliver(&receiver, FRAME.m_data, FRAME.m_len);
            } else {
                deliver(&sender, FRAME.m_data, FRAME.m_len);
            }
        }

        /* chunked transfers exchange far more frames than the log is able to hold */
        if (nextFrame == gFrameCount) {
            gFrameCount = 0;
            nextFrame    = 0;
        }

        if (ISOTP_RECEIVE_STATUS_FULL == receiver.receive_status) {
            uint32_t chunkSize = 0;

            /* a partially received message must not be handed out as a complete one */
            if (0 == chunkCount) { EXPECT_EQ(isotp_receive(&receiver, chunk, sizeof(chunk), &chunkSize), ISOTP_RET_ERROR); }

            EXPECT_EQ(isotp_receive_streaming(&receiver, chunk, sizeof(chunk), &chunkSize, &complete), ISOTP_RET_OK);
            EXPECT_TRUE(chunkSize > 0);
            EXPECT_TRUE(chunkSize <= receiveBufferSize);
            EXPECT_TRUE(received + chunkSize <= size);

            if (received + chunkSize <= size) { memcpy(&gReceived[received], chunk, chunkSize); }
            received += chunkSize;
            ++chunkCount;
        }

        isotp_poll(&sender);
        isotp_poll(&receiver);
        gTimeUs += 10;
    }

    EXPECT_TRUE(complete);
    EXPECT_TRUE(chunkCount > 1);
    EXPECT_EQ(received, size);
    EXPECT_EQ(sender.send_status, ISOTP_SEND_STATUS_IDLE);

    ASSERT_EQ(received, size);
    EXPECT_EQ(memcmp(gReceived.data(), gPayload.data(), size), 0);
}

static void test_streaming(uint8_t txDl) {
    printf("test_streaming (TX_DL %u)\n", (unsigned int)txDl);

    /* receive buffers smaller than the data of a single frame exercise the carry over */
    check_streaming_transfer(txDl, 500, 1);
    check_streaming_transfer(txDl, 500, 4);
    check_streaming_transfer(txDl, 500, 20);
    check_streaming_transfer(txDl, 500, 64);
    check_streaming_transfer(txDl, 4096, 100);
    check_streaming_transfer(txDl, 5000, 333);
}

static void test_streaming_of_small_messages(uint8_t txDl) {
    IsoTpLink sender;
    IsoTpLink receiver;
    uint32_t  chunkSize = 0;
    bool      complete   = false;

    printf("test_streaming_of_small_messages (TX_DL %u)\n", (unsigned int)txDl);

    /* messages fitting into the receive buffer are handed out in one piece */
    init_links(&sender, &receiver, txDl);
    fill_pattern(gPayload.data(), 100);
    EXPECT_EQ(isotp_send(&sender, gPayload.data(), 100), ISOTP_RET_OK);
    pump(&sender, &receiver);

    EXPECT_EQ(receiver.receive_status, ISOTP_RECEIVE_STATUS_FULL);
    EXPECT_EQ(isotp_receive_streaming(&receiver, gReceived.data(), sizeof(gReceived), &chunkSize, &complete), ISOTP_RET_OK);
    EXPECT_EQ(chunkSize, 100);
    EXPECT_TRUE(complete);
    EXPECT_EQ(memcmp(gReceived.data(), gPayload.data(), 100), 0);

    /* the same goes for single frames */
    init_links(&sender, &receiver, txDl);
    fill_pattern(gPayload.data(), 6);
    complete = false;
    EXPECT_EQ(isotp_send(&sender, gPayload.data(), 6), ISOTP_RET_OK);
    pump(&sender, &receiver);

    EXPECT_EQ(isotp_receive_streaming(&receiver, gReceived.data(), sizeof(gReceived), &chunkSize, &complete), ISOTP_RET_OK);
    EXPECT_EQ(chunkSize, 6);
    EXPECT_TRUE(complete);
    EXPECT_EQ(memcmp(gReceived.data(), gPayload.data(), 6), 0);

    /* invalid arguments are rejected */
    EXPECT_EQ(isotp_receive_streaming(nullptr, gReceived.data(), sizeof(gReceived), &chunkSize, &complete), ISOTP_RET_ERROR);
    EXPECT_EQ(isotp_receive_streaming(&receiver, nullptr, sizeof(gReceived), &chunkSize, &complete), ISOTP_RET_ERROR);
    EXPECT_EQ(isotp_receive_streaming(&receiver, gReceived.data(), sizeof(gReceived), nullptr, &complete), ISOTP_RET_ERROR);
    EXPECT_EQ(isotp_receive_streaming(&receiver, gReceived.data(), sizeof(gReceived), &chunkSize, nullptr), ISOTP_RET_ERROR);

    /* and so is a request without any data pending */
    EXPECT_EQ(isotp_receive_streaming(&receiver, gReceived.data(), sizeof(gReceived), &chunkSize, &complete), ISOTP_RET_NO_DATA);
}

#endif // ISO_TP_ENABLE_STREAMING

#ifdef ISO_TP_USER_SEND_CAN_FLAGS

/* the frame format flags a link is expected to hand to the user shim */
static uint8_t expected_frame_flags(uint8_t txDl) {
    uint8_t flags = ISOTP_CAN_FRAME_FLAG_NONE;

    if (txDl > 8) {
        flags |= ISOTP_CAN_FRAME_FLAG_FD;

    #ifdef ISO_TP_CAN_FD_USE_BRS
        flags |= ISOTP_CAN_FRAME_FLAG_BRS;
    #endif
    }

    return flags;
}

static void test_frame_flags(uint8_t txDl) {
    IsoTpLink sender;
    IsoTpLink receiver;

    printf("test_frame_flags (TX_DL %u)\n", (unsigned int)txDl);

    /* a single frame short enough to fit into a Classical CAN frame */
    init_links(&sender, &receiver, txDl);
    fill_pattern(gPayload.data(), 3);
    EXPECT_EQ(isotp_send(&sender, gPayload.data(), 3), ISOTP_RET_OK);
    EXPECT_EQ(gFrameCount, 1);
    EXPECT_EQ(gFrames[0].m_flags, expected_frame_flags(txDl));

    /* first frame, flow control frames of the receiver and consecutive frames */
    init_links(&sender, &receiver, txDl);
    fill_pattern(gPayload.data(), 500);
    EXPECT_EQ(isotp_send(&sender, gPayload.data(), 500), ISOTP_RET_OK);
    pump(&sender, &receiver);

    EXPECT_TRUE(gFrameCount > 3);
    for (uint32_t i = 0; i < gFrameCount; ++i) { EXPECT_EQ(gFrames[i].m_flags, expected_frame_flags(txDl)); }
}

#endif // ISO_TP_USER_SEND_CAN_FLAGS

#if defined(ISO_TP_TRANSMIT_COMPLETE_CALLBACK) || defined(ISO_TP_RECEIVE_COMPLETE_CALLBACK)

    #ifdef ISO_TP_TRANSMIT_COMPLETE_CALLBACK
static uint32_t gTxDoneCount;
static uint32_t gTxDoneSize;

static void     on_tx_done(void* link, uint32_t size, void* userArg) {
    (void)link;
    ++gTxDoneCount;
    gTxDoneSize = size;
    EXPECT_TRUE(userArg == &gTxDoneCount);
}
    #endif

    #ifdef ISO_TP_RECEIVE_COMPLETE_CALLBACK
static uint32_t gRxDoneCount;
static uint32_t gRxDoneSize;

static void     on_rx_done(void* link, const uint8_t* data, uint32_t size, void* userArg) {
    (void)link;
    ++gRxDoneCount;
    gRxDoneSize = size;
    EXPECT_TRUE(userArg == &gRxDoneCount);
    EXPECT_EQ(memcmp(data, gPayload.data(), size), 0);
}
    #endif

static void test_callbacks(uint8_t txDl, uint32_t size) {
    IsoTpLink sender;
    IsoTpLink receiver;

    printf("test_callbacks (TX_DL %u, %u bytes)\n", (unsigned int)txDl, (unsigned int)size);
    init_links(&sender, &receiver, txDl);

    #ifdef ISO_TP_TRANSMIT_COMPLETE_CALLBACK
    gTxDoneCount = 0;
    gTxDoneSize  = 0;
    isotp_set_tx_done_cb(&sender, on_tx_done, &gTxDoneCount);
    #endif

    #ifdef ISO_TP_RECEIVE_COMPLETE_CALLBACK
    gRxDoneCount = 0;
    gRxDoneSize  = 0;
    isotp_set_rx_done_cb(&receiver, on_rx_done, &gRxDoneCount);
    #endif

    fill_pattern(gPayload.data(), size);
    EXPECT_EQ(isotp_send(&sender, gPayload.data(), size), ISOTP_RET_OK);
    pump(&sender, &receiver);

    #ifdef ISO_TP_TRANSMIT_COMPLETE_CALLBACK
    EXPECT_EQ(gTxDoneCount, 1);
    EXPECT_EQ(gTxDoneSize, size);
    #endif

    #ifdef ISO_TP_RECEIVE_COMPLETE_CALLBACK
    EXPECT_EQ(gRxDoneCount, 1);
    EXPECT_EQ(gRxDoneSize, size);
    #endif
}

#endif // callbacks

#if ISO_TP_MAX_CAN_FRAME_SIZE >= 64

static void test_can_fd_single_frame(void) {
    IsoTpLink link;

    printf("test_can_fd_single_frame\n");
    reset_bus();
    test_init_link(&link, TEST_TX_ID, gSendBuffer.data(), sizeof(gSendBuffer), gReceiveBuffer.data(), sizeof(gReceiveBuffer));
    EXPECT_EQ(isotp_set_tx_dl(&link, 64), ISOTP_RET_OK);

    /* payloads of up to 7 bytes keep using the Classical CAN single frame format */
    fill_pattern(gPayload.data(), 7);
    EXPECT_EQ(isotp_send(&link, gPayload.data(), 7), ISOTP_RET_OK);
    EXPECT_EQ(gFrameCount, 1);
    EXPECT_EQ(gFrames[0].m_len, 8);
    EXPECT_EQ(gFrames[0].m_data[0], 0x07);
    EXPECT_EQ(memcmp(&gFrames[0].m_data[1], gPayload.data(), 7), 0);

    /* larger payloads use the SF_DL escape sequence and are padded to a valid CAN FD length */
    reset_bus();
    fill_pattern(gPayload.data(), 20);
    EXPECT_EQ(isotp_send(&link, gPayload.data(), 20), ISOTP_RET_OK);
    EXPECT_EQ(gFrameCount, 1);
    EXPECT_EQ(gFrames[0].m_len, 24);
    EXPECT_EQ(gFrames[0].m_data[0], 0x00);
    EXPECT_EQ(gFrames[0].m_data[1], 20);
    EXPECT_EQ(memcmp(&gFrames[0].m_data[2], gPayload.data(), 20), 0);
    check_padding(&gFrames[0], 22);

    /* the largest payload which still fits into a single frame */
    reset_bus();
    fill_pattern(gPayload.data(), 62);
    EXPECT_EQ(isotp_send(&link, gPayload.data(), 62), ISOTP_RET_OK);
    EXPECT_EQ(gFrameCount, 1);
    EXPECT_EQ(gFrames[0].m_len, 64);
    EXPECT_EQ(gFrames[0].m_data[0], 0x00);
    EXPECT_EQ(gFrames[0].m_data[1], 62);
    EXPECT_EQ(memcmp(&gFrames[0].m_data[2], gPayload.data(), 62), 0);

    /* smaller frame lengths reduce the amount of data a single frame can carry */
    reset_bus();
    EXPECT_EQ(isotp_set_tx_dl(&link, 12), ISOTP_RET_OK);
    fill_pattern(gPayload.data(), 10);
    EXPECT_EQ(isotp_send(&link, gPayload.data(), 10), ISOTP_RET_OK);
    EXPECT_EQ(gFrameCount, 1);
    EXPECT_EQ(gFrames[0].m_len, 12);
    EXPECT_EQ(gFrames[0].m_data[1], 10);
    EXPECT_EQ(memcmp(&gFrames[0].m_data[2], gPayload.data(), 10), 0);
}

static void test_can_fd_multi_frame(void) {
    IsoTpLink link;

    printf("test_can_fd_multi_frame\n");
    reset_bus();
    test_init_link(&link, TEST_TX_ID, gSendBuffer.data(), sizeof(gSendBuffer), gReceiveBuffer.data(), sizeof(gReceiveBuffer));
    EXPECT_EQ(isotp_set_tx_dl(&link, 64), ISOTP_RET_OK);

    /* 63 bytes is the smallest payload requiring segmentation at a TX_DL of 64 */
    fill_pattern(gPayload.data(), 63);
    EXPECT_EQ(isotp_send(&link, gPayload.data(), 63), ISOTP_RET_OK);

    /* first frames always use the full frame length of the sender */
    EXPECT_EQ(gFrameCount, 1);
    EXPECT_EQ(gFrames[0].m_len, 64);
    EXPECT_EQ(gFrames[0].m_data[0], 0x10);
    EXPECT_EQ(gFrames[0].m_data[1], 63);
    EXPECT_EQ(memcmp(&gFrames[0].m_data[2], gPayload.data(), 62), 0);

    deliver_flow_control(&link, PCI_FLOW_STATUS_CONTINUE, 0, 0);
    isotp_poll(&link);

    /* the trailing consecutive frame only carries the remaining byte */
    EXPECT_EQ(gFrameCount, 2);
    EXPECT_EQ(gFrames[1].m_len, expected_can_dl(2));
    EXPECT_EQ(gFrames[1].m_data[0], 0x21);
    EXPECT_EQ(gFrames[1].m_data[1], gPayload[62]);
    check_padding(&gFrames[1], 2);
    EXPECT_EQ(link.send_status, ISOTP_SEND_STATUS_IDLE);

    /* consecutive frames carry up to TX_DL - 1 bytes */
    reset_bus();
    fill_pattern(gPayload.data(), 200);
    EXPECT_EQ(isotp_send(&link, gPayload.data(), 200), ISOTP_RET_OK);
    deliver_flow_control(&link, PCI_FLOW_STATUS_CONTINUE, 0, 0);
    isotp_poll(&link);
    EXPECT_EQ(gFrameCount, 2);
    EXPECT_EQ(gFrames[1].m_len, 64);
    EXPECT_EQ(gFrames[1].m_data[0], 0x21);
    EXPECT_EQ(memcmp(&gFrames[1].m_data[1], &gPayload[62], 63), 0);
}

static void test_can_fd_receive(void) {
    IsoTpLink link;
    uint8_t   frame[64];
    uint32_t  outSize = 0;

    printf("test_can_fd_receive\n");
    reset_bus();
    test_init_link(&link, TEST_RX_ID, gSendBuffer.data(), sizeof(gSendBuffer), gReceiveBuffer.data(), sizeof(gReceiveBuffer));

    /* single frame using the SF_DL escape sequence */
    memset(frame, ISO_TP_FRAME_PADDING_VALUE, sizeof(frame));
    frame[0] = 0x00;
    frame[1] = 20;
    fill_pattern(gPayload.data(), 20);
    memcpy(&frame[2], gPayload.data(), 20);
    deliver(&link, frame, 24);
    EXPECT_EQ(isotp_receive(&link, gReceived.data(), sizeof(gReceived), &outSize), ISOTP_RET_OK);
    EXPECT_EQ(outSize, 20);
    EXPECT_EQ(memcmp(gReceived.data(), gPayload.data(), 20), 0);

    /* the escape sequence must not announce more data than the frame contains */
    reset_bus();
    frame[1] = 40;
    deliver(&link, frame, 24);
    EXPECT_EQ(isotp_receive(&link, gReceived.data(), sizeof(gReceived), &outSize), ISOTP_RET_NO_DATA);

    /* first frames must use a valid CAN FD frame length */
    reset_bus();
    frame[0] = 0x10;
    frame[1] = 100;
    deliver(&link, frame, 22);
    EXPECT_EQ(gFrameCount, 0);
    EXPECT_EQ(link.receive_status, ISOTP_RECEIVE_STATUS_IDLE);

    /* messages which would have fit into a single frame are rejected */
    reset_bus();
    frame[1] = 60;
    deliver(&link, frame, 64);
    EXPECT_EQ(gFrameCount, 0);
    EXPECT_EQ(link.receive_status, ISOTP_RECEIVE_STATUS_IDLE);

    /* a segmented message: the consecutive frames use the frame length of the first frame */
    reset_bus();
    fill_pattern(gPayload.data(), 100);
    frame[0] = 0x10;
    frame[1] = 100;
    memcpy(&frame[2], gPayload.data(), 62);
    deliver(&link, frame, 64);
    EXPECT_EQ(link.receive_status, ISOTP_RECEIVE_STATUS_INPROGRESS);
    EXPECT_EQ(gFrameCount, 1);
    EXPECT_EQ(gFrames[0].m_data[0], 0x30);

    frame[0] = 0x21;
    memcpy(&frame[1], &gPayload[62], 38);
    deliver(&link, frame, 48);
    EXPECT_EQ(isotp_receive(&link, gReceived.data(), sizeof(gReceived), &outSize), ISOTP_RET_OK);
    EXPECT_EQ(outSize, 100);
    EXPECT_EQ(memcmp(gReceived.data(), gPayload.data(), 100), 0);

    /* consecutive frames must be full while data is still outstanding */
    reset_bus();
    frame[0] = 0x10;
    frame[1] = 100;
    memcpy(&frame[2], gPayload.data(), 62);
    deliver(&link, frame, 64);
    frame[0] = 0x21;
    memcpy(&frame[1], &gPayload[62], 11);
    deliver(&link, frame, 12);
    EXPECT_EQ(link.receive_status, ISOTP_RECEIVE_STATUS_INPROGRESS);
    EXPECT_EQ(isotp_receive(&link, gReceived.data(), sizeof(gReceived), &outSize), ISOTP_RET_NO_DATA);

    /* frames larger than the configured maximum are ignored */
    reset_bus();
    EXPECT_EQ(link.receive_status, ISOTP_RECEIVE_STATUS_INPROGRESS);
}

#endif // ISO_TP_MAX_CAN_FRAME_SIZE >= 64

TEST_F(FramingTest, InitializesAndValidatesTransmitDataLength) {
    test_default_tx_dl();
    test_set_tx_dl();
}

TEST_F(FramingTest, EncodesClassicSingleFrames) { test_classic_single_frame(); }
TEST_F(FramingTest, EncodesClassicMultiFrames) { test_classic_multi_frame(); }
TEST_F(FramingTest, ReceivesClassicFrames) { test_classic_receive(); }
TEST_F(FramingTest, RejectsMalformedAndOversizedFrames) {
    test_receive_rejects_invalid_frames();
    test_oversized_frames_ignored();
}
TEST_F(FramingTest, EncodesLongFirstFrames) { test_long_first_frame(8); }
TEST_F(FramingTest, TransfersPayloadsAtClassicDataLength) { test_transfers(8); }

TEST_F(FramingTest, SendsWithOverriddenIdentifier) {
    IsoTpLink link;
    test_init_link(&link, TEST_TX_ID, gSendBuffer.data(), sizeof(gSendBuffer), gReceiveBuffer.data(), sizeof(gReceiveBuffer));
    ASSERT_EQ(isotp_set_tx_dl(&link, 8), ISOTP_RET_OK);

    /* a functional request is a Single Frame sent with a one-time identifier */
    fill_pattern(gPayload.data(), 3);
    EXPECT_EQ(isotp_send_with_id(&link, TEST_FUNCTIONAL_ID, gPayload.data(), 3), ISOTP_RET_OK);
    EXPECT_EQ(gFrameCount, 1);
    EXPECT_EQ(gFrames[0].m_id, TEST_FUNCTIONAL_ID);
    EXPECT_EQ(gFrames[0].m_data[0], 0x03);
    EXPECT_EQ(memcmp(&gFrames[0].m_data[1], gPayload.data(), 3), 0);

    /* the override applies to that send only and leaves the link identifier untouched */
    EXPECT_EQ(link.send_arbitration_id, TEST_TX_ID);
    reset_bus();
    EXPECT_EQ(isotp_send(&link, gPayload.data(), 3), ISOTP_RET_OK);
    EXPECT_EQ(gFrameCount, 1);
    EXPECT_EQ(gFrames[0].m_id, TEST_TX_ID);

    /* a segmented send starts its First Frame with the override as well */
    reset_bus();
    fill_pattern(gPayload.data(), 20);
    EXPECT_EQ(isotp_send_with_id(&link, TEST_FUNCTIONAL_ID, gPayload.data(), 20), ISOTP_RET_OK);
    EXPECT_EQ(gFrameCount, 1);
    EXPECT_EQ(gFrames[0].m_id, TEST_FUNCTIONAL_ID);
    EXPECT_EQ(gFrames[0].m_data[0], 0x10);

#if ISO_TP_MAX_CAN_FRAME_SIZE > 8
    /* the CAN FD SF_DL escape format honours the override too */
    reset_bus();
    test_init_link(&link, TEST_TX_ID, gSendBuffer.data(), sizeof(gSendBuffer), gReceiveBuffer.data(), sizeof(gReceiveBuffer));
    ASSERT_EQ(isotp_set_tx_dl(&link, ISO_TP_MAX_CAN_FRAME_SIZE), ISOTP_RET_OK);
    EXPECT_EQ(isotp_send_with_id(&link, TEST_FUNCTIONAL_ID, gPayload.data(), 8), ISOTP_RET_OK);
    EXPECT_EQ(gFrameCount, 1);
    EXPECT_EQ(gFrames[0].m_id, TEST_FUNCTIONAL_ID);
    EXPECT_EQ(gFrames[0].m_data[0], 0x00);
    EXPECT_EQ(gFrames[0].m_data[1], 8);
#endif
}

TEST_F(FramingTest, RejectsInvalidSendStateAndPropagatesDriverFailures) {
    IsoTpLink link;
    EXPECT_CALL(m_user, debug(testing::_)).Times(testing::AtLeast(1));
    EXPECT_CALL(m_user, send_can(testing::_, testing::_, testing::_, testing::_, testing::_)).Times(2).WillRepeatedly(capture_can_frame);
    test_init_link(&link, TEST_TX_ID, gSendBuffer.data(), 16, gReceiveBuffer.data(), sizeof(gReceiveBuffer));
    ASSERT_EQ(isotp_set_tx_dl(&link, 8), ISOTP_RET_OK);
    fill_pattern(gPayload.data(), 20);

    EXPECT_EQ(isotp_send_with_id(nullptr, TEST_TX_ID, gPayload.data(), 1), ISOTP_RET_ERROR);
    EXPECT_FALSE(gLastDebugMessage.empty());
    EXPECT_EQ(isotp_send(&link, gPayload.data(), 17), ISOTP_RET_OVERFLOW);
    EXPECT_NE(gLastDebugMessage.find("17"), std::string::npos);

    link.send_status = ISOTP_SEND_STATUS_INPROGRESS;
    EXPECT_EQ(isotp_send(&link, gPayload.data(), 1), ISOTP_RET_INPROGRESS);

    link.send_status = ISOTP_SEND_STATUS_IDLE;
    gSendResult    = ISOTP_RET_ERROR;
    EXPECT_EQ(isotp_send(&link, gPayload.data(), 1), ISOTP_RET_ERROR);
    EXPECT_EQ(link.send_status, ISOTP_SEND_STATUS_IDLE);

    gSendResult = ISOTP_RET_ERROR;
    EXPECT_EQ(isotp_send(&link, gPayload.data(), 12), ISOTP_RET_ERROR);
    EXPECT_EQ(link.send_status, ISOTP_SEND_STATUS_IDLE);
}

TEST_F(FramingTest, HandlesFlowControlErrorsRetriesAndTimeouts) {
    IsoTpLink link;
    test_init_link(&link, TEST_TX_ID, gSendBuffer.data(), sizeof(gSendBuffer), gReceiveBuffer.data(), sizeof(gReceiveBuffer));
    ASSERT_EQ(isotp_set_tx_dl(&link, 8), ISOTP_RET_OK);
    fill_pattern(gPayload.data(), 20);

    ASSERT_EQ(isotp_send(&link, gPayload.data(), 20), ISOTP_RET_OK);
    const uint8_t SHORT_FLOW_CONTROL[] = {0x30, 0x00};
    deliver(&link, SHORT_FLOW_CONTROL, sizeof(SHORT_FLOW_CONTROL));
    EXPECT_EQ(link.send_status, ISOTP_SEND_STATUS_INPROGRESS);

    deliver_flow_control(&link, PCI_FLOW_STATUS_WAIT, 0, 0);
    deliver_flow_control(&link, PCI_FLOW_STATUS_WAIT, 0, 0);
    deliver_flow_control(&link, PCI_FLOW_STATUS_WAIT, 0, 0);
    deliver_flow_control(&link, PCI_FLOW_STATUS_WAIT, 0, 0);
    EXPECT_EQ(link.send_status, ISOTP_SEND_STATUS_ERROR);
    EXPECT_EQ(link.send_protocol_result, ISOTP_PROTOCOL_RESULT_WFT_OVRN);

    reset_bus();
    test_init_link(&link, TEST_TX_ID, gSendBuffer.data(), sizeof(gSendBuffer), gReceiveBuffer.data(), sizeof(gReceiveBuffer));
    ASSERT_EQ(isotp_set_tx_dl(&link, 8), ISOTP_RET_OK);
    ASSERT_EQ(isotp_send(&link, gPayload.data(), 20), ISOTP_RET_OK);
    deliver_flow_control(&link, PCI_FLOW_STATUS_OVERFLOW, 0, 0);
    EXPECT_EQ(link.send_status, ISOTP_SEND_STATUS_ERROR);
    EXPECT_EQ(link.send_protocol_result, ISOTP_PROTOCOL_RESULT_BUFFER_OVFLW);

    reset_bus();
    test_init_link(&link, TEST_TX_ID, gSendBuffer.data(), sizeof(gSendBuffer), gReceiveBuffer.data(), sizeof(gReceiveBuffer));
    ASSERT_EQ(isotp_set_tx_dl(&link, 8), ISOTP_RET_OK);
    ASSERT_EQ(isotp_send(&link, gPayload.data(), 20), ISOTP_RET_OK);
    gTimeUs = ISO_TP_DEFAULT_RESPONSE_TIMEOUT_US + 1U;
    isotp_poll(&link);
    EXPECT_EQ(link.send_status, ISOTP_SEND_STATUS_ERROR);
    EXPECT_EQ(link.send_protocol_result, ISOTP_PROTOCOL_RESULT_TIMEOUT_BS);

    reset_bus();
    test_init_link(&link, TEST_TX_ID, gSendBuffer.data(), sizeof(gSendBuffer), gReceiveBuffer.data(), sizeof(gReceiveBuffer));
    ASSERT_EQ(isotp_set_tx_dl(&link, 8), ISOTP_RET_OK);
    ASSERT_EQ(isotp_send(&link, gPayload.data(), 20), ISOTP_RET_OK);
    deliver_flow_control(&link, PCI_FLOW_STATUS_CONTINUE, 0, 0);
    gSendResult = ISOTP_RET_NOSPACE;
    isotp_poll(&link);
    EXPECT_EQ(link.send_status, ISOTP_SEND_STATUS_INPROGRESS);
    gSendResult = ISOTP_RET_ERROR;
    isotp_poll(&link);
    EXPECT_EQ(link.send_status, ISOTP_SEND_STATUS_ERROR);
}

TEST_F(FramingTest, DestroysInitializedAndNullLinks) {
    IsoTpLink link;
    test_init_link(&link, TEST_TX_ID, gSendBuffer.data(), sizeof(gSendBuffer), gReceiveBuffer.data(), sizeof(gReceiveBuffer));
    isotp_destroy_link(nullptr);
    isotp_destroy_link(&link);
    EXPECT_EQ(link.send_buffer, nullptr);
    EXPECT_EQ(link.receive_buffer, nullptr);
    EXPECT_EQ(link.send_status, 0);
}

TEST_F(FramingTest, HandlesProtocolStateAndBoundaryBranches) {
    IsoTpLink link;
    uint32_t  outSize    = 0;
    uint8_t   oneByte[1] = {};
    test_init_link(&link, TEST_TX_ID, gSendBuffer.data(), sizeof(gSendBuffer), gReceiveBuffer.data(), sizeof(gReceiveBuffer));
    ASSERT_EQ(isotp_set_tx_dl(&link, 8), ISOTP_RET_OK);

    const uint8_t UNEXPECTED_CONSECUTIVE[] = {0x21, 0xAA};
    deliver(&link, UNEXPECTED_CONSECUTIVE, sizeof(UNEXPECTED_CONSECUTIVE));
    EXPECT_EQ(link.receive_protocol_result, ISOTP_PROTOCOL_RESULT_UNEXP_PDU);

    const uint8_t UNKNOWN_PCI[] = {0xF0, 0x00};
    deliver(&link, UNKNOWN_PCI, sizeof(UNKNOWN_PCI));
    EXPECT_EQ(link.receive_status, ISOTP_RECEIVE_STATUS_IDLE);

    const uint8_t FIRST_FRAME[8]        = {0x10, 0x0A, 1, 2, 3, 4, 5, 6};
    const uint8_t INTERRUPTING_SINGLE[] = {0x02, 0xCA, 0xFE};
    deliver(&link, FIRST_FRAME, sizeof(FIRST_FRAME));
    ASSERT_EQ(link.receive_status, ISOTP_RECEIVE_STATUS_INPROGRESS);
    deliver(&link, INTERRUPTING_SINGLE, sizeof(INTERRUPTING_SINGLE));
    EXPECT_EQ(link.receive_protocol_result, ISOTP_PROTOCOL_RESULT_UNEXP_PDU);
    EXPECT_EQ(isotp_receive(&link, oneByte, sizeof(oneByte), &outSize), ISOTP_RET_OK);
    EXPECT_EQ(outSize, 1U);
    EXPECT_EQ(oneByte[0], 0xCA);

    fill_pattern(gPayload.data(), 20);
    ASSERT_EQ(isotp_send(&link, gPayload.data(), 20), ISOTP_RET_OK);
    deliver_flow_control(&link, PCI_FLOW_STATUS_CONTINUE, 2, 5);
    EXPECT_EQ(link.send_bs_remain, 2U);
    EXPECT_EQ(link.send_st_min_us, 5000U);
    deliver_flow_control(&link, PCI_FLOW_STATUS_CONTINUE, 1, 0xF1);
    EXPECT_EQ(link.send_st_min_us, 100U);
    deliver_flow_control(&link, PCI_FLOW_STATUS_CONTINUE, 1, 0x80);
    EXPECT_EQ(link.send_st_min_us, 0U);

    link.send_status      = ISOTP_SEND_STATUS_IDLE;
    link.receive_status   = ISOTP_RECEIVE_STATUS_INPROGRESS;
    link.receive_timer_cr = 0;
    isotp_poll(&link);
    EXPECT_EQ(link.receive_status, ISOTP_RECEIVE_STATUS_INPROGRESS);

#ifdef ISO_TP_TRANSMIT_COMPLETE_CALLBACK
    isotp_set_tx_done_cb(nullptr, nullptr, nullptr);
#endif
#ifdef ISO_TP_RECEIVE_COMPLETE_CALLBACK
    isotp_set_rx_done_cb(nullptr, nullptr, nullptr);
#endif
}

#ifdef ISO_TP_USER_SEND_CAN_FLAGS
TEST_F(FramingTest, SuppliesClassicFrameFlags) { test_frame_flags(8); }
#endif

#ifdef ISO_TP_ENABLE_STREAMING
TEST_F(FramingTest, StreamsAtClassicDataLength) { test_streaming(8); }
TEST_F(FramingTest, StreamsSmallMessagesAtClassicDataLength) { test_streaming_of_small_messages(8); }
#endif

#if ISO_TP_MAX_CAN_FRAME_SIZE >= 64
TEST_F(FramingTest, EncodesCanFdSingleFrames) { test_can_fd_single_frame(); }
TEST_F(FramingTest, EncodesCanFdMultiFrames) { test_can_fd_multi_frame(); }
TEST_F(FramingTest, ReceivesCanFdFrames) { test_can_fd_receive(); }
TEST_F(FramingTest, EncodesCanFdLongFirstFrames) {
    test_long_first_frame(12);
    test_long_first_frame(64);
}
TEST_F(FramingTest, TransfersPayloadsAtCanFdDataLengths) {
    test_transfers(12);
    test_transfers(64);
}

    #ifdef ISO_TP_USER_SEND_CAN_FLAGS
TEST_F(FramingTest, SuppliesCanFdFrameFlags) {
    test_frame_flags(12);
    test_frame_flags(64);
}
    #endif

    #ifdef ISO_TP_ENABLE_STREAMING
TEST_F(FramingTest, StreamsAtCanFdDataLengths) {
    test_streaming(12);
    test_streaming(64);
}
TEST_F(FramingTest, StreamsSmallMessagesAtCanFdDataLength) { test_streaming_of_small_messages(64); }
    #endif
#endif

#if defined(ISO_TP_TRANSMIT_COMPLETE_CALLBACK) || defined(ISO_TP_RECEIVE_COMPLETE_CALLBACK)
TEST_F(FramingTest, InvokesCompletionCallbacks) {
    test_callbacks(8, 5);
    test_callbacks(8, 100);
    #if ISO_TP_MAX_CAN_FRAME_SIZE >= 64
    test_callbacks(64, 20);
    test_callbacks(64, 1000);
    #endif
}
#endif
