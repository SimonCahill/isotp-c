#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "isotp.h"
#include "mocks/isotp_user_mock.h"

#define TEST_OUTPUT_CAPACITY 8192u
#define TEST_CHUNK_CAPACITY 1024u

#define CHECK(condition)                                                                                                                \
    do {                                                                                                                                \
        if (!(condition)) {                                                                                                             \
            (void)fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #condition);                                       \
            return 1;                                                                                                                   \
        }                                                                                                                               \
    } while (0)

typedef struct TestLink {
    IsoTpLink link;
    uint8_t send_buffer[8];
    uint8_t receive_buffer[64];
} TestLink;

typedef struct StreamResult {
    uint8_t output[TEST_OUTPUT_CAPACITY];
    uint32_t output_size;
    uint32_t chunk_sizes[TEST_CHUNK_CAPACITY];
    size_t chunk_count;
    size_t complete_count;
} StreamResult;

static void fill_payload(uint8_t* payload, uint32_t size) {
    uint32_t index;

    for (index = 0; index < size; ++index) { payload[index] = (uint8_t)((index * 37u + 11u) & 0xffu); }
}

static void init_test_link(TestLink* test_link, uint32_t receive_buffer_size) {
    isotp_mock_reset();
    (void)memset(test_link, 0, sizeof(*test_link));
    isotp_init_link(&test_link->link, 0x731, test_link->send_buffer, sizeof(test_link->send_buffer), test_link->receive_buffer, receive_buffer_size);
}

static uint32_t inject_first_frame(IsoTpLink* link, const uint8_t* payload, uint32_t payload_size) {
    uint8_t frame[8] = {0};
    uint32_t data_size;

    if (payload_size <= 4095u) {
        frame[0] = (uint8_t)(0x10u | (payload_size >> 8));
        frame[1] = (uint8_t)payload_size;
        data_size = 6;
        (void)memcpy(frame + 2, payload, data_size);
    } else {
        frame[0] = 0x10;
        frame[1] = 0;
        frame[2] = (uint8_t)(payload_size >> 24);
        frame[3] = (uint8_t)(payload_size >> 16);
        frame[4] = (uint8_t)(payload_size >> 8);
        frame[5] = (uint8_t)payload_size;
        data_size = 2;
        (void)memcpy(frame + 6, payload, data_size);
    }

    isotp_on_can_message(link, frame, sizeof(frame));
    return data_size;
}

static void inject_consecutive_frame(IsoTpLink* link, const uint8_t* payload, uint32_t payload_size, uint32_t* offset, uint8_t* sequence_number) {
    uint8_t frame[8] = {0};
    uint32_t data_size = payload_size - *offset;

    if (data_size > 7u) { data_size = 7; }
    frame[0] = (uint8_t)(0x20u | *sequence_number);
    (void)memcpy(frame + 1, payload + *offset, data_size);
    isotp_on_can_message(link, frame, (uint8_t)(data_size + 1u));

    *offset += data_size;
    *sequence_number = (uint8_t)((*sequence_number + 1u) & 0x0fu);
}

static int drain_available_chunks(IsoTpLink* link, StreamResult* result) {
    while (link->receive_status == ISOTP_RECEIVE_STATUS_FULL) {
        uint32_t chunk_size = 0;
        bool is_complete = false;
        int return_code;

        CHECK(result->chunk_count < TEST_CHUNK_CAPACITY);
        CHECK(result->output_size < TEST_OUTPUT_CAPACITY);

        return_code = isotp_receive_streaming(link, result->output + result->output_size,
                                              TEST_OUTPUT_CAPACITY - result->output_size, &chunk_size, &is_complete);
        CHECK(return_code == ISOTP_RET_OK);
        result->chunk_sizes[result->chunk_count++] = chunk_size;
        result->output_size += chunk_size;
        if (is_complete) { ++result->complete_count; }
    }

    return 0;
}

