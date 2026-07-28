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

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "isotp.h"

#define TEST_MAX_FRAMES 1024
#define TEST_BUFFER_SIZE 5000

#define TEST_TX_ID 0x123
#define TEST_RX_ID 0x456

typedef struct {
    uint32_t id;
    uint8_t  data[ISO_TP_MAX_CAN_FRAME_SIZE];
    uint8_t  len;
    uint8_t  flags;
} test_frame_t;

static test_frame_t g_frames[TEST_MAX_FRAMES];
static uint32_t     g_frame_count;
static uint32_t     g_time_us;
static uint32_t     g_failures;
static uint32_t     g_checks;
static char         g_last_debug_message[256];

#ifdef ISO_TP_USER_SEND_CAN_ARG
/* the address of this object is handed to the library and expected back unmodified */
static int g_can_arg_marker;
#endif

///////////////////////////////////////////////////////
///                    TEST SHIMS                   ///
///////////////////////////////////////////////////////

int isotp_user_send_can(const uint32_t arbitration_id, const uint8_t* data, const uint8_t size
#ifdef ISO_TP_USER_SEND_CAN_FLAGS
                        ,
                        const uint8_t flags
#endif
#ifdef ISO_TP_USER_SEND_CAN_ARG
                        ,
                        void* arg
#endif
) {
#ifdef ISO_TP_USER_SEND_CAN_ARG
    if (arg != &g_can_arg_marker) {
        printf("FATAL: user CAN argument was not forwarded\n");
        exit(1);
    }
#endif

    if (g_frame_count >= TEST_MAX_FRAMES) {
        printf("FATAL: frame buffer exhausted\n");
        exit(1);
    }

    if (size > ISO_TP_MAX_CAN_FRAME_SIZE) {
        printf("FATAL: frame of %u bytes exceeds the configured frame size\n", (unsigned int)size);
        exit(1);
    }

    g_frames[g_frame_count].id  = arbitration_id;
    g_frames[g_frame_count].len = size;
    memcpy(g_frames[g_frame_count].data, data, size);

#ifdef ISO_TP_USER_SEND_CAN_FLAGS
    g_frames[g_frame_count].flags = flags;

    /* frames of more than 8 bytes can only be transmitted as CAN FD frames */
    if (size > 8 && (flags & ISOTP_CAN_FRAME_FLAG_FD) == 0) {
        printf("FATAL: frame of %u bytes was not flagged as a CAN FD frame\n", (unsigned int)size);
        exit(1);
    }
#endif

    ++g_frame_count;

    return ISOTP_RET_OK;
}

uint32_t isotp_user_get_us(void) { return g_time_us; }

void     isotp_user_debug(const char* message, ...) {
    va_list args;
    va_start(args, message);
    (void)vsnprintf(g_last_debug_message, sizeof(g_last_debug_message), message, args);
    va_end(args);
}

///////////////////////////////////////////////////////
///                  TEST HELPERS                   ///
///////////////////////////////////////////////////////

#define CHECK(condition)                                                  \
    do {                                                                  \
        ++g_checks;                                                       \
        if (!(condition)) {                                               \
            ++g_failures;                                                 \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        }                                                                 \
    } while (0)

