#include "mocks/isotp_user_mock.h"

#include <stdarg.h>
#include <string.h>

static IsoTpMockCanFrame can_frames[ISOTP_MOCK_MAX_CAN_FRAMES];
static size_t can_frame_count;
static size_t debug_count;
static uint32_t current_time_us;

static size_t rx_callback_count;
static uint8_t rx_callback_data[ISOTP_MOCK_MAX_CALLBACK_DATA];
static uint32_t rx_callback_size;
static void* rx_callback_link;
static void* rx_callback_arg;

void isotp_mock_reset(void) {
    (void)memset(can_frames, 0, sizeof(can_frames));
    can_frame_count = 0;
    debug_count = 0;
    current_time_us = 0;

    rx_callback_count = 0;
    (void)memset(rx_callback_data, 0, sizeof(rx_callback_data));
    rx_callback_size = 0;
    rx_callback_link = NULL;
    rx_callback_arg = NULL;
}

void isotp_mock_set_time_us(uint32_t time_us) { current_time_us = time_us; }

void isotp_mock_advance_time_us(uint32_t elapsed_us) { current_time_us += elapsed_us; }

size_t isotp_mock_can_frame_count(void) { return can_frame_count; }

const IsoTpMockCanFrame* isotp_mock_can_frame(size_t index) {
    if (index >= can_frame_count) { return NULL; }
    return &can_frames[index];
}

size_t isotp_mock_debug_count(void) { return debug_count; }

void isotp_user_debug(const char* message, ...) {
    (void)message;
    ++debug_count;
}

int isotp_user_send_can(const uint32_t arbitration_id, const uint8_t* data, const uint8_t size) {
    IsoTpMockCanFrame* frame;

    if (can_frame_count >= ISOTP_MOCK_MAX_CAN_FRAMES || size > sizeof(can_frames[0].data)) { return ISOTP_RET_NOSPACE; }

    frame = &can_frames[can_frame_count++];
    frame->arbitration_id = arbitration_id;
    frame->size = size;
    (void)memcpy(frame->data, data, size);
    return ISOTP_RET_OK;
}

uint32_t isotp_user_get_us(void) { return current_time_us; }

void isotp_mock_rx_done_cb(void* link, const uint8_t* data, uint32_t size, void* user_arg) {
    ++rx_callback_count;
    rx_callback_link = link;
    rx_callback_arg = user_arg;
    rx_callback_size = size;

    if (size > sizeof(rx_callback_data)) { size = sizeof(rx_callback_data); }
    (void)memcpy(rx_callback_data, data, size);
}

size_t isotp_mock_rx_callback_count(void) { return rx_callback_count; }

const uint8_t* isotp_mock_rx_callback_data(void) { return rx_callback_data; }

uint32_t isotp_mock_rx_callback_size(void) { return rx_callback_size; }

void* isotp_mock_rx_callback_link(void) { return rx_callback_link; }

void* isotp_mock_rx_callback_arg(void) { return rx_callback_arg; }