static int run_stream_transfer(uint32_t payload_size, uint32_t receive_buffer_size, StreamResult* result) {
    TestLink test_link;
    uint8_t payload[TEST_OUTPUT_CAPACITY];
    uint32_t offset;
    uint8_t sequence_number = 1;

    CHECK(payload_size <= sizeof(payload));
    CHECK(receive_buffer_size <= sizeof(test_link.receive_buffer));

    fill_payload(payload, payload_size);
    (void)memset(result, 0, sizeof(*result));
    init_test_link(&test_link, receive_buffer_size);

    offset = inject_first_frame(&test_link.link, payload, payload_size);
    CHECK(drain_available_chunks(&test_link.link, result) == 0);

    while (offset < payload_size) {
        CHECK(test_link.link.receive_status == ISOTP_RECEIVE_STATUS_INPROGRESS);
        inject_consecutive_frame(&test_link.link, payload, payload_size, &offset, &sequence_number);
        CHECK(drain_available_chunks(&test_link.link, result) == 0);
    }

    CHECK(result->output_size == payload_size);
    CHECK(memcmp(result->output, payload, payload_size) == 0);
    CHECK(result->complete_count == 1u);
    CHECK(test_link.link.receive_status == ISOTP_RECEIVE_STATUS_IDLE);
    return 0;
}

static int check_flow_control_frames(uint8_t expected_block_size, size_t expected_count) {
    size_t index;

    CHECK(isotp_mock_can_frame_count() == expected_count);
    for (index = 0; index < expected_count; ++index) {
        const IsoTpMockCanFrame* frame = isotp_mock_can_frame(index);

        CHECK(frame != NULL);
        CHECK(frame->arbitration_id == 0x731u);
        CHECK(frame->size == 8u);
        CHECK((frame->data[0] >> 4) == ISOTP_PCI_TYPE_FLOW_CONTROL_FRAME);
        CHECK((frame->data[0] & 0x0fu) == PCI_FLOW_STATUS_CONTINUE);
        CHECK(frame->data[1] == expected_block_size);
    }
    return 0;
}

static int test_single_frame(void) {
    TestLink test_link;
    uint8_t frame[] = {0x03, 0xa1, 0xb2, 0xc3};
    uint8_t output[8] = {0};
    uint32_t output_size = 0;
    bool is_complete = false;

    init_test_link(&test_link, sizeof(test_link.receive_buffer));
    isotp_on_can_message(&test_link.link, frame, sizeof(frame));

    CHECK(isotp_receive_streaming(&test_link.link, output, sizeof(output), &output_size, &is_complete) == ISOTP_RET_OK);
    CHECK(output_size == 3u);
    CHECK(is_complete);
    CHECK(memcmp(output, frame + 1, output_size) == 0);
    CHECK(isotp_mock_can_frame_count() == 0u);
    return 0;
}

static int test_multiframe_fitting_receive_buffer(void) {
    TestLink test_link;
    uint8_t payload[20];
    uint8_t output[20] = {0};
    uint32_t output_size = 0;
    uint32_t offset;
    uint8_t sequence_number = 1;
    bool is_complete = false;

    fill_payload(payload, sizeof(payload));
    init_test_link(&test_link, 32);
    offset = inject_first_frame(&test_link.link, payload, sizeof(payload));
    CHECK(check_flow_control_frames(ISO_TP_DEFAULT_BLOCK_SIZE, 1) == 0);

    while (offset < sizeof(payload)) { inject_consecutive_frame(&test_link.link, payload, sizeof(payload), &offset, &sequence_number); }

    CHECK(isotp_receive_streaming(&test_link.link, output, sizeof(output), &output_size, &is_complete) == ISOTP_RET_OK);
    CHECK(output_size == sizeof(payload));
    CHECK(is_complete);
    CHECK(memcmp(output, payload, sizeof(payload)) == 0);
    return 0;
}