#define CHECK_EQ(actual, expected)                                                                                          \
    do {                                                                                                                    \
        ++g_checks;                                                                                                         \
        const long long actual_value   = (long long)(actual);                                                               \
        const long long expected_value = (long long)(expected);                                                             \
        if (actual_value != expected_value) {                                                                               \
            ++g_failures;                                                                                                   \
            printf("  FAIL %s:%d: %s == %lld, expected %lld\n", __FILE__, __LINE__, #actual, actual_value, expected_value); \
        }                                                                                                                   \
    } while (0)

/* initialises a link, attaching the user CAN argument if the library was built with it */
static void test_init_link(IsoTpLink* link, uint32_t send_id, uint8_t* send_buffer, uint32_t send_buffer_size, uint8_t* receive_buffer,
                           uint32_t receive_buffer_size) {
    isotp_init_link(link, send_id, send_buffer, send_buffer_size, receive_buffer, receive_buffer_size);

#ifdef ISO_TP_USER_SEND_CAN_ARG
    link->user_send_can_arg = &g_can_arg_marker;
#endif
}

static void reset_bus(void) {
    g_frame_count = 0;
    g_time_us     = 0;
    memset(g_frames, 0, sizeof(g_frames));
    memset(g_last_debug_message, 0, sizeof(g_last_debug_message));
}

/* the CAN_DL a frame carrying used bytes of payload is expected to be sent with */
static uint8_t expected_can_dl(uint8_t used) {
    static const uint8_t can_fd_sizes[] = {12, 16, 20, 24, 32, 48, 64};

#ifdef ISO_TP_FRAME_PADDING
    if (used < 8) { used = 8; }
#endif

    if (used <= 8) { return used; }

    for (size_t i = 0; i < sizeof(can_fd_sizes) / sizeof(can_fd_sizes[0]); ++i) {
        if (used <= can_fd_sizes[i]) { return can_fd_sizes[i]; }
    }

    return 64;
}

/* verifies that all bytes beyond the payload of a frame contain the padding value */
static void check_padding(const test_frame_t* frame, uint8_t used) {
    for (uint8_t i = used; i < frame->len; ++i) { CHECK_EQ(frame->data[i], ISO_TP_FRAME_PADDING_VALUE); }
}

static void fill_pattern(uint8_t* buffer, uint32_t size) {
    for (uint32_t i = 0; i < size; ++i) { buffer[i] = (uint8_t)(i * 7u + 1u); }
}

/* hands a frame to a link, as a CAN driver would */
static void deliver(IsoTpLink* link, const uint8_t* data, uint8_t len) { isotp_on_can_message(link, data, len); }

/* delivers a flow control frame permitting the sender to continue */
static void deliver_flow_control(IsoTpLink* link, uint8_t flow_status, uint8_t block_size, uint8_t st_min) {
    const uint8_t frame[3] = {(uint8_t)(0x30u | flow_status), block_size, st_min};
    deliver(link, frame, sizeof(frame));
}

/*
 * Runs a complete transmission between two links, routing every frame emitted
 * by one link to the other one.
 */
static void pump(IsoTpLink* sender, IsoTpLink* receiver) {
    uint32_t next_frame = 0;
    uint32_t iterations = 0;

    while (iterations++ < 100000u) {
        while (next_frame < g_frame_count) {
            const test_frame_t frame = g_frames[next_frame++];

            if (frame.id == TEST_TX_ID) {
                deliver(receiver, frame.data, frame.len);
            } else {
                deliver(sender, frame.data, frame.len);
            }
        }

        isotp_poll(sender);
        isotp_poll(receiver);
        g_time_us += 10;

        if (next_frame >= g_frame_count && ISOTP_SEND_STATUS_INPROGRESS != sender->send_status) { break; }
    }
}

///////////////////////////////////////////////////////
///                      TESTS                      ///
///////////////////////////////////////////////////////

static uint8_t g_send_buffer[TEST_BUFFER_SIZE];
static uint8_t g_receive_buffer[TEST_BUFFER_SIZE];
static uint8_t g_peer_send_buffer[TEST_BUFFER_SIZE];
static uint8_t g_peer_receive_buffer[TEST_BUFFER_SIZE];
static uint8_t g_payload[TEST_BUFFER_SIZE];
static uint8_t g_received[TEST_BUFFER_SIZE];

static void    init_links(IsoTpLink* sender, IsoTpLink* receiver, uint8_t tx_dl) {
    reset_bus();

    test_init_link(sender, TEST_TX_ID, g_send_buffer, sizeof(g_send_buffer), g_receive_buffer, sizeof(g_receive_buffer));
    test_init_link(receiver, TEST_RX_ID, g_peer_send_buffer, sizeof(g_peer_send_buffer), g_peer_receive_buffer, sizeof(g_peer_receive_buffer));

    CHECK_EQ(isotp_set_tx_dl(sender, tx_dl), ISOTP_RET_OK);
    CHECK_EQ(isotp_set_tx_dl(receiver, tx_dl), ISOTP_RET_OK);
}

static void test_default_tx_dl(void) {
    IsoTpLink link;

    printf("test_default_tx_dl\n");
    reset_bus();
    test_init_link(&link, TEST_TX_ID, g_send_buffer, sizeof(g_send_buffer), g_receive_buffer, sizeof(g_receive_buffer));

    CHECK_EQ(isotp_get_tx_dl(&link), ISO_TP_DEFAULT_TX_DL);
    CHECK_EQ(isotp_get_tx_dl(NULL), 0);

    /* links which were not initialised through isotp_init_link() fall back to Classical CAN */
    memset(&link, 0, sizeof(link));
    CHECK_EQ(isotp_get_tx_dl(&link), 8);
}

static void test_set_tx_dl(void) {
    IsoTpLink link;

    printf("test_set_tx_dl\n");
    reset_bus();
    test_init_link(&link, TEST_TX_ID, g_send_buffer, sizeof(g_send_buffer), g_receive_buffer, sizeof(g_receive_buffer));

    CHECK_EQ(isotp_set_tx_dl(NULL, 8), ISOTP_RET_ERROR);
    CHECK_EQ(isotp_set_tx_dl(&link, 8), ISOTP_RET_OK);
    CHECK_EQ(isotp_get_tx_dl(&link), 8);

    /* lengths which cannot be transmitted by a CAN(-FD) controller */
    CHECK_EQ(isotp_set_tx_dl(&link, 0), ISOTP_RET_ERROR);
    CHECK_EQ(isotp_set_tx_dl(&link, 7), ISOTP_RET_ERROR);
    CHECK_EQ(isotp_set_tx_dl(&link, 9), ISOTP_RET_ERROR);
    CHECK_EQ(isotp_set_tx_dl(&link, 63), ISOTP_RET_ERROR);
    CHECK_EQ(isotp_set_tx_dl(&link, 255), ISOTP_RET_ERROR);

#if ISO_TP_MAX_CAN_FRAME_SIZE >= 64
    CHECK_EQ(isotp_set_tx_dl(&link, 12), ISOTP_RET_OK);
    CHECK_EQ(isotp_set_tx_dl(&link, 64), ISOTP_RET_OK);
    CHECK_EQ(isotp_get_tx_dl(&link), 64);
#else
    /* frame sizes exceeding the compile time maximum are rejected */
    CHECK_EQ(isotp_set_tx_dl(&link, 12), ISOTP_RET_ERROR);
    CHECK_EQ(isotp_set_tx_dl(&link, 64), ISOTP_RET_ERROR);
#endif

    /* TX_DL must not change while a message is being transmitted */
    CHECK_EQ(isotp_set_tx_dl(&link, 8), ISOTP_RET_OK);
    fill_pattern(g_payload, 20);
    CHECK_EQ(isotp_send(&link, g_payload, 20), ISOTP_RET_OK);
    CHECK_EQ(isotp_set_tx_dl(&link, 8), ISOTP_RET_INPROGRESS);
}

static void test_classic_single_frame(void) {
    IsoTpLink link;

    printf("test_classic_single_frame\n");
    reset_bus();
    test_init_link(&link, TEST_TX_ID, g_send_buffer, sizeof(g_send_buffer), g_receive_buffer, sizeof(g_receive_buffer));
    CHECK_EQ(isotp_set_tx_dl(&link, 8), ISOTP_RET_OK);

    fill_pattern(g_payload, 7);
    CHECK_EQ(isotp_send(&link, g_payload, 7), ISOTP_RET_OK);

    CHECK_EQ(g_frame_count, 1);
    CHECK_EQ(g_frames[0].id, TEST_TX_ID);
    CHECK_EQ(g_frames[0].len, 8);
    CHECK_EQ(g_frames[0].data[0], 0x07);
    CHECK_EQ(memcmp(&g_frames[0].data[1], g_payload, 7), 0);

    /* a shorter payload is only padded if padding is enabled */
    reset_bus();
    fill_pattern(g_payload, 3);
    CHECK_EQ(isotp_send(&link, g_payload, 3), ISOTP_RET_OK);
    CHECK_EQ(g_frame_count, 1);
    CHECK_EQ(g_frames[0].len, expected_can_dl(4));
    CHECK_EQ(g_frames[0].data[0], 0x03);
    CHECK_EQ(memcmp(&g_frames[0].data[1], g_payload, 3), 0);
    check_padding(&g_frames[0], 4);
}

static void test_classic_multi_frame(void) {
    IsoTpLink link;

    printf("test_classic_multi_frame\n");
    reset_bus();
    test_init_link(&link, TEST_TX_ID, g_send_buffer, sizeof(g_send_buffer), g_receive_buffer, sizeof(g_receive_buffer));
    CHECK_EQ(isotp_set_tx_dl(&link, 8), ISOTP_RET_OK);

    fill_pattern(g_payload, 20);
    CHECK_EQ(isotp_send(&link, g_payload, 20), ISOTP_RET_OK);

    /* first frame: 0x1, FF_DL = 20, followed by 6 data bytes */
    CHECK_EQ(g_frame_count, 1);
    CHECK_EQ(g_frames[0].len, 8);
    CHECK_EQ(g_frames[0].data[0], 0x10);
    CHECK_EQ(g_frames[0].data[1], 20);
    CHECK_EQ(memcmp(&g_frames[0].data[2], g_payload, 6), 0);

    /* consecutive frames are only sent once flow control was received */
    isotp_poll(&link);
    CHECK_EQ(g_frame_count, 1);

    deliver_flow_control(&link, PCI_FLOW_STATUS_CONTINUE, 0, 0);
    isotp_poll(&link);
    CHECK_EQ(g_frame_count, 2);
    CHECK_EQ(g_frames[1].len, 8);
    CHECK_EQ(g_frames[1].data[0], 0x21);
    CHECK_EQ(memcmp(&g_frames[1].data[1], &g_payload[6], 7), 0);

    isotp_poll(&link);
    CHECK_EQ(g_frame_count, 3);
    CHECK_EQ(g_frames[2].len, 8);
    CHECK_EQ(g_frames[2].data[0], 0x22);
    CHECK_EQ(memcmp(&g_frames[2].data[1], &g_payload[13], 7), 0);
    CHECK_EQ(link.send_status, ISOTP_SEND_STATUS_IDLE);
}

static void test_classic_receive(void) {
    IsoTpLink link;
    uint32_t  out_size = 0;

    printf("test_classic_receive\n");
    reset_bus();
    test_init_link(&link, TEST_RX_ID, g_send_buffer, sizeof(g_send_buffer), g_receive_buffer, sizeof(g_receive_buffer));

    /* single frame */
    const uint8_t single_frame[8] = {0x04, 0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x00, 0x00};
    deliver(&link, single_frame, sizeof(single_frame));
    CHECK_EQ(isotp_receive(&link, g_received, sizeof(g_received), &out_size), ISOTP_RET_OK);
    CHECK_EQ(out_size, 4);
    CHECK_EQ(memcmp(g_received, &single_frame[1], 4), 0);

    /* multi frame: 10 bytes spread across a first and a consecutive frame */
    const uint8_t first_frame[8]      = {0x10, 0x0A, 1, 2, 3, 4, 5, 6};
    const uint8_t consecutive_frame[] = {0x21, 7, 8, 9, 10};
    const uint8_t expected[10]        = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};

    reset_bus();
    deliver(&link, first_frame, sizeof(first_frame));

    /* the receiver answers a first frame with flow control */
    CHECK_EQ(g_frame_count, 1);
    CHECK_EQ(g_frames[0].id, TEST_RX_ID);
    CHECK_EQ(g_frames[0].len, expected_can_dl(3));
    CHECK_EQ(g_frames[0].data[0], 0x30);
    CHECK_EQ(g_frames[0].data[1], ISO_TP_DEFAULT_BLOCK_SIZE);
    check_padding(&g_frames[0], 3);

    deliver(&link, consecutive_frame, sizeof(consecutive_frame));
    CHECK_EQ(isotp_receive(&link, g_received, sizeof(g_received), &out_size), ISOTP_RET_OK);
    CHECK_EQ(out_size, 10);
    CHECK_EQ(memcmp(g_received, expected, sizeof(expected)), 0);
}

