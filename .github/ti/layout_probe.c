/*
 * Checks that armcl lays out isotp-c's frame structures as they appear on the
 * bus. build-ti-armcl.sh compiles this file, reads isotp_layout_probe back out
 * of the object, and compares its bytes with the frames written out by hand.
 * A compile that succeeds with the wrong bit-field order would otherwise pass
 * unnoticed, and put the PCI type in the low nibble of every frame.
 */
#include <stddef.h>

#include "isotp_defines.h"

/* Fails to compile when the condition is false. */
#define PROBE_ASSERT(name, condition) typedef char probe_assert_##name[(condition) ? 1 : -1]

PROBE_ASSERT(message_is_one_frame, sizeof(IsoTpCanMessage) == ISO_TP_MAX_CAN_FRAME_SIZE);
PROBE_ASSERT(long_first_frame_is_packed, sizeof(IsoTpFirstFrameLong) == ISO_TP_MAX_CAN_FRAME_SIZE);
PROBE_ASSERT(long_first_frame_length_at_byte_2, offsetof(IsoTpFirstFrameLong, FF_DL) == 2);

const IsoTpCanMessage isotp_layout_probe[] = {
    { { .single_frame        = { .type = 0x0, .SF_DL = 0x7 } } },                               /* 07 */
    { { .single_frame_escape = { .type = 0x0, .set_to_zero = 0x0, .SF_DL = 0x3E } } },          /* 00 3e */
    { { .first_frame_short   = { .type = 0x1, .FF_DL_high = 0x2, .FF_DL_low = 0x34 } } },       /* 12 34 */
    { { .first_frame_long    = { .type = 0x1, .set_to_zero_high = 0x0, .set_to_zero_low = 0 } } }, /* 10 00 */
    { { .consecutive_frame   = { .type = 0x2, .SN = 0x5 } } },                                  /* 25 */
    { { .flow_control        = { .type = 0x3, .FS = 0x1, .BS = 0x0A, .STmin = 0x14 } } },       /* 31 0a 14 */
};