static int test_short_stream_chunk_boundaries(void) {
    StreamResult result;

    CHECK(run_stream_transfer(25, 8, &result) == 0);
    CHECK(result.chunk_count == 4u);
    CHECK(result.chunk_sizes[0] == 8u);
    CHECK(result.chunk_sizes[1] == 8u);
    CHECK(result.chunk_sizes[2] == 8u);
    CHECK(result.chunk_sizes[3] == 1u);
    CHECK(check_flow_control_frames(1, 3) == 0);
    return 0;
}

static int test_one_byte_receive_buffer(void) {
    StreamResult result;
    size_t index;

    CHECK(run_stream_transfer(20, 1, &result) == 0);
    CHECK(result.chunk_count == 20u);
    for (index = 0; index < result.chunk_count; ++index) { CHECK(result.chunk_sizes[index] == 1u); }
    CHECK(check_flow_control_frames(1, 2) == 0);
    return 0;
}

static int test_long_2016_message(void) {
    StreamResult result;

    CHECK(run_stream_transfer(4100, 13, &result) == 0);
    CHECK(result.chunk_count == 316u);
    CHECK(result.chunk_sizes[0] == 13u);
    CHECK(result.chunk_sizes[result.chunk_count - 1u] == 5u);
    CHECK(check_flow_control_frames(1, 586) == 0);
    return 0;
}

static int test_destination_too_small_does_not_consume_chunk(void) {
    TestLink test_link;
    uint8_t payload[20];
    uint8_t output[8] = {0};
    uint32_t output_size = 1234;
    uint32_t offset;
    uint8_t sequence_number = 1;
    bool is_complete = true;

    fill_payload(payload, sizeof(payload));
    init_test_link(&test_link, 8);
    offset = inject_first_frame(&test_link.link, payload, sizeof(payload));
    inject_consecutive_frame(&test_link.link, payload, sizeof(payload), &offset, &sequence_number);
    CHECK(test_link.link.receive_status == ISOTP_RECEIVE_STATUS_FULL);

    CHECK(isotp_receive_streaming(&test_link.link, output, 7, &output_size, &is_complete) == ISOTP_RET_NOSPACE);
    CHECK(test_link.link.receive_status == ISOTP_RECEIVE_STATUS_FULL);
    CHECK(output_size == 1234u);
    CHECK(is_complete);
    CHECK(isotp_mock_can_frame_count() == 1u);

    CHECK(isotp_receive_streaming(&test_link.link, output, sizeof(output), &output_size, &is_complete) == ISOTP_RET_OK);
    CHECK(output_size == sizeof(output));
    CHECK(!is_complete);
    CHECK(memcmp(output, payload, sizeof(output)) == 0);
    CHECK(isotp_mock_can_frame_count() == 2u);
    return 0;
}

static int test_errors_and_legacy_api(void) {
    TestLink test_link;
    uint8_t payload[20];
    uint8_t output[32] = {0};
    uint32_t output_size = 0;
    uint32_t offset;
    uint8_t sequence_number = 1;
    bool is_complete = false;

    fill_payload(payload, sizeof(payload));
    init_test_link(&test_link, 8);

    CHECK(isotp_receive_streaming(&test_link.link, output, sizeof(output), &output_size, &is_complete) == ISOTP_RET_NO_DATA);
    CHECK(isotp_receive_streaming(NULL, output, sizeof(output), &output_size, &is_complete) == ISOTP_RET_ERROR);
    CHECK(isotp_receive_streaming(&test_link.link, NULL, sizeof(output), &output_size, &is_complete) == ISOTP_RET_ERROR);
    CHECK(isotp_receive_streaming(&test_link.link, output, sizeof(output), NULL, &is_complete) == ISOTP_RET_ERROR);
    CHECK(isotp_receive_streaming(&test_link.link, output, sizeof(output), &output_size, NULL) == ISOTP_RET_ERROR);

    offset = inject_first_frame(&test_link.link, payload, sizeof(payload));
    inject_consecutive_frame(&test_link.link, payload, sizeof(payload), &offset, &sequence_number);
    CHECK(isotp_receive(&test_link.link, output, sizeof(output), &output_size) == ISOTP_RET_ERROR);
    CHECK(test_link.link.receive_status == ISOTP_RECEIVE_STATUS_FULL);
    return 0;
}

