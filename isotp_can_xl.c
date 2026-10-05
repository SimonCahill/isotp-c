#include "isotp.h"

#ifdef ISO_TP_ENABLE_CAN_XL

#if defined(__GNUC__) || defined(__clang__)
    #define ISOTP_PRIVATE_SYMBOL __attribute__((visibility("hidden")))
#else
    #define ISOTP_PRIVATE_SYMBOL
#endif

ISOTP_PRIVATE_SYMBOL int  isotp_private_send_can_xl(IsoTpLink* link, const uint8_t payload[], uint32_t size, const IsoTpCanXlMetadata* metadata);
ISOTP_PRIVATE_SYMBOL void isotp_private_on_can_xl_data(IsoTpLink* link, const uint8_t* data, uint16_t size);

static uint8_t isotp_is_valid_can_xl_metadata(const IsoTpCanXlMetadata* metadata) {
    return (uint8_t)(metadata != NULL && metadata->priority_id <= 0x07FFu && (metadata->flags & (uint8_t)~ISOTP_CAN_XL_FLAG_MASK) == 0u);
}

int isotp_init_link_xl(IsoTpLink* link, const IsoTpCanXlMetadata* metadata, uint16_t tx_dl, uint8_t* sendbuf, uint32_t sendbufsize,
                       uint8_t* recvbuf, uint32_t recvbufsize) {
    if (link == NULL || !isotp_is_valid_can_xl_metadata(metadata) || tx_dl < ISOTP_CAN_DL_CLASSIC || tx_dl > ISO_TP_MAX_CAN_XL_FRAME_SIZE) {
        isotp_user_debug("Invalid CAN XL link configuration.");
        return ISOTP_RET_ERROR;
    }

    isotp_init_link(link, 0u, sendbuf, sendbufsize, recvbuf, recvbufsize);
    link->is_can_xl             = 1u;
    link->tx_dl                 = tx_dl;
    link->rx_dl                 = ISOTP_CAN_DL_CLASSIC;
    link->can_xl_metadata       = *metadata;
    link->send_can_xl_metadata = *metadata;
    return ISOTP_RET_OK;
}

int isotp_set_tx_dl_xl(IsoTpLink* link, uint16_t tx_dl) {
    if (link == NULL || !link->is_can_xl || tx_dl < ISOTP_CAN_DL_CLASSIC || tx_dl > ISO_TP_MAX_CAN_XL_FRAME_SIZE) {
        isotp_user_debug("Invalid CAN XL TX_DL.");
        return ISOTP_RET_ERROR;
    }
    if (link->send_status == ISOTP_SEND_STATUS_INPROGRESS) {
        isotp_user_debug("Cannot change TX_DL while a transmission is in progress.\n");
        return ISOTP_RET_INPROGRESS;
    }
    link->tx_dl = tx_dl;
    return ISOTP_RET_OK;
}

uint16_t isotp_get_tx_dl_xl(const IsoTpLink* link) {
    if (link == NULL || !link->is_can_xl) {
        isotp_user_debug("Link is not a CAN XL link.");
        return 0u;
    }
    if (link->tx_dl < ISOTP_CAN_DL_CLASSIC || link->tx_dl > ISO_TP_MAX_CAN_XL_FRAME_SIZE) { return ISOTP_CAN_DL_CLASSIC; }
    return link->tx_dl;
}

int isotp_send_can_xl_with_metadata(IsoTpLink* link, const IsoTpCanXlMetadata* metadata, const uint8_t payload[], uint32_t size) {
    if (link == NULL || !link->is_can_xl || !isotp_is_valid_can_xl_metadata(metadata)) {
        isotp_user_debug("Invalid CAN XL send configuration.");
        return ISOTP_RET_ERROR;
    }
    return isotp_private_send_can_xl(link, payload, size, metadata);
}

void isotp_on_can_xl_message(IsoTpLink* link, const IsoTpCanXlFrame* frame) {
    if (link == NULL || !link->is_can_xl || frame == NULL || frame->data == NULL || !isotp_is_valid_can_xl_metadata(&frame->metadata) || frame->size < 2u
        || frame->size > ISO_TP_MAX_CAN_XL_FRAME_SIZE) {
        return;
    }
    isotp_private_on_can_xl_data(link, frame->data, frame->size);
}

#endif