static void test_receive_rejects_invalid_frames(void) {
    IsoTpLink link;
    uint32_t  out_size = 0;

    printf("test_receive_rejects_invalid_frames\n");
    reset_bus();
    test_init_link(&link, TEST_RX_ID, g_send_buffer, sizeof(g_send_buffer), g_receive_buffer, 16);

    /* single frame announcing more data than the frame contains */
    const uint8_t truncated_single_frame[3] = {0x07, 0x01, 0x02};
    deliver(&link, truncated_single_frame, sizeof(truncated_single_frame));
    CHECK_EQ(isotp_receive(&link, g_received, sizeof(g_received), &out_size), ISOTP_RET_NO_DATA);

    /* SF_DL of zero is only valid for CAN FD frames using the escape sequence */
    const uint8_t empty_single_frame[8] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    deliver(&link, empty_single_frame, sizeof(empty_single_frame));
    CHECK_EQ(isotp_receive(&link, g_received, sizeof(g_received), &out_size), ISOTP_RET_NO_DATA);

    /* first frames must fill the frame of the sender */
    const uint8_t short_first_frame[7] = {0x10, 0x0A, 1, 2, 3, 4, 5};
    deliver(&link, short_first_frame, sizeof(short_first_frame));
    CHECK_EQ(g_frame_count, 0);
    CHECK_EQ(link.receive_status, ISOTP_RECEIVE_STATUS_IDLE);

    /* payloads which fit into a single frame must not be segmented */
    const uint8_t pointless_first_frame[8] = {0x10, 0x05, 1, 2, 3, 4, 5, 6};
    deliver(&link, pointless_first_frame, sizeof(pointless_first_frame));
    CHECK_EQ(g_frame_count, 0);
    CHECK_EQ(link.receive_status, ISOTP_RECEIVE_STATUS_IDLE);

    const uint8_t oversized_first_frame[8] = {0x10, 0x64, 1, 2, 3, 4, 5, 6};
    deliver(&link, oversized_first_frame, sizeof(oversized_first_frame));
#ifndef ISO_TP_ENABLE_STREAMING
    /* messages exceeding the receive buffer are refused with a flow control overflow */
    CHECK_EQ(g_frame_count, 1);
    CHECK_EQ(g_frames[0].data[0], 0x32);
    CHECK_EQ(link.receive_protocol_result, ISOTP_PROTOCOL_RESULT_BUFFER_OVFLW);
#else
    /* unless they are received in chunks, in which case reception continues */
    CHECK_EQ(g_frame_count, 1);
    CHECK_EQ(g_frames[0].data[0], 0x30);
    CHECK_EQ(link.receive_status, ISOTP_RECEIVE_STATUS_INPROGRESS);
    CHECK_EQ(link.receive_protocol_result, ISOTP_PROTOCOL_RESULT_OK);
#endif

    /* consecutive frames carrying an unexpected sequence number abort the message */
    reset_bus();
    const uint8_t first_frame[8]    = {0x10, 0x0A, 1, 2, 3, 4, 5, 6};
    const uint8_t wrong_sn_frame[5] = {0x22, 7, 8, 9, 10};
    deliver(&link, first_frame, sizeof(first_frame));
    deliver(&link, wrong_sn_frame, sizeof(wrong_sn_frame));
    CHECK_EQ(link.receive_status, ISOTP_RECEIVE_STATUS_IDLE);
    CHECK_EQ(link.receive_protocol_result, ISOTP_PROTOCOL_RESULT_WRONG_SN);

    /* consecutive frames must be full, unless they carry the end of the message */
    reset_bus();
    const uint8_t short_consecutive_frame[5] = {0x21, 7, 8, 9, 10};
    deliver(&link, first_frame, sizeof(first_frame));
    CHECK_EQ(link.receive_status, ISOTP_RECEIVE_STATUS_INPROGRESS);
    deliver(&link, short_consecutive_frame, sizeof(short_consecutive_frame));
    CHECK_EQ(link.receive_status, ISOTP_RECEIVE_STATUS_FULL);
    CHECK_EQ(isotp_receive(&link, g_received, sizeof(g_received), &out_size), ISOTP_RET_OK);
    CHECK_EQ(out_size, 10);

    /* frames which are too short to carry protocol control information are ignored */
    reset_bus();
    const uint8_t runt_frame[1] = {0x02};
    deliver(&link, runt_frame, sizeof(runt_frame));
    CHECK_EQ(g_frame_count, 0);
    CHECK_EQ(isotp_receive(&link, g_received, sizeof(g_received), &out_size), ISOTP_RET_NO_DATA);

    /* single frames which don't fit into the receive buffer are reported as overflow */
    reset_bus();
    test_init_link(&link, TEST_RX_ID, g_send_buffer, sizeof(g_send_buffer), g_receive_buffer, 2);
    const uint8_t single_frame[8] = {0x04, 1, 2, 3, 4, 0, 0, 0};
    deliver(&link, single_frame, sizeof(single_frame));
    CHECK_EQ(link.receive_protocol_result, ISOTP_PROTOCOL_RESULT_BUFFER_OVFLW);
    CHECK_EQ(isotp_receive(&link, g_received, sizeof(g_received), &out_size), ISOTP_RET_NO_DATA);
}

