#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#ifdef isotpc_USE_INCLUDE_DIR
    #include "isotp_c/isotp.h"
#else
    #include "isotp.h"
#endif // isotpc_USE_INCLUDE_DIR

#include "mocks/isotp_user_mock.hpp"

#define TEST_OUTPUT_CAPACITY 8192u
#define TEST_CHUNK_CAPACITY 1024u

using IsoTpMockCanFrame = struct {
        uint32_t m_arbitrationId;
        uint8_t  m_data[ISO_TP_MAX_CAN_FRAME_SIZE];
        uint8_t  m_size;
};

static std::vector<IsoTpMockCanFrame> gCanFrames;
static std::vector<std::string>       gDebugMessages;
static uint32_t                       gCurrentTimeUs;
static size_t                         gRxCallbackCount;
static std::vector<uint8_t>           gRxCallbackData;
static void*                          gRxCallbackLink;
static void*                          gRxCallbackArg;

static void                           reset_platform_state(void) {
    gCanFrames.clear();
    gDebugMessages.clear();
    gCurrentTimeUs   = 0;
    gRxCallbackCount = 0;
    gRxCallbackData.clear();
    gRxCallbackLink = nullptr;
    gRxCallbackArg  = nullptr;
}

static int capture_can_frame(uint32_t arbitrationId, const uint8_t* data, uint8_t size, uint8_t, void*) {
    IsoTpMockCanFrame frame = {};
    frame.m_arbitrationId    = arbitrationId;
    frame.m_size              = size;
    std::memcpy(frame.m_data, data, size);
    gCanFrames.push_back(frame);
    return ISOTP_RET_OK;
}

static size_t                   isotp_mock_can_frame_count(void) { return gCanFrames.size(); }
static const IsoTpMockCanFrame* isotp_mock_can_frame(size_t index) { return index < gCanFrames.size() ? &gCanFrames[index] : nullptr; }
static size_t                   isotp_mock_debug_count(void) { return gDebugMessages.size(); }
static void                     isotp_mock_set_time_us(uint32_t timeUs) { gCurrentTimeUs = timeUs; }
static void                     isotp_mock_advance_time_us(uint32_t elapsedUs) { gCurrentTimeUs += elapsedUs; }
static void                     isotp_mock_rx_done_cb(void* link, const uint8_t* data, uint32_t size, void* argument) {
    ++gRxCallbackCount;
    gRxCallbackLink = link;
    gRxCallbackArg  = argument;
    gRxCallbackData.assign(data, data + size);
}
static size_t         isotp_mock_rx_callback_count(void) { return gRxCallbackCount; }
static const uint8_t* isotp_mock_rx_callback_data(void) { return gRxCallbackData.data(); }
static uint32_t       isotp_mock_rx_callback_size(void) { return static_cast<uint32_t>(gRxCallbackData.size()); }
static void*          isotp_mock_rx_callback_link(void) { return gRxCallbackLink; }
static void*          isotp_mock_rx_callback_arg(void) { return gRxCallbackArg; }

class StreamingTest: public testing::Test {
    protected:
        void SetUp() override {
            reset_platform_state();
            isotp_set_user_mock(&m_user);
            ON_CALL(m_user, get_us()).WillByDefault([] { return gCurrentTimeUs; });
            ON_CALL(m_user, send_can(testing::_, testing::_, testing::_, testing::_, testing::_)).WillByDefault(capture_can_frame);
            ON_CALL(m_user, debug(testing::_)).WillByDefault([](const std::string& message) { gDebugMessages.push_back(message); });
        }

        void                             TearDown() override { isotp_set_user_mock(nullptr); }

        testing::NiceMock<IsoTpUserMock> m_user;
};

using testlink_t = struct testlink_t {
        IsoTpLink               m_link;
        std::array<uint8_t, 8>  m_sendBuffer{};
        std::array<uint8_t, 64> m_receiveBuffer{};
};

using streamresult_t = struct streamresult_t {
        std::array<uint8_t, TEST_OUTPUT_CAPACITY> m_output{};
        uint32_t                                  m_outputSize;
        std::array<uint32_t, TEST_CHUNK_CAPACITY> m_chunkSizes{};
        size_t                                    m_chunkCount;
        size_t                                    m_completeCount;
};

