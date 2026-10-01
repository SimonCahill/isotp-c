#include <stdint.h>

#include "isotp.h"

static IsoTpLink link;
static uint8_t tx_buffer[8192];
static uint8_t rx_buffer[8192];

int isotp_user_send_can(uint32_t arbitration_id, const uint8_t* data, uint8_t size) {
    (void)arbitration_id;
    (void)data;
    (void)size;
    return ISOTP_RET_ERROR;
}

int isotp_user_send_can_xl(const IsoTpCanXlFrame* frame) {
    /* Copy frame data and metadata into the controller before returning. */
    (void)frame;
    return ISOTP_RET_OK;
}

uint32_t isotp_user_get_us(void) { return 0u; }
void isotp_user_debug(const char* message, ...) { (void)message; }

int example_can_xl_init(void) {
    const IsoTpCanXlMetadata metadata = {
        .priority_id = 0x123u,
        .acceptance_field = 0x12345678u,
        .vcid = 0u,
        .sdt = 0u,
        .flags = 0u,
    };
    return isotp_init_link_xl(&link, &metadata, 2048u, tx_buffer, sizeof(tx_buffer), rx_buffer, sizeof(rx_buffer));
}

void example_can_xl_receive(const IsoTpCanXlFrame* frame) { isotp_on_can_xl_message(&link, frame); }