/* transmits a message from one link to another and verifies it arrives unchanged */
static void check_transfer(uint8_t tx_dl, uint32_t size) {
    IsoTpLink sender;
    IsoTpLink receiver;
    uint32_t  out_size = 0;

    init_links(&sender, &receiver, tx_dl);

    fill_pattern(g_payload, size);
    CHECK_EQ(isotp_send(&sender, g_payload, size), ISOTP_RET_OK);

    pump(&sender, &receiver);

    CHECK_EQ(sender.send_status, ISOTP_SEND_STATUS_IDLE);
    CHECK_EQ(isotp_receive(&receiver, g_received, sizeof(g_received), &out_size), ISOTP_RET_OK);
    CHECK_EQ(out_size, size);

    if (out_size == size && memcmp(g_received, g_payload, size) != 0) {
        ++g_failures;
        printf("  FAIL payload mismatch for %u bytes at TX_DL %u\n", (unsigned int)size, (unsigned int)tx_dl);
    }
    ++g_checks;
}

static void test_transfers(uint8_t tx_dl) {
    static const uint32_t sizes[] = {1, 2, 6, 7, 8, 9, 10, 62, 63, 64, 100, 511, 4094, 4095, 4096, 4097, TEST_BUFFER_SIZE};

    printf("test_transfers (TX_DL %u)\n", (unsigned int)tx_dl);

    for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); ++i) { check_transfer(tx_dl, sizes[i]); }
}