static void fill_payload(uint8_t* payload, uint32_t size) {
    for (uint32_t index = 0; index < size; ++index) { payload[index] = (uint8_t)((index * 37u + 11u) & 0xffu); }
}

static void init_test_link(testlink_t* testLink, uint32_t receiveBufferSize) {
    reset_platform_state();
    *testLink = {};
    isotp_init_link(&testLink->m_link, 0x731, testLink->m_sendBuffer.data(), sizeof(testLink->m_sendBuffer), testLink->m_receiveBuffer.data(),
                    receiveBufferSize);
}

static uint32_t inject_first_frame(IsoTpLink* link, const uint8_t* payload, uint32_t payloadSize) {
    std::array<uint8_t, 8> frame{};
    uint32_t               dataSize;

    if (payloadSize <= 4095u) {
        frame[0]  = (uint8_t)(0x10u | (payloadSize >> 8));
        frame[1]  = (uint8_t)payloadSize;
        dataSize = 6;
        (void)memcpy(frame.data() + 2, payload, dataSize);
    } else {
        frame[0]  = 0x10;
        frame[1]  = 0;
        frame[2]  = (uint8_t)(payloadSize >> 24);
        frame[3]  = (uint8_t)(payloadSize >> 16);
        frame[4]  = (uint8_t)(payloadSize >> 8);
        frame[5]  = (uint8_t)payloadSize;
        dataSize = 2;
        (void)memcpy(frame.data() + 6, payload, dataSize);
    }

    isotp_on_can_message(link, frame.data(), sizeof(frame));
    return dataSize;
}

static void inject_consecutive_frame(IsoTpLink* link, const uint8_t* payload, uint32_t payloadSize, uint32_t* offset, uint8_t* sequenceNumber) {
    std::array<uint8_t, 8> frame{};
    uint32_t               dataSize = payloadSize - *offset;

    if (dataSize > 7u) { dataSize = 7; }
    frame[0] = (uint8_t)(0x20u | *sequenceNumber);
    (void)memcpy(frame.data() + 1, payload + *offset, dataSize);
    isotp_on_can_message(link, frame.data(), (uint8_t)(dataSize + 1u));

    *offset += dataSize;
    *sequenceNumber = (uint8_t)((*sequenceNumber + 1u) & 0x0fu);
}

static void drain_available_chunks(IsoTpLink* link, streamresult_t* result) {
    while (link->receive_status == ISOTP_RECEIVE_STATUS_FULL) {
        uint32_t chunkSize  = 0;
        bool     isComplete = false;
        int      returnCode;

        EXPECT_TRUE(result->m_chunkCount < TEST_CHUNK_CAPACITY);
        EXPECT_TRUE(result->m_outputSize < TEST_OUTPUT_CAPACITY);

        returnCode =
            isotp_receive_streaming(link, result->m_output.data() + result->m_outputSize, TEST_OUTPUT_CAPACITY - result->m_outputSize, &chunkSize, &isComplete);
        EXPECT_TRUE(returnCode == ISOTP_RET_OK);
        result->m_chunkSizes[result->m_chunkCount++] = chunkSize;
        result->m_outputSize += chunkSize;
        if (isComplete) { ++result->m_completeCount; }
    }
}

