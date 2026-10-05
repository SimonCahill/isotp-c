#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#ifdef isotpc_USE_INCLUDE_DIR
    #include "isotp_c/isotp.h"
#else
    #include "isotp.h"
#endif
#include "mocks/isotp_user_mock.hpp"

namespace {
constexpr uint32_t BUFFER_SIZE = 10000u;

struct CapturedXlFrame {
    IsoTpCanXlMetadata m_metadata;
    std::vector<uint8_t> m_data;
};

std::vector<CapturedXlFrame> frames;
uint32_t nowUs;
int xlSendResult;
void* expectedArgument;

const IsoTpCanXlMetadata TESTER_METADATA{0x123u, 0x12345678u, 0x12u, 0x34u, ISOTP_CAN_XL_FLAG_SEC};
const IsoTpCanXlMetadata ECU_METADATA{0x456u, 0x87654321u, 0x56u, 0x78u, ISOTP_CAN_XL_FLAG_RRS};

int capture_xl(const IsoTpCanXlFrame& frame, void* argument) {
    EXPECT_EQ(argument, expectedArgument);
    frames.push_back({frame.metadata, std::vector<uint8_t>(frame.data, frame.data + frame.size)});
    return xlSendResult;
}

bool same_metadata(const IsoTpCanXlMetadata& lhs, const IsoTpCanXlMetadata& rhs) {
    return lhs.priority_id == rhs.priority_id && lhs.acceptance_field == rhs.acceptance_field && lhs.vcid == rhs.vcid && lhs.sdt == rhs.sdt
           && lhs.flags == rhs.flags;
}

void fill_pattern(uint8_t* data, uint32_t size) {
    for (uint32_t i = 0; i < size; ++i) { data[i] = static_cast<uint8_t>(i * 13u + 7u); }
}

class CanXlTest: public testing::Test {
   protected:
    void SetUp() override {
        frames.clear();
        nowUs = 0u;
        xlSendResult = ISOTP_RET_OK;
        expectedArgument = nullptr;
        isotp_set_user_mock(&m_user);
        ON_CALL(m_user, get_us()).WillByDefault([] { return nowUs; });
        ON_CALL(m_user, send_can_xl(testing::_, testing::_)).WillByDefault(capture_xl);
    }

    void TearDown() override { isotp_set_user_mock(nullptr); }

    testing::NiceMock<IsoTpUserMock> m_user;
};

std::array<uint8_t, BUFFER_SIZE> txBuffer{};
std::array<uint8_t, BUFFER_SIZE> rxBuffer{};
std::array<uint8_t, BUFFER_SIZE> peerTxBuffer{};
std::array<uint8_t, BUFFER_SIZE> peerRxBuffer{};
std::array<uint8_t, BUFFER_SIZE> payload{};
std::array<uint8_t, BUFFER_SIZE> received{};

void deliver(IsoTpLink* link, const CapturedXlFrame& captured) {
    const IsoTpCanXlFrame FRAME{captured.m_metadata, captured.m_data.data(), static_cast<uint16_t>(captured.m_data.size())};
    isotp_on_can_xl_message(link, &FRAME);
}
}