static void test_long_first_frame(uint8_t tx_dl) {
    IsoTpLink      link;
    const uint32_t size = 5000;

    printf("test_long_first_frame (TX_DL %u)\n", (unsigned int)tx_dl);
    reset_bus();
    test_init_link(&link, TEST_TX_ID, g_send_buffer, sizeof(g_send_buffer), g_receive_buffer, sizeof(g_receive_buffer));
    CHECK_EQ(isotp_set_tx_dl(&link, tx_dl), ISOTP_RET_OK);

    fill_pattern(g_payload, size);
    CHECK_EQ(isotp_send(&link, g_payload, size), ISOTP_RET_OK);

    /* messages of more than 4095 bytes use the escaped first frame format */
    CHECK_EQ(g_frame_count, 1);
    CHECK_EQ(g_frames[0].len, tx_dl);
    CHECK_EQ(g_frames[0].data[0], 0x10);
    CHECK_EQ(g_frames[0].data[1], 0x00);
    CHECK_EQ(g_frames[0].data[2], (size >> 24) & 0xFF);
    CHECK_EQ(g_frames[0].data[3], (size >> 16) & 0xFF);
    CHECK_EQ(g_frames[0].data[4], (size >> 8) & 0xFF);
    CHECK_EQ(g_frames[0].data[5], size & 0xFF);
    CHECK_EQ(memcmp(&g_frames[0].data[6], g_payload, (size_t)(tx_dl - 6)), 0);
}

/* a build which is not able to handle a frame of a given length must ignore it,
 * as a Classical CAN build does with the CAN FD frames of an FD capable peer
 */
static void test_oversized_frames_ignored(void) {
    IsoTpLink link;
    uint8_t   frame[ISO_TP_MAX_CAN_FRAME_SIZE + 4];
    uint32_t  out_size = 0;

    printf("test_oversized_frames_ignored\n");
    reset_bus();
    test_init_link(&link, TEST_RX_ID, g_send_buffer, sizeof(g_send_buffer), g_receive_buffer, sizeof(g_receive_buffer));

    memset(frame, 0x55, sizeof(frame));

    /* a single frame using the SF_DL escape sequence */
    frame[0] = 0x00;
    frame[1] = ISO_TP_MAX_CAN_FRAME_SIZE;
    deliver(&link, frame, (uint8_t)sizeof(frame));
    CHECK_EQ(g_frame_count, 0);
    CHECK_EQ(link.receive_status, ISOTP_RECEIVE_STATUS_IDLE);
    CHECK_EQ(isotp_receive(&link, g_received, sizeof(g_received), &out_size), ISOTP_RET_NO_DATA);

    /* a first frame, which is not answered with flow control either */
    frame[0] = 0x10;
    frame[1] = 100;
    deliver(&link, frame, (uint8_t)sizeof(frame));
    CHECK_EQ(g_frame_count, 0);
    CHECK_EQ(link.receive_status, ISOTP_RECEIVE_STATUS_IDLE);
}

#ifdef ISO_TP_ENABLE_STREAMING

/*
 * Transfers a message which does not fit into the receiver's buffer and collects
 * the chunks the receiver hands out, which is where a frame carrying more data
 * than the remaining buffer space has to be carried over to the next chunk.
 */
static void check_streaming_transfer(uint8_t tx_dl, uint32_t size, uint32_t receive_buffer_size) {
    IsoTpLink sender;
    IsoTpLink receiver;
    uint8_t   chunk[TEST_BUFFER_SIZE];
    uint32_t  received    = 0;
    uint32_t  chunk_count = 0;
    uint32_t  next_frame  = 0;
    uint32_t  iterations  = 0;
    bool      complete    = false;

    reset_bus();
    test_init_link(&sender, TEST_TX_ID, g_send_buffer, sizeof(g_send_buffer), g_receive_buffer, sizeof(g_receive_buffer));
    test_init_link(&receiver, TEST_RX_ID, g_peer_send_buffer, sizeof(g_peer_send_buffer), g_peer_receive_buffer, receive_buffer_size);
    CHECK_EQ(isotp_set_tx_dl(&sender, tx_dl), ISOTP_RET_OK);
    CHECK_EQ(isotp_set_tx_dl(&receiver, tx_dl), ISOTP_RET_OK);

    fill_pattern(g_payload, size);
    CHECK_EQ(isotp_send(&sender, g_payload, size), ISOTP_RET_OK);

    while (!complete && iterations++ < 200000u) {
        while (next_frame < g_frame_count) {
            const test_frame_t frame = g_frames[next_frame++];

            if (frame.id == TEST_TX_ID) {
                deliver(&receiver, frame.data, frame.len);
            } else {
                deliver(&sender, frame.data, frame.len);
            }
        }

        /* chunked transfers exchange far more frames than the log is able to hold */
        if (next_frame == g_frame_count) {
            g_frame_count = 0;
            next_frame    = 0;
        }

        if (ISOTP_RECEIVE_STATUS_FULL == receiver.receive_status) {
            uint32_t chunk_size = 0;

            /* a partially received message must not be handed out as a complete one */
            if (0 == chunk_count) { CHECK_EQ(isotp_receive(&receiver, chunk, sizeof(chunk), &chunk_size), ISOTP_RET_ERROR); }

            CHECK_EQ(isotp_receive_streaming(&receiver, chunk, sizeof(chunk), &chunk_size, &complete), ISOTP_RET_OK);
            CHECK(chunk_size > 0);
            CHECK(chunk_size <= receive_buffer_size);
            CHECK(received + chunk_size <= size);

            if (received + chunk_size <= size) { memcpy(&g_received[received], chunk, chunk_size); }
            received += chunk_size;
            ++chunk_count;
        }

        isotp_poll(&sender);
        isotp_poll(&receiver);
        g_time_us += 10;
    }

    CHECK(complete);
    CHECK(chunk_count > 1);
    CHECK_EQ(received, size);
    CHECK_EQ(sender.send_status, ISOTP_SEND_STATUS_IDLE);

    if (received == size && memcmp(g_received, g_payload, size) != 0) {
        ++g_failures;
        printf("  FAIL payload mismatch for %u bytes at TX_DL %u with a %u byte receive buffer\n", (unsigned int)size, (unsigned int)tx_dl,
               (unsigned int)receive_buffer_size);
    }
    ++g_checks;
}

static void test_streaming(uint8_t tx_dl) {
    printf("test_streaming (TX_DL %u)\n", (unsigned int)tx_dl);

    /* receive buffers smaller than the data of a single frame exercise the carry over */
    check_streaming_transfer(tx_dl, 500, 1);
    check_streaming_transfer(tx_dl, 500, 4);
    check_streaming_transfer(tx_dl, 500, 20);
    check_streaming_transfer(tx_dl, 500, 64);
    check_streaming_transfer(tx_dl, 4096, 100);
    check_streaming_transfer(tx_dl, 5000, 333);
}

