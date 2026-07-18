#ifndef ISOTP_USER_MOCK_H
#define ISOTP_USER_MOCK_H

#include <stddef.h>
#include <stdint.h>

#include "isotp.h"

#define ISOTP_MOCK_MAX_CAN_FRAMES 2048u
#define ISOTP_MOCK_MAX_CALLBACK_DATA 8192u

typedef struct IsoTpMockCanFrame {
    uint32_t arbitration_id;
    uint8_t data[8];
    uint8_t size;
} IsoTpMockCanFrame;

void isotp_mock_reset(void);
void isotp_mock_set_time_us(uint32_t time_us);
void isotp_mock_advance_time_us(uint32_t elapsed_us);

size_t isotp_mock_can_frame_count(void);
const IsoTpMockCanFrame* isotp_mock_can_frame(size_t index);
size_t isotp_mock_debug_count(void);

void isotp_mock_rx_done_cb(void* link, const uint8_t* data, uint32_t size, void* user_arg);
size_t isotp_mock_rx_callback_count(void);
const uint8_t* isotp_mock_rx_callback_data(void);
uint32_t isotp_mock_rx_callback_size(void);
void* isotp_mock_rx_callback_link(void);
void* isotp_mock_rx_callback_arg(void);

#endif