TEST_F(CanXlTest, ValidatesConfigurationAndKeepsLegacyApiSeparate) {
    IsoTpLink link{};
    IsoTpCanXlMetadata invalid = TESTER_METADATA;
    invalid.priority_id = 0x800u;

    EXPECT_EQ(isotp_init_link_xl(nullptr, &TESTER_METADATA, 2048u, txBuffer.data(), txBuffer.size(), rxBuffer.data(), rxBuffer.size()), ISOTP_RET_ERROR);
    EXPECT_EQ(isotp_init_link_xl(&link, nullptr, 2048u, txBuffer.data(), txBuffer.size(), rxBuffer.data(), rxBuffer.size()), ISOTP_RET_ERROR);
    EXPECT_EQ(isotp_init_link_xl(&link, &invalid, 2048u, txBuffer.data(), txBuffer.size(), rxBuffer.data(), rxBuffer.size()), ISOTP_RET_ERROR);
    invalid = TESTER_METADATA;
    invalid.flags = 0x80u;
    EXPECT_EQ(isotp_init_link_xl(&link, &invalid, 2048u, txBuffer.data(), txBuffer.size(), rxBuffer.data(), rxBuffer.size()), ISOTP_RET_ERROR);
    EXPECT_EQ(isotp_init_link_xl(&link, &TESTER_METADATA, 7u, txBuffer.data(), txBuffer.size(), rxBuffer.data(), rxBuffer.size()), ISOTP_RET_ERROR);
    EXPECT_EQ(isotp_init_link_xl(&link, &TESTER_METADATA, ISO_TP_MAX_CAN_XL_FRAME_SIZE + 1u, txBuffer.data(), txBuffer.size(), rxBuffer.data(), rxBuffer.size()),
              ISOTP_RET_ERROR);
    ASSERT_EQ(isotp_init_link_xl(&link, &TESTER_METADATA, 2048u, txBuffer.data(), txBuffer.size(), rxBuffer.data(), rxBuffer.size()), ISOTP_RET_OK);

    EXPECT_EQ(isotp_get_tx_dl_xl(&link), 2048u);
    EXPECT_EQ(isotp_get_tx_dl_xl(nullptr), 0u);
    EXPECT_EQ(isotp_set_tx_dl_xl(nullptr, 64u), ISOTP_RET_ERROR);
    IsoTpLink legacy{};
    isotp_init_link(&legacy, 1u, txBuffer.data(), txBuffer.size(), rxBuffer.data(), rxBuffer.size());
    EXPECT_EQ(isotp_set_tx_dl_xl(&legacy, 64u), ISOTP_RET_ERROR);
    EXPECT_EQ(isotp_get_tx_dl_xl(&legacy), 0u);
    EXPECT_EQ(isotp_set_tx_dl_xl(&link, 7u), ISOTP_RET_ERROR);
    EXPECT_EQ(isotp_set_tx_dl_xl(&link, ISO_TP_MAX_CAN_XL_FRAME_SIZE + 1u), ISOTP_RET_ERROR);
    EXPECT_EQ(isotp_set_tx_dl_xl(&link, 64u), ISOTP_RET_OK);
    EXPECT_EQ(isotp_get_tx_dl_xl(&link), 64u);
    link.tx_dl = 7u;
    EXPECT_EQ(isotp_get_tx_dl_xl(&link), ISOTP_CAN_DL_CLASSIC);
    link.tx_dl = ISO_TP_MAX_CAN_XL_FRAME_SIZE + 1u;
    EXPECT_EQ(isotp_get_tx_dl_xl(&link), ISOTP_CAN_DL_CLASSIC);
    link.tx_dl = 64u;
    EXPECT_EQ(isotp_set_tx_dl(&link, 64u), ISOTP_RET_ERROR);
    EXPECT_EQ(isotp_get_tx_dl(&link), 0u);
    EXPECT_EQ(isotp_send_with_id(&link, 1u, payload.data(), 1u), ISOTP_RET_ERROR);
    EXPECT_EQ(isotp_send_with_id(&legacy, 1u, payload.data(), 1u), ISOTP_RET_OK);
    EXPECT_EQ(isotp_send_with_id(nullptr, 1u, payload.data(), 1u), ISOTP_RET_ERROR);
    EXPECT_EQ(isotp_send_can_xl_with_metadata(nullptr, &TESTER_METADATA, payload.data(), 1u), ISOTP_RET_ERROR);
    EXPECT_EQ(isotp_send_can_xl_with_metadata(&legacy, &TESTER_METADATA, payload.data(), 1u), ISOTP_RET_ERROR);
    invalid = TESTER_METADATA;
    invalid.priority_id = 0x800u;
    EXPECT_EQ(isotp_send_can_xl_with_metadata(&link, &invalid, payload.data(), 1u), ISOTP_RET_ERROR);
}