static void test_streaming_of_small_messages(uint8_t tx_dl) {
    IsoTpLink sender;
    IsoTpLink receiver;
    uint32_t  chunk_size = 0;
    bool      complete   = false;

    printf("test_streaming_of_small_messages (TX_DL %u)\n", (unsigned int)tx_dl);

    /* messages fitting into the receive buffer are handed out in one piece */
    init_links(&sender, &receiver, tx_dl);
    fill_pattern(g_payload, 100);
    CHECK_EQ(isotp_send(&sender, g_payload, 100), ISOTP_RET_OK);
    pump(&sender, &receiver);

    CHECK_EQ(receiver.receive_status, ISOTP_RECEIVE_STATUS_FULL);
    CHECK_EQ(isotp_receive_streaming(&receiver, g_received, sizeof(g_received), &chunk_size, &complete), ISOTP_RET_OK);
    CHECK_EQ(chunk_size, 100);
    CHECK(complete);
    CHECK_EQ(memcmp(g_received, g_payload, 100), 0);

    /* the same goes for single frames */
    init_links(&sender, &receiver, tx_dl);
    fill_pattern(g_payload, 6);
    complete = false;
    CHECK_EQ(isotp_send(&sender, g_payload, 6), ISOTP_RET_OK);
    pump(&sender, &receiver);

    CHECK_EQ(isotp_receive_streaming(&receiver, g_received, sizeof(g_received), &chunk_size, &complete), ISOTP_RET_OK);
    CHECK_EQ(chunk_size, 6);
    CHECK(complete);
    CHECK_EQ(memcmp(g_received, g_payload, 6), 0);

    /* invalid arguments are rejected */
    CHECK_EQ(isotp_receive_streaming(NULL, g_received, sizeof(g_received), &chunk_size, &complete), ISOTP_RET_ERROR);
    CHECK_EQ(isotp_receive_streaming(&receiver, NULL, sizeof(g_received), &chunk_size, &complete), ISOTP_RET_ERROR);
    CHECK_EQ(isotp_receive_streaming(&receiver, g_received, sizeof(g_received), NULL, &complete), ISOTP_RET_ERROR);
    CHECK_EQ(isotp_receive_streaming(&receiver, g_received, sizeof(g_received), &chunk_size, NULL), ISOTP_RET_ERROR);

    /* and so is a request without any data pending */
    CHECK_EQ(isotp_receive_streaming(&receiver, g_received, sizeof(g_received), &chunk_size, &complete), ISOTP_RET_NO_DATA);
}

#endif // ISO_TP_ENABLE_STREAMING

#ifdef ISO_TP_USER_SEND_CAN_FLAGS

/* the frame format flags a link is expected to hand to the user shim */
static uint8_t expected_frame_flags(uint8_t tx_dl) {
    uint8_t flags = ISOTP_CAN_FRAME_FLAG_NONE;

    if (tx_dl > 8) {
        flags |= ISOTP_CAN_FRAME_FLAG_FD;

    #ifdef ISO_TP_CAN_FD_USE_BRS
        flags |= ISOTP_CAN_FRAME_FLAG_BRS;
    #endif
    }

    return flags;
}

static void test_frame_flags(uint8_t tx_dl) {
    IsoTpLink sender;
    IsoTpLink receiver;

    printf("test_frame_flags (TX_DL %u)\n", (unsigned int)tx_dl);

    /* a single frame short enough to fit into a Classical CAN frame */
    init_links(&sender, &receiver, tx_dl);
    fill_pattern(g_payload, 3);
    CHECK_EQ(isotp_send(&sender, g_payload, 3), ISOTP_RET_OK);
    CHECK_EQ(g_frame_count, 1);
    CHECK_EQ(g_frames[0].flags, expected_frame_flags(tx_dl));

    /* first frame, flow control frames of the receiver and consecutive frames */
    init_links(&sender, &receiver, tx_dl);
    fill_pattern(g_payload, 500);
    CHECK_EQ(isotp_send(&sender, g_payload, 500), ISOTP_RET_OK);
    pump(&sender, &receiver);

    CHECK(g_frame_count > 3);
    for (uint32_t i = 0; i < g_frame_count; ++i) { CHECK_EQ(g_frames[i].flags, expected_frame_flags(tx_dl)); }
}

#endif // ISO_TP_USER_SEND_CAN_FLAGS

#if defined(ISO_TP_TRANSMIT_COMPLETE_CALLBACK) || defined(ISO_TP_RECEIVE_COMPLETE_CALLBACK)

    #ifdef ISO_TP_TRANSMIT_COMPLETE_CALLBACK
static uint32_t g_tx_done_count;
static uint32_t g_tx_done_size;

static void     on_tx_done(void* link, uint32_t size, void* user_arg) {
    (void)link;
    ++g_tx_done_count;
    g_tx_done_size = size;
    CHECK(user_arg == &g_tx_done_count);
}
    #endif

    #ifdef ISO_TP_RECEIVE_COMPLETE_CALLBACK
static uint32_t g_rx_done_count;
static uint32_t g_rx_done_size;

static void     on_rx_done(void* link, const uint8_t* data, uint32_t size, void* user_arg) {
    (void)link;
    ++g_rx_done_count;
    g_rx_done_size = size;
    CHECK(user_arg == &g_rx_done_count);
    CHECK_EQ(memcmp(data, g_payload, size), 0);
}
    #endif

