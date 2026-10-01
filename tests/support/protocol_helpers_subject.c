/* Test the real private conversions without exporting production symbols. */
#include "../../isotp.c"

uint8_t isotp_test_ceil_can_dl(uint8_t length) {
    return isotp_ceil_can_dl(length);
}

uint8_t isotp_test_us_to_st_min(uint32_t us) {
    return isotp_us_to_st_min(us);
}

uint32_t isotp_test_st_min_to_us(uint8_t stMin) {
    return isotp_st_min_to_us(stMin);
}