TEST_F(CanXlTest, EncodesSmallSingleFramesAtMinimumDataLength) {
    IsoTpLink link{};
    ASSERT_EQ(isotp_init_link_xl(&link, &TESTER_METADATA, 8u, txBuffer.data(), txBuffer.size(), rxBuffer.data(), rxBuffer.size()), ISOTP_RET_OK);
    fill_pattern(payload.data(), 7u);
    ASSERT_EQ(isotp_send(&link, payload.data(), 7u), ISOTP_RET_OK);
    ASSERT_EQ(frames.size(), 1u);
    EXPECT_EQ(frames[0].m_data.size(), 8u);
    EXPECT_EQ(frames[0].m_data[0], 7u);
}

TEST_F(CanXlTest, EncodesExactSingleAndAdaptiveMultiFrames) {
    IsoTpLink link{};
    ASSERT_EQ(isotp_init_link_xl(&link, &TESTER_METADATA, 2048u, txBuffer.data(), txBuffer.size(), rxBuffer.data(), rxBuffer.size()), ISOTP_RET_OK);

    fill_pattern(payload.data(), 255u);
    ASSERT_EQ(isotp_send(&link, payload.data(), 255u), ISOTP_RET_OK);
    ASSERT_EQ(frames.size(), 1u);
    EXPECT_EQ(frames[0].m_data.size(), 257u);
    EXPECT_EQ(frames[0].m_data[0], 0u);
    EXPECT_EQ(frames[0].m_data[1], 255u);
    EXPECT_TRUE(same_metadata(frames[0].m_metadata, TESTER_METADATA));

    frames.clear();
    fill_pattern(payload.data(), 256u);
    ASSERT_EQ(isotp_send(&link, payload.data(), 256u), ISOTP_RET_OK);
    ASSERT_EQ(frames.size(), 1u);
    EXPECT_EQ(frames[0].m_data.size(), 257u);
    EXPECT_EQ(frames[0].m_data[0], 0x11u);
    EXPECT_EQ(frames[0].m_data[1], 0u);

    const std::array<uint8_t, 3> FLOW_CONTROL{0x30u, 0u, 0u};
    const IsoTpCanXlFrame FC{ECU_METADATA, FLOW_CONTROL.data(), static_cast<uint16_t>(FLOW_CONTROL.size())};
    isotp_on_can_xl_message(&link, &FC);
    isotp_poll(&link);
    ASSERT_EQ(frames.size(), 2u);
    EXPECT_EQ(frames[1].m_data.size(), 2u);
    EXPECT_EQ(frames[1].m_data[0], 0x21u);
    EXPECT_EQ(frames[1].m_data[1], payload[255]);
    EXPECT_EQ(link.send_status, ISOTP_SEND_STATUS_IDLE);
}