static void test_callbacks(uint8_t tx_dl, uint32_t size) {
    IsoTpLink sender;
    IsoTpLink receiver;

    printf("test_callbacks (TX_DL %u, %u bytes)\n", (unsigned int)tx_dl, (unsigned int)size);
    init_links(&sender, &receiver, tx_dl);

    #ifdef ISO_TP_TRANSMIT_COMPLETE_CALLBACK
    g_tx_done_count = 0;
    g_tx_done_size  = 0;
    isotp_set_tx_done_cb(&sender, on_tx_done, &g_tx_done_count);
    #endif

    #ifdef ISO_TP_RECEIVE_COMPLETE_CALLBACK
    g_rx_done_count = 0;
    g_rx_done_size  = 0;
    isotp_set_rx_done_cb(&receiver, on_rx_done, &g_rx_done_count);
    #endif

    fill_pattern(g_payload, size);
    CHECK_EQ(isotp_send(&sender, g_payload, size), ISOTP_RET_OK);
    pump(&sender, &receiver);

    #ifdef ISO_TP_TRANSMIT_COMPLETE_CALLBACK
    CHECK_EQ(g_tx_done_count, 1);
    CHECK_EQ(g_tx_done_size, size);
    #endif

    #ifdef ISO_TP_RECEIVE_COMPLETE_CALLBACK
    CHECK_EQ(g_rx_done_count, 1);
    CHECK_EQ(g_rx_done_size, size);
    #endif
}

#endif // callbacks

#if ISO_TP_MAX_CAN_FRAME_SIZE >= 64

static void test_can_fd_single_frame(void) {
    IsoTpLink link;

    printf("test_can_fd_single_frame\n");
    reset_bus();
    test_init_link(&link, TEST_TX_ID, g_send_buffer, sizeof(g_send_buffer), g_receive_buffer, sizeof(g_receive_buffer));
    CHECK_EQ(isotp_set_tx_dl(&link, 64), ISOTP_RET_OK);

    /* payloads of up to 7 bytes keep using the Classical CAN single frame format */
    fill_pattern(g_payload, 7);
    CHECK_EQ(isotp_send(&link, g_payload, 7), ISOTP_RET_OK);
    CHECK_EQ(g_frame_count, 1);
    CHECK_EQ(g_frames[0].len, 8);
    CHECK_EQ(g_frames[0].data[0], 0x07);
    CHECK_EQ(memcmp(&g_frames[0].data[1], g_payload, 7), 0);

    /* larger payloads use the SF_DL escape sequence and are padded to a valid CAN FD length */
    reset_bus();
    fill_pattern(g_payload, 20);
    CHECK_EQ(isotp_send(&link, g_payload, 20), ISOTP_RET_OK);
    CHECK_EQ(g_frame_count, 1);
    CHECK_EQ(g_frames[0].len, 24);
    CHECK_EQ(g_frames[0].data[0], 0x00);
    CHECK_EQ(g_frames[0].data[1], 20);
    CHECK_EQ(memcmp(&g_frames[0].data[2], g_payload, 20), 0);
    check_padding(&g_frames[0], 22);

    /* the largest payload which still fits into a single frame */
    reset_bus();
    fill_pattern(g_payload, 62);
    CHECK_EQ(isotp_send(&link, g_payload, 62), ISOTP_RET_OK);
    CHECK_EQ(g_frame_count, 1);
    CHECK_EQ(g_frames[0].len, 64);
    CHECK_EQ(g_frames[0].data[0], 0x00);
    CHECK_EQ(g_frames[0].data[1], 62);
    CHECK_EQ(memcmp(&g_frames[0].data[2], g_payload, 62), 0);

    /* smaller frame lengths reduce the amount of data a single frame can carry */
    reset_bus();
    CHECK_EQ(isotp_set_tx_dl(&link, 12), ISOTP_RET_OK);
    fill_pattern(g_payload, 10);
    CHECK_EQ(isotp_send(&link, g_payload, 10), ISOTP_RET_OK);
    CHECK_EQ(g_frame_count, 1);
    CHECK_EQ(g_frames[0].len, 12);
    CHECK_EQ(g_frames[0].data[1], 10);
    CHECK_EQ(memcmp(&g_frames[0].data[2], g_payload, 10), 0);
}

static void test_can_fd_multi_frame(void) {
    IsoTpLink link;

    printf("test_can_fd_multi_frame\n");
    reset_bus();
    test_init_link(&link, TEST_TX_ID, g_send_buffer, sizeof(g_send_buffer), g_receive_buffer, sizeof(g_receive_buffer));
    CHECK_EQ(isotp_set_tx_dl(&link, 64), ISOTP_RET_OK);

    /* 63 bytes is the smallest payload requiring segmentation at a TX_DL of 64 */
    fill_pattern(g_payload, 63);
    CHECK_EQ(isotp_send(&link, g_payload, 63), ISOTP_RET_OK);

    /* first frames always use the full frame length of the sender */
    CHECK_EQ(g_frame_count, 1);
    CHECK_EQ(g_frames[0].len, 64);
    CHECK_EQ(g_frames[0].data[0], 0x10);
    CHECK_EQ(g_frames[0].data[1], 63);
    CHECK_EQ(memcmp(&g_frames[0].data[2], g_payload, 62), 0);

    deliver_flow_control(&link, PCI_FLOW_STATUS_CONTINUE, 0, 0);
    isotp_poll(&link);

    /* the trailing consecutive frame only carries the remaining byte */
    CHECK_EQ(g_frame_count, 2);
    CHECK_EQ(g_frames[1].len, expected_can_dl(2));
    CHECK_EQ(g_frames[1].data[0], 0x21);
    CHECK_EQ(g_frames[1].data[1], g_payload[62]);
    check_padding(&g_frames[1], 2);
    CHECK_EQ(link.send_status, ISOTP_SEND_STATUS_IDLE);

    /* consecutive frames carry up to TX_DL - 1 bytes */
    reset_bus();
    fill_pattern(g_payload, 200);
    CHECK_EQ(isotp_send(&link, g_payload, 200), ISOTP_RET_OK);
    deliver_flow_control(&link, PCI_FLOW_STATUS_CONTINUE, 0, 0);
    isotp_poll(&link);
    CHECK_EQ(g_frame_count, 2);
    CHECK_EQ(g_frames[1].len, 64);
    CHECK_EQ(g_frames[1].data[0], 0x21);
    CHECK_EQ(memcmp(&g_frames[1].data[1], &g_payload[62], 63), 0);
}