static void run_stream_transfer(uint32_t payloadSize, uint32_t receiveBufferSize, streamresult_t* result) {
    testlink_t                                testLink;
    std::array<uint8_t, TEST_OUTPUT_CAPACITY> payload{};
    uint32_t                                  offset;
    uint8_t                                   sequenceNumber = 1;

    EXPECT_TRUE(payloadSize <= sizeof(payload));
    EXPECT_TRUE(receiveBufferSize <= sizeof(testLink.m_receiveBuffer));

    fill_payload(payload.data(), payloadSize);
    *result = {};
    init_test_link(&testLink, receiveBufferSize);

    offset = inject_first_frame(&testLink.m_link, payload.data(), payloadSize);
    drain_available_chunks(&testLink.m_link, result);

    while (offset < payloadSize) {
        EXPECT_TRUE(testLink.m_link.receive_status == ISOTP_RECEIVE_STATUS_INPROGRESS);
        inject_consecutive_frame(&testLink.m_link, payload.data(), payloadSize, &offset, &sequenceNumber);
        drain_available_chunks(&testLink.m_link, result);
    }

    EXPECT_TRUE(result->m_outputSize == payloadSize);
    EXPECT_TRUE(memcmp(result->m_output.data(), payload.data(), payloadSize) == 0);
    EXPECT_TRUE(result->m_completeCount == 1u);
    EXPECT_TRUE(testLink.m_link.receive_status == ISOTP_RECEIVE_STATUS_IDLE);
}

static void check_flow_control_frames(uint8_t expectedBlockSize, size_t expectedCount) {
    EXPECT_TRUE(isotp_mock_can_frame_count() == expectedCount);
    for (size_t index = 0; index < expectedCount; ++index) {
        const IsoTpMockCanFrame* frame = isotp_mock_can_frame(index);

        EXPECT_TRUE(frame != nullptr);
        EXPECT_TRUE(frame->m_arbitrationId == 0x731u);
        EXPECT_TRUE(frame->m_size == 8u);
        EXPECT_TRUE((frame->m_data[0] >> 4) == ISOTP_PCI_TYPE_FLOW_CONTROL_FRAME);
        EXPECT_TRUE((frame->m_data[0] & 0x0fu) == PCI_FLOW_STATUS_CONTINUE);
        EXPECT_TRUE(frame->m_data[1] == expectedBlockSize);
    }
}

TEST_F(StreamingTest, ReceivesSingleFrame) {
    testlink_t             testLink;
    std::array<uint8_t, 4> frame{0x03, 0xa1, 0xb2, 0xc3};
    std::array<uint8_t, 8> output{};
    uint32_t               outputSize = 0;
    bool                   isComplete = false;

    init_test_link(&testLink, sizeof(testLink.m_receiveBuffer));
    isotp_on_can_message(&testLink.m_link, frame.data(), sizeof(frame));

    EXPECT_TRUE(isotp_receive_streaming(&testLink.m_link, output.data(), sizeof(output), &outputSize, &isComplete) == ISOTP_RET_OK);
    EXPECT_TRUE(outputSize == 3u);
    EXPECT_TRUE(isComplete);
    EXPECT_TRUE(memcmp(output.data(), frame.data() + 1, outputSize) == 0);
    EXPECT_TRUE(isotp_mock_can_frame_count() == 0u);
}

TEST_F(StreamingTest, ReceivesMultiFrameThatFitsBuffer) {
    testlink_t              testLink;
    std::array<uint8_t, 20> payload{};
    std::array<uint8_t, 20> output{};
    uint32_t                outputSize = 0;
    uint32_t                offset;
    uint8_t                 sequenceNumber = 1;
    bool                    isComplete     = false;

    fill_payload(payload.data(), sizeof(payload));
    init_test_link(&testLink, 32);
    offset = inject_first_frame(&testLink.m_link, payload.data(), sizeof(payload));
    check_flow_control_frames(ISO_TP_DEFAULT_BLOCK_SIZE, 1);

    while (offset < sizeof(payload)) { inject_consecutive_frame(&testLink.m_link, payload.data(), sizeof(payload), &offset, &sequenceNumber); }

    EXPECT_TRUE(isotp_receive_streaming(&testLink.m_link, output.data(), sizeof(output), &outputSize, &isComplete) == ISOTP_RET_OK);
    EXPECT_TRUE(outputSize == sizeof(payload));
    EXPECT_TRUE(isComplete);
    EXPECT_TRUE(memcmp(output.data(), payload.data(), sizeof(payload)) == 0);
}

TEST_F(StreamingTest, PreservesShortChunkBoundaries) {
    streamresult_t result;

    run_stream_transfer(25, 8, &result);
    EXPECT_TRUE(result.m_chunkCount == 4u);
    EXPECT_TRUE(result.m_chunkSizes[0] == 8u);
    EXPECT_TRUE(result.m_chunkSizes[1] == 8u);
    EXPECT_TRUE(result.m_chunkSizes[2] == 8u);
    EXPECT_TRUE(result.m_chunkSizes[3] == 1u);
    check_flow_control_frames(1, 3);
}