TEST_F(CanXlTest, PreservesOverrideMetadataForCompleteTransfer) {
    IsoTpLink sender{};
    IsoTpLink receiver{};
    IsoTpCanXlMetadata overrideMetadata{0x321u, 0xABCDEF01u, 0xA5u, 0x5Au, ISOTP_CAN_XL_FLAG_SEC | ISOTP_CAN_XL_FLAG_RRS};
    ASSERT_EQ(isotp_init_link_xl(&sender, &TESTER_METADATA, 2048u, txBuffer.data(), txBuffer.size(), rxBuffer.data(), rxBuffer.size()), ISOTP_RET_OK);
    ASSERT_EQ(isotp_init_link_xl(&receiver, &ECU_METADATA, 2048u, peerTxBuffer.data(), peerTxBuffer.size(), peerRxBuffer.data(), peerRxBuffer.size()),
              ISOTP_RET_OK);

    fill_pattern(payload.data(), 5000u);
    ASSERT_EQ(isotp_send_can_xl_with_metadata(&sender, &overrideMetadata, payload.data(), 5000u), ISOTP_RET_OK);

    size_t next = 0u;
    for (uint32_t iteration = 0u; iteration < 100u; ++iteration) {
        while (next < frames.size()) {
            const CapturedXlFrame CURRENT = frames[next++];
            if (same_metadata(CURRENT.m_metadata, ECU_METADATA)) {
                deliver(&sender, CURRENT);
            } else {
                EXPECT_TRUE(same_metadata(CURRENT.m_metadata, overrideMetadata));
                deliver(&receiver, CURRENT);
            }
        }
        isotp_poll(&sender);
        isotp_poll(&receiver);
        ++nowUs;
        if (next >= frames.size() && sender.send_status != ISOTP_SEND_STATUS_INPROGRESS) { break; }
    }

    ASSERT_EQ(sender.send_status, ISOTP_SEND_STATUS_IDLE);
    uint32_t outSize = 0u;
    ASSERT_EQ(isotp_receive(&receiver, received.data(), received.size(), &outSize), ISOTP_RET_OK);
    EXPECT_EQ(outSize, 5000u);
    EXPECT_EQ(std::memcmp(received.data(), payload.data(), outSize), 0);
    ASSERT_GE(frames.size(), 4u);
    EXPECT_EQ(frames[0].m_data.size(), 2048u);
    EXPECT_EQ(frames[1].m_data.size(), 3u);
    EXPECT_EQ(frames.back().m_data.size(), 912u);
}

TEST_F(CanXlTest, RejectsMalformedDescriptorsAndRuntimeChanges) {
    IsoTpLink link{};
    ASSERT_EQ(isotp_init_link_xl(&link, &TESTER_METADATA, 512u, txBuffer.data(), txBuffer.size(), rxBuffer.data(), rxBuffer.size()), ISOTP_RET_OK);
    EXPECT_EQ(isotp_send_can_xl_with_metadata(&link, nullptr, payload.data(), 1u), ISOTP_RET_ERROR);

    fill_pattern(payload.data(), 600u);
    ASSERT_EQ(isotp_send(&link, payload.data(), 600u), ISOTP_RET_OK);
    EXPECT_EQ(isotp_set_tx_dl_xl(&link, 128u), ISOTP_RET_INPROGRESS);

    const uint8_t ONE_BYTE = 0u;
    IsoTpCanXlFrame malformed{TESTER_METADATA, &ONE_BYTE, 1u};
    IsoTpLink legacy{};
    isotp_init_link(&legacy, 1u, txBuffer.data(), txBuffer.size(), rxBuffer.data(), rxBuffer.size());
    isotp_on_can_xl_message(&link, nullptr);
    isotp_on_can_xl_message(nullptr, &malformed);
    isotp_on_can_xl_message(&legacy, &malformed);
    isotp_on_can_xl_message(&link, &malformed);
    malformed.size = ISO_TP_MAX_CAN_XL_FRAME_SIZE + 1u;
    isotp_on_can_xl_message(&link, &malformed);
    malformed.size = 2u;
    malformed.data = nullptr;
    isotp_on_can_xl_message(&link, &malformed);
    malformed.data = &ONE_BYTE;
    malformed.metadata.priority_id = 0x800u;
    isotp_on_can_xl_message(&link, &malformed);

    const std::array<uint8_t, 2> LEGACY_FRAME{0x01u, 0xAAu};
    isotp_on_can_message(&link, LEGACY_FRAME.data(), LEGACY_FRAME.size());
}