static int test_receive_callback_coexists_with_streaming(void) {
    TestLink test_link;
    StreamResult result = {0};
    uint8_t single_frame[] = {0x03, 0xde, 0xad, 0x42};
    uint8_t payload[20];
    uint32_t offset;
    uint8_t sequence_number = 1;
    int callback_argument = 17;

    fill_payload(payload, sizeof(payload));
    init_test_link(&test_link, 8);
    isotp_set_rx_done_cb(&test_link.link, isotp_mock_rx_done_cb, &callback_argument);

    isotp_on_can_message(&test_link.link, single_frame, sizeof(single_frame));
    CHECK(isotp_mock_rx_callback_count() == 1u);
    CHECK(isotp_mock_rx_callback_size() == 3u);
    CHECK(memcmp(isotp_mock_rx_callback_data(), single_frame + 1, 3) == 0);
    CHECK(isotp_mock_rx_callback_link() == &test_link.link);
    CHECK(isotp_mock_rx_callback_arg() == &callback_argument);

    offset = inject_first_frame(&test_link.link, payload, sizeof(payload));
    inject_consecutive_frame(&test_link.link, payload, sizeof(payload), &offset, &sequence_number);
    CHECK(isotp_mock_rx_callback_count() == 1u);
    CHECK(drain_available_chunks(&test_link.link, &result) == 0);

    while (offset < sizeof(payload)) {
        inject_consecutive_frame(&test_link.link, payload, sizeof(payload), &offset, &sequence_number);
        CHECK(drain_available_chunks(&test_link.link, &result) == 0);
    }

    CHECK(result.output_size == sizeof(payload));
    CHECK(memcmp(result.output, payload, sizeof(payload)) == 0);
    CHECK(result.complete_count == 1u);
    CHECK(isotp_mock_rx_callback_count() == 1u);
    return 0;
}

static int test_zero_sized_receive_buffer_rejects_stream(void) {
    TestLink test_link;
    uint8_t payload[20];
    const IsoTpMockCanFrame* flow_control;

    fill_payload(payload, sizeof(payload));
    init_test_link(&test_link, 0);
    (void)inject_first_frame(&test_link.link, payload, sizeof(payload));

    CHECK(test_link.link.receive_status == ISOTP_RECEIVE_STATUS_IDLE);
    CHECK(test_link.link.receive_protocol_result == ISOTP_PROTOCOL_RESULT_BUFFER_OVFLW);
    CHECK(isotp_mock_can_frame_count() == 1u);
    flow_control = isotp_mock_can_frame(0);
    CHECK(flow_control != NULL);
    CHECK((flow_control->data[0] >> 4) == ISOTP_PCI_TYPE_FLOW_CONTROL_FRAME);
    CHECK((flow_control->data[0] & 0x0fu) == PCI_FLOW_STATUS_OVERFLOW);
    CHECK(flow_control->data[1] == 0u);
    CHECK(isotp_mock_debug_count() == 1u);
    return 0;
}

static int test_wrong_sequence_number_aborts_stream(void) {
    TestLink test_link;
    uint8_t payload[20];
    uint8_t wrong_frame[8] = {0x22};

    fill_payload(payload, sizeof(payload));
    (void)memcpy(wrong_frame + 1, payload + 6, 7);
    init_test_link(&test_link, 8);
    (void)inject_first_frame(&test_link.link, payload, sizeof(payload));
    isotp_on_can_message(&test_link.link, wrong_frame, sizeof(wrong_frame));

    CHECK(test_link.link.receive_status == ISOTP_RECEIVE_STATUS_IDLE);
    CHECK(test_link.link.receive_protocol_result == ISOTP_PROTOCOL_RESULT_WRONG_SN);
    return 0;
}