TEST_F(StreamingTest, SupportsOneByteReceiveBuffer) {
    streamresult_t result;

    run_stream_transfer(20, 1, &result);
    EXPECT_TRUE(result.m_chunkCount == 20u);
    for (size_t index = 0; index < result.m_chunkCount; ++index) { EXPECT_TRUE(result.m_chunkSizes[index] == 1u); }
    check_flow_control_frames(1, 2);
}

TEST_F(StreamingTest, ReceivesIsoTp2016LongMessage) {
    streamresult_t result;

    run_stream_transfer(4100, 13, &result);
    EXPECT_TRUE(result.m_chunkCount == 316u);
    EXPECT_TRUE(result.m_chunkSizes[0] == 13u);
    EXPECT_TRUE(result.m_chunkSizes[result.m_chunkCount - 1u] == 5u);
    check_flow_control_frames(1, 586);
}

TEST_F(StreamingTest, DoesNotConsumeChunkWhenDestinationIsTooSmall) {
    testlink_t              testLink;
    std::array<uint8_t, 20> payload{};
    std::array<uint8_t, 8>  output{};
    uint32_t                outputSize = 1234;
    uint32_t                offset;
    uint8_t                 sequenceNumber = 1;
    bool                    isComplete     = true;

    fill_payload(payload.data(), sizeof(payload));
    init_test_link(&testLink, 8);
    offset = inject_first_frame(&testLink.m_link, payload.data(), sizeof(payload));
    inject_consecutive_frame(&testLink.m_link, payload.data(), sizeof(payload), &offset, &sequenceNumber);
    EXPECT_TRUE(testLink.m_link.receive_status == ISOTP_RECEIVE_STATUS_FULL);

    EXPECT_TRUE(isotp_receive_streaming(&testLink.m_link, output.data(), 7, &outputSize, &isComplete) == ISOTP_RET_NOSPACE);
    EXPECT_TRUE(testLink.m_link.receive_status == ISOTP_RECEIVE_STATUS_FULL);
    EXPECT_TRUE(outputSize == 1234u);
    EXPECT_TRUE(isComplete);
    EXPECT_TRUE(isotp_mock_can_frame_count() == 1u);

    EXPECT_TRUE(isotp_receive_streaming(&testLink.m_link, output.data(), sizeof(output), &outputSize, &isComplete) == ISOTP_RET_OK);
    EXPECT_TRUE(outputSize == sizeof(output));
    EXPECT_TRUE(!isComplete);
    EXPECT_TRUE(memcmp(output.data(), payload.data(), sizeof(output)) == 0);
    EXPECT_TRUE(isotp_mock_can_frame_count() == 2u);
}

TEST_F(StreamingTest, RejectsInvalidArgumentsAndLegacyReceive) {
    testlink_t              testLink;
    std::array<uint8_t, 20> payload{};
    std::array<uint8_t, 32> output{};
    uint32_t                outputSize = 0;
    uint32_t                offset;
    uint8_t                 sequenceNumber = 1;
    bool                    isComplete     = false;

    fill_payload(payload.data(), sizeof(payload));
    init_test_link(&testLink, 8);

    EXPECT_TRUE(isotp_receive_streaming(&testLink.m_link, output.data(), sizeof(output), &outputSize, &isComplete) == ISOTP_RET_NO_DATA);
    EXPECT_TRUE(isotp_receive_streaming(nullptr, output.data(), sizeof(output), &outputSize, &isComplete) == ISOTP_RET_ERROR);
    EXPECT_TRUE(isotp_receive_streaming(&testLink.m_link, nullptr, sizeof(output), &outputSize, &isComplete) == ISOTP_RET_ERROR);
    EXPECT_TRUE(isotp_receive_streaming(&testLink.m_link, output.data(), sizeof(output), nullptr, &isComplete) == ISOTP_RET_ERROR);
    EXPECT_TRUE(isotp_receive_streaming(&testLink.m_link, output.data(), sizeof(output), &outputSize, nullptr) == ISOTP_RET_ERROR);

    offset = inject_first_frame(&testLink.m_link, payload.data(), sizeof(payload));
    inject_consecutive_frame(&testLink.m_link, payload.data(), sizeof(payload), &offset, &sequenceNumber);
    EXPECT_TRUE(isotp_receive(&testLink.m_link, output.data(), sizeof(output), &outputSize) == ISOTP_RET_ERROR);
    EXPECT_TRUE(testLink.m_link.receive_status == ISOTP_RECEIVE_STATUS_FULL);
}

