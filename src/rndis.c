#include "rndis.h"
#include "log.h"
#include <string.h>

#define TAG "rndis"

int rndis_build_init(uint8_t *buf, size_t buf_size, rndis_state_t *state)
{
    rndis_init_msg_t *msg = (rndis_init_msg_t *)buf;

    if (buf_size < sizeof(rndis_init_msg_t))
        return -1;

    memset(msg, 0, sizeof(*msg));
    msg->msg_type = RNDIS_MSG_INIT;
    msg->msg_len = sizeof(rndis_init_msg_t);
    msg->request_id = ++state->request_id;
    msg->major_version = RNDIS_MAJOR_VERSION;
    msg->minor_version = RNDIS_MINOR_VERSION;
    msg->max_transfer_size = RNDIS_MAX_TRANSFER_SIZE;

    return (int)msg->msg_len;
}

int rndis_parse_init_cmplt(const uint8_t *buf, size_t len, rndis_state_t *state)
{
    const rndis_init_cmplt_t *cmplt = (const rndis_init_cmplt_t *)buf;

    if (len < sizeof(rndis_init_cmplt_t))
        return -1;
    if (cmplt->msg_type != RNDIS_MSG_INIT_C)
        return -1;
    if (cmplt->status != RNDIS_STATUS_SUCCESS)
        return -1;

    state->max_transfer_size = cmplt->max_transfer_size;
    state->medium = cmplt->medium;

    LOG_I(TAG, "init complete: max_transfer=%u, medium=%u, alignment=%u",
          cmplt->max_transfer_size, cmplt->medium, cmplt->packet_alignment_factor);

    return 0;
}

int rndis_build_query(uint8_t *buf, size_t buf_size, uint32_t oid, rndis_state_t *state)
{
    rndis_query_msg_t *msg = (rndis_query_msg_t *)buf;

    if (buf_size < sizeof(rndis_query_msg_t))
        return -1;

    memset(msg, 0, sizeof(*msg));
    msg->msg_type = RNDIS_MSG_QUERY;
    msg->msg_len = sizeof(rndis_query_msg_t);
    msg->request_id = ++state->request_id;
    msg->oid = oid;
    msg->len = 0;
    msg->offset = 20; /* offset from start of request_id to data (no input data) */

    return (int)msg->msg_len;
}

int rndis_parse_query_cmplt(const uint8_t *buf, size_t len, uint8_t *data_out, size_t *data_len)
{
    const rndis_query_cmplt_t *cmplt = (const rndis_query_cmplt_t *)buf;

    if (len < sizeof(rndis_query_cmplt_t))
        return -1;
    if (cmplt->msg_type != RNDIS_MSG_QUERY_C)
        return -1;
    if (cmplt->status != RNDIS_STATUS_SUCCESS)
        return -1;

    if (cmplt->len > 0 && data_out && data_len) {
        /* Data starts at offset from the start of the request_id field (byte 8) */
        size_t data_start = 8 + cmplt->offset;
        if (data_start + cmplt->len > len)
            return -1;
        size_t copy_len = cmplt->len;
        if (copy_len > *data_len)
            copy_len = *data_len;
        memcpy(data_out, buf + data_start, copy_len);
        *data_len = copy_len;
    }

    return 0;
}

int rndis_build_set(uint8_t *buf, size_t buf_size, uint32_t oid,
                    const uint8_t *data, size_t data_len, rndis_state_t *state)
{
    rndis_set_msg_t *msg = (rndis_set_msg_t *)buf;
    size_t total = sizeof(rndis_set_msg_t) + data_len;

    if (buf_size < total)
        return -1;

    memset(msg, 0, sizeof(*msg));
    msg->msg_type = RNDIS_MSG_SET;
    msg->msg_len = (uint32_t)total;
    msg->request_id = ++state->request_id;
    msg->oid = oid;
    msg->len = (uint32_t)data_len;
    msg->offset = 20; /* offset from request_id field to OID data */

    if (data_len > 0)
        memcpy(buf + sizeof(rndis_set_msg_t), data, data_len);

    return (int)total;
}

int rndis_parse_set_cmplt(const uint8_t *buf, size_t len)
{
    const rndis_set_cmplt_t *cmplt = (const rndis_set_cmplt_t *)buf;

    if (len < sizeof(rndis_set_cmplt_t))
        return -1;
    if (cmplt->msg_type != RNDIS_MSG_SET_C)
        return -1;
    if (cmplt->status != RNDIS_STATUS_SUCCESS)
        return -1;

    return 0;
}

int rndis_build_keepalive(uint8_t *buf, size_t buf_size, rndis_state_t *state)
{
    rndis_keepalive_msg_t *msg = (rndis_keepalive_msg_t *)buf;

    if (buf_size < sizeof(rndis_keepalive_msg_t))
        return -1;

    memset(msg, 0, sizeof(*msg));
    msg->msg_type = RNDIS_MSG_KEEPALIVE;
    msg->msg_len = sizeof(rndis_keepalive_msg_t);
    msg->request_id = ++state->request_id;

    return (int)msg->msg_len;
}

int rndis_build_data_packet(uint8_t *buf, size_t buf_size,
                            const uint8_t *eth_frame, size_t eth_len)
{
    rndis_data_packet_t *pkt = (rndis_data_packet_t *)buf;
    size_t total = sizeof(rndis_data_packet_t) + eth_len;

    if (buf_size < total)
        return -1;

    memset(pkt, 0, sizeof(*pkt));
    pkt->msg_type = RNDIS_MSG_PACKET;
    pkt->msg_len = (uint32_t)total;
    pkt->data_offset = 36; /* offset from start of data_offset field to payload */
    pkt->data_len = (uint32_t)eth_len;

    memcpy(buf + sizeof(rndis_data_packet_t), eth_frame, eth_len);

    return (int)total;
}

int rndis_parse_data_packet(const uint8_t *buf, size_t len,
                            const uint8_t **eth_frame, size_t *eth_len)
{
    const rndis_data_packet_t *pkt = (const rndis_data_packet_t *)buf;

    if (len < sizeof(rndis_data_packet_t))
        return -1;
    if (pkt->msg_type != RNDIS_MSG_PACKET)
        return -1;
    if (pkt->data_len == 0)
        return -1;

    /* Data starts at data_offset bytes from the start of the data_offset field (byte 8) */
    size_t data_start = 8 + pkt->data_offset;
    if (data_start + pkt->data_len > len)
        return -1;

    *eth_frame = buf + data_start;
    *eth_len = pkt->data_len;

    return 0;
}

int rndis_parse_indicate_status(const uint8_t *buf, size_t len, uint32_t *status)
{
    const rndis_indicate_status_t *ind = (const rndis_indicate_status_t *)buf;

    if (len < sizeof(rndis_indicate_status_t))
        return -1;

    *status = ind->status;
    return 0;
}
