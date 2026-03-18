#include "proto_driver.h"
#include "rndis.h"
#include "log.h"
#include "net_types.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define TAG "rndis-drv"

static int rndis_drv_init(proto_driver_t *drv, usb_device_t *usb)
{
    rndis_state_t *state = (rndis_state_t *)drv->priv;
    uint8_t buf[RNDIS_BUF_SIZE];
    int len, ret;

    /* Step 1: Send RNDIS init */
    len = rndis_build_init(buf, sizeof(buf), state);
    if (len < 0) return -1;

    ret = usb_send_ctrl(usb, buf, len);
    if (ret < 0) return -1;

    usleep(100000);

    ret = usb_recv_ctrl(usb, buf, sizeof(buf));
    if (ret < 0) return -1;

    if (rndis_parse_init_cmplt(buf, ret, state) < 0) {
        LOG_E(TAG, "RNDIS init failed");
        return -1;
    }

    /* Step 2: Query MAC address */
    len = rndis_build_query(buf, sizeof(buf), OID_802_3_PERMANENT_ADDRESS, state);
    if (len < 0) return -1;

    ret = usb_send_ctrl(usb, buf, len);
    if (ret < 0) return -1;

    usleep(50000);

    ret = usb_recv_ctrl(usb, buf, sizeof(buf));
    if (ret < 0) return -1;

    size_t mac_len = 6;
    if (rndis_parse_query_cmplt(buf, ret, state->mac_addr, &mac_len) < 0) {
        LOG_W(TAG, "failed to query MAC address, using fallback");
        state->mac_addr[0] = 0x02;
        state->mac_addr[1] = 0x42;
        state->mac_addr[2] = 0xAC;
        state->mac_addr[3] = 0x11;
        state->mac_addr[4] = 0x00;
        state->mac_addr[5] = 0x01;
    }

    LOG_I(TAG, "MAC: %02x:%02x:%02x:%02x:%02x:%02x",
          state->mac_addr[0], state->mac_addr[1], state->mac_addr[2],
          state->mac_addr[3], state->mac_addr[4], state->mac_addr[5]);

    /* Step 3: Set packet filter */
    uint32_t filter = RNDIS_PACKET_TYPE_DIRECTED |
                      RNDIS_PACKET_TYPE_MULTICAST |
                      RNDIS_PACKET_TYPE_ALL_MULTICAST |
                      RNDIS_PACKET_TYPE_BROADCAST;

    len = rndis_build_set(buf, sizeof(buf), OID_GEN_CURRENT_PACKET_FILTER,
                          (uint8_t *)&filter, sizeof(filter), state);
    if (len < 0) return -1;

    ret = usb_send_ctrl(usb, buf, len);
    if (ret < 0) return -1;

    usleep(50000);

    ret = usb_recv_ctrl(usb, buf, sizeof(buf));
    if (ret < 0) return -1;

    if (rndis_parse_set_cmplt(buf, ret) < 0) {
        LOG_W(TAG, "failed to set packet filter");
    }

    state->link_up = 1;
    LOG_I(TAG, "RNDIS initialization complete");
    return 0;
}

static int rndis_drv_get_mac(proto_driver_t *drv, uint8_t mac[6])
{
    rndis_state_t *state = (rndis_state_t *)drv->priv;
    memcpy(mac, state->mac_addr, 6);
    return 0;
}

static int rndis_drv_wrap_frame(proto_driver_t *drv, const uint8_t *eth, size_t eth_len,
                                uint8_t *out, size_t out_size)
{
    (void)drv;
    return rndis_build_data_packet(out, out_size, eth, eth_len);
}

static int rndis_drv_unwrap_data(proto_driver_t *drv, const uint8_t *usb_data, size_t usb_len,
                                 proto_frame_cb on_frame, void *ctx)
{
    (void)drv;
    uint32_t offset = 0;

    while (offset + 8 <= (uint32_t)usb_len) {
        const uint8_t *pkt_ptr = usb_data + offset;
        uint32_t msg_len;
        memcpy(&msg_len, pkt_ptr + 4, 4);

        if (msg_len == 0 || offset + msg_len > (uint32_t)usb_len)
            break;

        const uint8_t *frame;
        size_t frame_len;
        if (rndis_parse_data_packet(pkt_ptr, msg_len, &frame, &frame_len) == 0) {
            on_frame(frame, frame_len, ctx);
        }

        offset += msg_len;
    }

    return 0;
}

static int rndis_drv_keepalive(proto_driver_t *drv, usb_device_t *usb)
{
    rndis_state_t *state = (rndis_state_t *)drv->priv;
    uint8_t buf[RNDIS_BUF_SIZE];

    int klen = rndis_build_keepalive(buf, sizeof(buf), state);
    if (klen > 0) {
        usb_send_ctrl(usb, buf, klen);
        usleep(10000);
        usb_recv_ctrl(usb, buf, sizeof(buf));
    }
    return 0;
}

static int rndis_drv_get_usb_filters(proto_driver_t *drv,
                                     usb_iface_filter_t *filters, int max_filters)
{
    (void)drv;
    if (max_filters < 4) return 0;

    /* Standard RNDIS: Wireless Controller */
    filters[0] = (usb_iface_filter_t){ USB_CLASS_WIRELESS, USB_SUBCLASS_RNDIS, USB_PROTOCOL_RNDIS };
    /* CDC + ACM */
    filters[1] = (usb_iface_filter_t){ USB_CLASS_CDC, USB_SUBCLASS_ACM, USB_PROTOCOL_RNDIS };
    /* CDC + vendor-specific */
    filters[2] = (usb_iface_filter_t){ USB_CLASS_CDC, USB_SUBCLASS_VENDOR, USB_PROTOCOL_RNDIS };
    /* Vendor-specific */
    filters[3] = (usb_iface_filter_t){ 0xFF, 0x01, 0x03 };

    return 4;
}

static void rndis_drv_destroy(proto_driver_t *drv)
{
    if (drv) {
        free(drv->priv);
        free(drv);
    }
}

proto_driver_t *proto_rndis_create(void)
{
    proto_driver_t *drv = calloc(1, sizeof(proto_driver_t));
    if (!drv) return NULL;

    rndis_state_t *state = calloc(1, sizeof(rndis_state_t));
    if (!state) {
        free(drv);
        return NULL;
    }

    drv->name = "rndis";
    drv->init = rndis_drv_init;
    drv->get_mac = rndis_drv_get_mac;
    drv->wrap_frame = rndis_drv_wrap_frame;
    drv->unwrap_data = rndis_drv_unwrap_data;
    drv->keepalive = rndis_drv_keepalive;
    drv->get_usb_filters = rndis_drv_get_usb_filters;
    drv->destroy = rndis_drv_destroy;
    drv->priv = state;

    return drv;
}