TEST_F(CanXlTest, PropagatesXlDriverFailuresRetriesAndTimeouts) {
    IsoTpLink link{};
    ASSERT_EQ(isotp_init_link_xl(&link, &TESTER_METADATA, 512u, txBuffer.data(), txBuffer.size(), rxBuffer.data(), rxBuffer.size()), ISOTP_RET_OK);
    fill_pattern(payload.data(), 600u);

    xlSendResult = ISOTP_RET_ERROR;
    EXPECT_EQ(isotp_send(&link, payload.data(), 600u), ISOTP_RET_ERROR);
    EXPECT_EQ(link.send_status, ISOTP_SEND_STATUS_IDLE);

    xlSendResult = ISOTP_RET_OK;
    ASSERT_EQ(isotp_send(&link, payload.data(), 600u), ISOTP_RET_OK);
    const std::array<uint8_t, 3> WAIT{0x31u, 0u, 0u};
    const IsoTpCanXlFrame WAIT_FRAME{ECU_METADATA, WAIT.data(), static_cast<uint16_t>(WAIT.size())};
    isotp_on_can_xl_message(&link, &WAIT_FRAME);
    isotp_on_can_xl_message(&link, &WAIT_FRAME);
    EXPECT_EQ(link.send_status, ISOTP_SEND_STATUS_ERROR);

    ASSERT_EQ(isotp_init_link_xl(&link, &TESTER_METADATA, 512u, txBuffer.data(), txBuffer.size(), rxBuffer.data(), rxBuffer.size()), ISOTP_RET_OK);
    ASSERT_EQ(isotp_send(&link, payload.data(), 600u), ISOTP_RET_OK);
    const std::array<uint8_t, 3> PROCEED{0x30u, 0u, 0u};
    const IsoTpCanXlFrame PROCEED_FRAME{ECU_METADATA, PROCEED.data(), static_cast<uint16_t>(PROCEED.size())};
    isotp_on_can_xl_message(&link, &PROCEED_FRAME);
    xlSendResult = ISOTP_RET_NOSPACE;
    isotp_poll(&link);
    EXPECT_EQ(link.send_status, ISOTP_SEND_STATUS_INPROGRESS);
    xlSendResult = ISOTP_RET_ERROR;
    isotp_poll(&link);
    EXPECT_EQ(link.send_status, ISOTP_SEND_STATUS_ERROR);

    xlSendResult = ISOTP_RET_OK;
    ASSERT_EQ(isotp_init_link_xl(&link, &TESTER_METADATA, 512u, txBuffer.data(), txBuffer.size(), rxBuffer.data(), rxBuffer.size()), ISOTP_RET_OK);
    ASSERT_EQ(isotp_send(&link, payload.data(), 600u), ISOTP_RET_OK);
    nowUs = ISO_TP_DEFAULT_RESPONSE_TIMEOUT_US + 1u;
    isotp_poll(&link);
    EXPECT_EQ(link.send_protocol_result, ISOTP_PROTOCOL_RESULT_TIMEOUT_BS);
}

#ifdef ISO_TP_USER_SEND_CAN_ARG
TEST_F(CanXlTest, PassesTheOptionalUserArgumentToTheXlMock) {
    IsoTpLink link{};
    int marker = 0;
    ASSERT_EQ(isotp_init_link_xl(&link, &TESTER_METADATA, 64u, txBuffer.data(), txBuffer.size(), rxBuffer.data(), rxBuffer.size()), ISOTP_RET_OK);
    link.user_send_can_arg = &marker;
    expectedArgument = &marker;
    ASSERT_EQ(isotp_send(&link, payload.data(), 1u), ISOTP_RET_OK);
}
#endif

#ifdef ISO_TP_TRANSMIT_COMPLETE_CALLBACK
namespace {
uint32_t txCallbackCount;
void on_tx_complete(void*, uint32_t size, void*) {
    EXPECT_EQ(size, 1u);
    ++txCallbackCount;
}
}

