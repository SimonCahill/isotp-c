/* Keep the private helper private in production while testing its actual code. */
#include "../../isotp.c"

int isotp_test_pad_frame(IsoTpCanMessage* message, uint8_t usedLength) {
    return isotp_pad_frame(message, usedLength);
}