TEST_F(StreamingTest, CoexistsWithReceiveCallback) {
    testlink_t              testLink;
    streamresult_t          result{};
    std::array<uint8_t, 4>  singleFrame{0x03, 0xde, 0xad, 0x42};
    std::array<uint8_t, 20> payload{};
    uint32_t                offset;
    uint8_t                 sequenceNumber   = 1;
    int32_t                 callbackArgument = 17;

    fill_payload(payload.data(), sizeof(payload));
    init_test_link(&testLink, 8);
    isotp_set_rx_done_cb(&testLink.m_link, isotp_mock_rx_done_cb, &callbackArgument);

    isotp_on_can_message(&testLink.m_link, singleFrame.data(), sizeof(singleFrame));
    EXPECT_TRUE(isotp_mock_rx_callback_count() == 1u);
    EXPECT_TRUE(isotp_mock_rx_callback_size() == 3u);
    EXPECT_TRUE(memcmp(isotp_mock_rx_callback_data(), singleFrame.data() + 1, 3) == 0);
    EXPECT_TRUE(isotp_mock_rx_callback_link() == &testLink.m_link);
    EXPECT_TRUE(isotp_mock_rx_callback_arg() == &callbackArgument);

    offset = inject_first_frame(&testLink.m_link, payload.data(), sizeof(payload));
    inject_consecutive_frame(&testLink.m_link, payload.data(), sizeof(payload), &offset, &sequenceNumber);
    EXPECT_TRUE(isotp_mock_rx_callback_count() == 1u);
    drain_available_chunks(&testLink.m_link, &result);

    while (offset < sizeof(payload)) {
        inject_consecutive_frame(&testLink.m_link, payload.data(), sizeof(payload), &offset, &sequenceNumber);
        drain_available_chunks(&testLink.m_link, &result);
    }

    EXPECT_TRUE(result.m_outputSize == sizeof(payload));
    EXPECT_TRUE(memcmp(result.m_output.data(), payload.data(), sizeof(payload)) == 0);
    EXPECT_TRUE(result.m_completeCount == 1u);
    EXPECT_TRUE(isotp_mock_rx_callback_count() == 1u);
}

TEST_F(StreamingTest, RejectsZeroSizedReceiveBuffer) {
    testlink_t               testLink;
    std::array<uint8_t, 20>  payload{};
    const IsoTpMockCanFrame* flowControl;

    fill_payload(payload.data(), sizeof(payload));
    init_test_link(&testLink, 0);
    (void)inject_first_frame(&testLink.m_link, payload.data(), sizeof(payload));

    EXPECT_TRUE(testLink.m_link.receive_status == ISOTP_RECEIVE_STATUS_IDLE);
    EXPECT_TRUE(testLink.m_link.receive_protocol_result == ISOTP_PROTOCOL_RESULT_BUFFER_OVFLW);
    EXPECT_TRUE(isotp_mock_can_frame_count() == 1u);
    flowControl = isotp_mock_can_frame(0);
    EXPECT_TRUE(flowControl != nullptr);
    EXPECT_TRUE((flowControl->m_data[0] >> 4) == ISOTP_PCI_TYPE_FLOW_CONTROL_FRAME);
    EXPECT_TRUE((flowControl->m_data[0] & 0x0fu) == PCI_FLOW_STATUS_OVERFLOW);
    EXPECT_TRUE(flowControl->m_data[1] == 0u);
    EXPECT_TRUE(isotp_mock_debug_count() == 1u);
}

