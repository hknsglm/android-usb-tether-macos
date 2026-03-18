#ifndef PROTO_DRIVER_H
#define PROTO_DRIVER_H

#include "usb_device.h"
#include <stdint.h>
#include <stddef.h>

/*
 * Protocol driver interface (vtable pattern).
 *
 * Each USB tethering protocol (RNDIS, NCM, ECM) implements this interface.
 * The core bridge code uses these callbacks instead of calling protocol-specific
 * functions directly, allowing new protocols to be added without modifying
 * the bridge or session logic.
 *
 * To add a new protocol driver:
 *   1. Create a new source file (e.g., src/proto_ncm.c)
 *   2. Implement the proto_driver_t callbacks
 *   3. Provide a proto_ncm_create() function that returns a configured driver
 *   4. Register the driver name in the driver lookup table in main.c
 */

/* Callback for unwrap_data: called once per extracted Ethernet frame */
typedef void (*proto_frame_cb)(const uint8_t *frame, size_t len, void *ctx);

/* USB interface filter for device discovery */
typedef struct {
    uint8_t bclass;
    uint8_t subclass;
    uint8_t protocol;
} usb_iface_filter_t;

typedef struct proto_driver {
    const char *name;  /* "rndis", "ncm", "ecm" */

    /* Initialize the protocol over USB. Returns 0 on success.
       Called after USB device is opened and interfaces are claimed. */
    int (*init)(struct proto_driver *drv, usb_device_t *usb);

    /* Get the device MAC address (6 bytes). Returns 0 on success. */
    int (*get_mac)(struct proto_driver *drv, uint8_t mac[6]);

    /* Wrap an Ethernet frame for USB transmission.
       Writes wrapped data to 'out', returns total bytes or -1. */
    int (*wrap_frame)(struct proto_driver *drv, const uint8_t *eth, size_t eth_len,
                      uint8_t *out, size_t out_size);

    /* Unwrap USB data to extract Ethernet frame(s).
       Calls 'on_frame' callback for each extracted frame. Returns 0 on success. */
    int (*unwrap_data)(struct proto_driver *drv, const uint8_t *usb_data, size_t usb_len,
                       proto_frame_cb on_frame, void *ctx);

    /* Send keepalive message (optional, can be NULL).
       Called periodically (every ~30s) from the main thread. */
    int (*keepalive)(struct proto_driver *drv, usb_device_t *usb);

    /* Get USB interface filters for device discovery.
       Returns number of filters written to 'filters'. */
    int (*get_usb_filters)(struct proto_driver *drv,
                           usb_iface_filter_t *filters, int max_filters);

    /* Cleanup and free driver resources. */
    void (*destroy)(struct proto_driver *drv);

    void *priv;  /* Protocol-specific state (e.g., rndis_state_t for RNDIS) */
} proto_driver_t;

/* Validate that all required function pointers are set. Returns 0 on success. */
static inline int proto_driver_validate(const proto_driver_t *drv)
{
    if (!drv || !drv->name || !drv->init || !drv->get_mac ||
        !drv->wrap_frame || !drv->unwrap_data || !drv->destroy)
        return -1;
    return 0;
}

/* Built-in protocol driver constructors */

/* Create an RNDIS protocol driver. Caller must call drv->destroy() when done. */
proto_driver_t *proto_rndis_create(void);

#endif /* PROTO_DRIVER_H */