TEST_F(CanXlTest, InvokesTransmitCompletionCallback) {
    IsoTpLink link{};
    txCallbackCount = 0u;
    ASSERT_EQ(isotp_init_link_xl(&link, &TESTER_METADATA, 64u, txBuffer.data(), txBuffer.size(), rxBuffer.data(), rxBuffer.size()), ISOTP_RET_OK);
    isotp_set_tx_done_cb(&link, on_tx_complete, nullptr);
    ASSERT_EQ(isotp_send(&link, payload.data(), 1u), ISOTP_RET_OK);
    EXPECT_EQ(txCallbackCount, 1u);
}
#endif

#ifdef ISO_TP_RECEIVE_COMPLETE_CALLBACK
namespace {
uint32_t rxCallbackCount;
void* rxCallbackLink;

void on_rx_complete(void* link, const uint8_t* data, uint32_t size, void*) {
    rxCallbackLink = link;
    ++rxCallbackCount;
    ASSERT_EQ(size, 3u);
    EXPECT_EQ(data[0], 0xA1u);
    EXPECT_EQ(data[1], 0xB2u);
    EXPECT_EQ(data[2], 0xC3u);
}
}

TEST_F(CanXlTest, InvokesReceiveCompletionCallback) {
    IsoTpLink link{};
    rxCallbackCount = 0u;
    rxCallbackLink = nullptr;
    ASSERT_EQ(isotp_init_link_xl(&link, &TESTER_METADATA, 64u, txBuffer.data(), txBuffer.size(), rxBuffer.data(), rxBuffer.size()), ISOTP_RET_OK);
    isotp_set_rx_done_cb(&link, on_rx_complete, nullptr);

    const std::array<uint8_t, 4> DATA{0x03u, 0xA1u, 0xB2u, 0xC3u};
    const IsoTpCanXlFrame FRAME{ECU_METADATA, DATA.data(), static_cast<uint16_t>(DATA.size())};
    isotp_on_can_xl_message(&link, &FRAME);

    EXPECT_EQ(rxCallbackCount, 1u);
    EXPECT_EQ(rxCallbackLink, &link);
}
#endif

#ifdef ISO_TP_ENABLE_STREAMING
TEST_F(CanXlTest, StreamsFramesLargerThanTheReceiveBuffer) {
    IsoTpLink sender{};
    IsoTpLink receiver{};
    ASSERT_EQ(isotp_init_link_xl(&sender, &TESTER_METADATA, 512u, txBuffer.data(), txBuffer.size(), rxBuffer.data(), rxBuffer.size()), ISOTP_RET_OK);
    ASSERT_EQ(isotp_init_link_xl(&receiver, &ECU_METADATA, 512u, peerTxBuffer.data(), peerTxBuffer.size(), peerRxBuffer.data(), 128u), ISOTP_RET_OK);

    fill_pattern(payload.data(), 1200u);
    ASSERT_EQ(isotp_send(&sender, payload.data(), 1200u), ISOTP_RET_OK);

    size_t next = 0u;
    uint32_t total = 0u;
    bool complete = false;
    for (uint32_t iteration = 0u; iteration < 100u && !complete; ++iteration) {
        while (next < frames.size()) {
            const CapturedXlFrame CURRENT = frames[next++];
            deliver(same_metadata(CURRENT.m_metadata, ECU_METADATA) ? &sender : &receiver, CURRENT);
        }
        if (receiver.receive_status == ISOTP_RECEIVE_STATUS_FULL) {
            uint32_t chunkSize = 0u;
            ASSERT_EQ(isotp_receive_streaming(&receiver, received.data() + total, received.size() - total, &chunkSize, &complete), ISOTP_RET_OK);
            total += chunkSize;
        }
        isotp_poll(&sender);
        isotp_poll(&receiver);
        ++nowUs;
    }

    EXPECT_TRUE(complete);
    EXPECT_EQ(total, 1200u);
    EXPECT_EQ(std::memcmp(received.data(), payload.data(), total), 0);
    EXPECT_EQ(sender.send_status, ISOTP_SEND_STATUS_IDLE);
}
#endif