TEST_F(StreamingTest, AbortsOnWrongSequenceNumber) {
    testlink_t              testLink;
    std::array<uint8_t, 20> payload{};
    std::array<uint8_t, 8>  wrongFrame{0x22};

    fill_payload(payload.data(), sizeof(payload));
    (void)memcpy(wrongFrame.data() + 1, payload.data() + 6, 7);
    init_test_link(&testLink, 8);
    (void)inject_first_frame(&testLink.m_link, payload.data(), sizeof(payload));
    isotp_on_can_message(&testLink.m_link, wrongFrame.data(), sizeof(wrongFrame));

    EXPECT_TRUE(testLink.m_link.receive_status == ISOTP_RECEIVE_STATUS_IDLE);
    EXPECT_TRUE(testLink.m_link.receive_protocol_result == ISOTP_PROTOCOL_RESULT_WRONG_SN);
}

TEST_F(StreamingTest, AbortsOnReceiveTimeout) {
    testlink_t              testLink;
    std::array<uint8_t, 20> payload{};

    fill_payload(payload.data(), sizeof(payload));
    init_test_link(&testLink, 16);
    isotp_mock_set_time_us(100);
    (void)inject_first_frame(&testLink.m_link, payload.data(), sizeof(payload));
    EXPECT_TRUE(testLink.m_link.receive_status == ISOTP_RECEIVE_STATUS_INPROGRESS);

    isotp_mock_advance_time_us(ISO_TP_DEFAULT_RESPONSE_TIMEOUT_US + 1u);
    isotp_poll(&testLink.m_link);
    EXPECT_TRUE(testLink.m_link.receive_status == ISOTP_RECEIVE_STATUS_IDLE);
    EXPECT_TRUE(testLink.m_link.receive_protocol_result == ISOTP_PROTOCOL_RESULT_TIMEOUT_CR);
}

TEST_F(StreamingTest, RejectsMalformedFrames) {
    testlink_t              testLink{};
    std::array<uint8_t, 20> payload{};
    std::array<uint8_t, 2>  shortSingleFrame{0x03, 0xaa};
    std::array<uint8_t, 7>  shortFirstFrame{0x10, 0x14};
    std::array<uint8_t, 8>  invalidFirstFrame{0x10, 0x07};
    std::array<uint8_t, 2>  shortConsecutiveFrame{0x21, 0xaa};

    EXPECT_CALL(m_user, debug(testing::_)).Times(4);
    fill_payload(payload.data(), sizeof(payload));

    init_test_link(&testLink, 16);
    isotp_on_can_message(&testLink.m_link, shortSingleFrame.data(), sizeof(shortSingleFrame));
    EXPECT_TRUE(testLink.m_link.receive_status == ISOTP_RECEIVE_STATUS_IDLE);
    EXPECT_TRUE(isotp_mock_debug_count() == 1u);

    init_test_link(&testLink, 16);
    isotp_on_can_message(&testLink.m_link, shortFirstFrame.data(), sizeof(shortFirstFrame));
    EXPECT_TRUE(testLink.m_link.receive_status == ISOTP_RECEIVE_STATUS_IDLE);
    EXPECT_TRUE(isotp_mock_debug_count() == 1u);

    init_test_link(&testLink, 16);
    isotp_on_can_message(&testLink.m_link, invalidFirstFrame.data(), sizeof(invalidFirstFrame));
    EXPECT_TRUE(testLink.m_link.receive_status == ISOTP_RECEIVE_STATUS_IDLE);
    EXPECT_TRUE(isotp_mock_debug_count() == 1u);

    init_test_link(&testLink, 16);
    (void)inject_first_frame(&testLink.m_link, payload.data(), sizeof(payload));
    isotp_on_can_message(&testLink.m_link, shortConsecutiveFrame.data(), sizeof(shortConsecutiveFrame));
    EXPECT_TRUE(testLink.m_link.receive_status == ISOTP_RECEIVE_STATUS_INPROGRESS);
    EXPECT_TRUE(isotp_mock_debug_count() == 1u);
}