static int test_stream_timeout(void) {
    TestLink test_link;
    uint8_t payload[20];

    fill_payload(payload, sizeof(payload));
    init_test_link(&test_link, 16);
    isotp_mock_set_time_us(100);
    (void)inject_first_frame(&test_link.link, payload, sizeof(payload));
    CHECK(test_link.link.receive_status == ISOTP_RECEIVE_STATUS_INPROGRESS);

    isotp_mock_advance_time_us(ISO_TP_DEFAULT_RESPONSE_TIMEOUT_US + 1u);
    isotp_poll(&test_link.link);
    CHECK(test_link.link.receive_status == ISOTP_RECEIVE_STATUS_IDLE);
    CHECK(test_link.link.receive_protocol_result == ISOTP_PROTOCOL_RESULT_TIMEOUT_CR);
    return 0;
}

static int test_malformed_frames(void) {
    TestLink test_link;
    uint8_t payload[20];
    uint8_t short_single_frame[] = {0x03, 0xaa};
    uint8_t short_first_frame[7] = {0x10, 0x14};
    uint8_t invalid_first_frame[8] = {0x10, 0x07};
    uint8_t short_consecutive_frame[] = {0x21, 0xaa};

    fill_payload(payload, sizeof(payload));

    init_test_link(&test_link, 16);
    isotp_on_can_message(&test_link.link, short_single_frame, sizeof(short_single_frame));
    CHECK(test_link.link.receive_status == ISOTP_RECEIVE_STATUS_IDLE);
    CHECK(isotp_mock_debug_count() == 1u);

    init_test_link(&test_link, 16);
    isotp_on_can_message(&test_link.link, short_first_frame, sizeof(short_first_frame));
    CHECK(test_link.link.receive_status == ISOTP_RECEIVE_STATUS_IDLE);
    CHECK(isotp_mock_debug_count() == 1u);

    init_test_link(&test_link, 16);
    isotp_on_can_message(&test_link.link, invalid_first_frame, sizeof(invalid_first_frame));
    CHECK(test_link.link.receive_status == ISOTP_RECEIVE_STATUS_IDLE);
    CHECK(isotp_mock_debug_count() == 1u);

    init_test_link(&test_link, 16);
    (void)inject_first_frame(&test_link.link, payload, sizeof(payload));
    isotp_on_can_message(&test_link.link, short_consecutive_frame, sizeof(short_consecutive_frame));
    CHECK(test_link.link.receive_status == ISOTP_RECEIVE_STATUS_INPROGRESS);
    CHECK(isotp_mock_debug_count() == 1u);
    return 0;
}

typedef int (*TestFunction)(void);

typedef struct TestCase {
    const char* name;
    TestFunction function;
} TestCase;

int main(void) {
    static const TestCase tests[] = {
        {"single frame", test_single_frame},
        {"multi-frame fitting receive buffer", test_multiframe_fitting_receive_buffer},
        {"short stream chunk boundaries", test_short_stream_chunk_boundaries},
        {"one-byte receive buffer", test_one_byte_receive_buffer},
        {"ISO-TP 2016 long message", test_long_2016_message},
        {"destination too small", test_destination_too_small_does_not_consume_chunk},
        {"errors and legacy API", test_errors_and_legacy_api},
        {"receive callback coexistence", test_receive_callback_coexists_with_streaming},
        {"zero-sized receive buffer", test_zero_sized_receive_buffer_rejects_stream},
        {"wrong sequence number", test_wrong_sequence_number_aborts_stream},
        {"stream timeout", test_stream_timeout},
        {"malformed frames", test_malformed_frames},
    };
    size_t index;

    for (index = 0; index < sizeof(tests) / sizeof(tests[0]); ++index) {
        int return_code = tests[index].function();

        if (return_code != 0) {
            (void)fprintf(stderr, "FAILED: %s\n", tests[index].name);
            return return_code;
        }
        (void)printf("PASS: %s\n", tests[index].name);
    }

    return 0;
}