static void test_can_fd_receive(void) {
    IsoTpLink link;
    uint8_t   frame[64];
    uint32_t  out_size = 0;

    printf("test_can_fd_receive\n");
    reset_bus();
    test_init_link(&link, TEST_RX_ID, g_send_buffer, sizeof(g_send_buffer), g_receive_buffer, sizeof(g_receive_buffer));

    /* single frame using the SF_DL escape sequence */
    memset(frame, ISO_TP_FRAME_PADDING_VALUE, sizeof(frame));
    frame[0] = 0x00;
    frame[1] = 20;
    fill_pattern(g_payload, 20);
    memcpy(&frame[2], g_payload, 20);
    deliver(&link, frame, 24);
    CHECK_EQ(isotp_receive(&link, g_received, sizeof(g_received), &out_size), ISOTP_RET_OK);
    CHECK_EQ(out_size, 20);
    CHECK_EQ(memcmp(g_received, g_payload, 20), 0);

    /* the escape sequence must not announce more data than the frame contains */
    reset_bus();
    frame[1] = 40;
    deliver(&link, frame, 24);
    CHECK_EQ(isotp_receive(&link, g_received, sizeof(g_received), &out_size), ISOTP_RET_NO_DATA);

    /* first frames must use a valid CAN FD frame length */
    reset_bus();
    frame[0] = 0x10;
    frame[1] = 100;
    deliver(&link, frame, 22);
    CHECK_EQ(g_frame_count, 0);
    CHECK_EQ(link.receive_status, ISOTP_RECEIVE_STATUS_IDLE);

    /* messages which would have fit into a single frame are rejected */
    reset_bus();
    frame[1] = 60;
    deliver(&link, frame, 64);
    CHECK_EQ(g_frame_count, 0);
    CHECK_EQ(link.receive_status, ISOTP_RECEIVE_STATUS_IDLE);

    /* a segmented message: the consecutive frames use the frame length of the first frame */
    reset_bus();
    fill_pattern(g_payload, 100);
    frame[0] = 0x10;
    frame[1] = 100;
    memcpy(&frame[2], g_payload, 62);
    deliver(&link, frame, 64);
    CHECK_EQ(link.receive_status, ISOTP_RECEIVE_STATUS_INPROGRESS);
    CHECK_EQ(g_frame_count, 1);
    CHECK_EQ(g_frames[0].data[0], 0x30);

    frame[0] = 0x21;
    memcpy(&frame[1], &g_payload[62], 38);
    deliver(&link, frame, 48);
    CHECK_EQ(isotp_receive(&link, g_received, sizeof(g_received), &out_size), ISOTP_RET_OK);
    CHECK_EQ(out_size, 100);
    CHECK_EQ(memcmp(g_received, g_payload, 100), 0);

    /* consecutive frames must be full while data is still outstanding */
    reset_bus();
    frame[0] = 0x10;
    frame[1] = 100;
    memcpy(&frame[2], g_payload, 62);
    deliver(&link, frame, 64);
    frame[0] = 0x21;
    memcpy(&frame[1], &g_payload[62], 11);
    deliver(&link, frame, 12);
    CHECK_EQ(link.receive_status, ISOTP_RECEIVE_STATUS_INPROGRESS);
    CHECK_EQ(isotp_receive(&link, g_received, sizeof(g_received), &out_size), ISOTP_RET_NO_DATA);

    /* frames larger than the configured maximum are ignored */
    reset_bus();
    CHECK_EQ(link.receive_status, ISOTP_RECEIVE_STATUS_INPROGRESS);
}

#endif // ISO_TP_MAX_CAN_FRAME_SIZE >= 64

int main(void) {
    printf("isotp-c test suite (ISO_TP_MAX_CAN_FRAME_SIZE=%d, TX_DL=%d, padding %s)\n", ISO_TP_MAX_CAN_FRAME_SIZE, ISO_TP_DEFAULT_TX_DL,
#ifdef ISO_TP_FRAME_PADDING
           "enabled"
#else
           "disabled"
#endif
    );

    test_default_tx_dl();
    test_set_tx_dl();
    test_classic_single_frame();
    test_classic_multi_frame();
    test_classic_receive();
    test_receive_rejects_invalid_frames();
    test_long_first_frame(8);
    test_transfers(8);
    test_oversized_frames_ignored();

#ifdef ISO_TP_USER_SEND_CAN_FLAGS
    test_frame_flags(8);
#endif

#ifdef ISO_TP_ENABLE_STREAMING
    test_streaming(8);
    test_streaming_of_small_messages(8);
#endif

#if ISO_TP_MAX_CAN_FRAME_SIZE >= 64
    test_can_fd_single_frame();
    test_can_fd_multi_frame();
    test_can_fd_receive();
    test_long_first_frame(12);
    test_long_first_frame(64);
    test_transfers(12);
    test_transfers(64);

    #ifdef ISO_TP_USER_SEND_CAN_FLAGS
    test_frame_flags(12);
    test_frame_flags(64);
    #endif

    #ifdef ISO_TP_ENABLE_STREAMING
    test_streaming(12);
    test_streaming(64);
    test_streaming_of_small_messages(64);
    #endif
#endif

#if defined(ISO_TP_TRANSMIT_COMPLETE_CALLBACK) || defined(ISO_TP_RECEIVE_COMPLETE_CALLBACK)
    test_callbacks(8, 5);
    test_callbacks(8, 100);
    #if ISO_TP_MAX_CAN_FRAME_SIZE >= 64
    test_callbacks(64, 20);
    test_callbacks(64, 1000);
    #endif
#endif

    printf("%u checks, %u failure(s)\n", (unsigned int)g_checks, (unsigned int)g_failures);

    return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
