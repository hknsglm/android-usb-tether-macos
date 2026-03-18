#ifndef USB_DEVICE_H
#define USB_DEVICE_H

#include <libusb.h>
#include <stdint.h>
#include <stddef.h>

/* Android RNDIS USB class identifiers */
#define USB_CLASS_WIRELESS      0xE0
#define USB_SUBCLASS_RNDIS      0x01
#define USB_PROTOCOL_RNDIS      0x03

/* Alternative: CDC class with vendor-specific subclass (some Android devices) */
#define USB_CLASS_CDC           0x02
#define USB_SUBCLASS_ACM        0x02
#define USB_SUBCLASS_VENDOR     0xFF

/* USB CDC Data class */
#define USB_CLASS_CDC_DATA      0x0A

/* USB transfer constants */
#define USB_CTRL_TIMEOUT    5000
#define USB_BULK_TIMEOUT    5000
#define USB_MAX_PACKET_SIZE 16384

/* CDC send encapsulated command/response */
#define CDC_SEND_ENCAPSULATED   0x00
#define CDC_GET_ENCAPSULATED    0x01

typedef struct {
    libusb_context *ctx;
    libusb_device_handle *handle;
    uint8_t iface_comm;      /* Communication interface number */
    uint8_t iface_data;      /* Data interface number */
    uint8_t ep_in;           /* Bulk IN endpoint (data from device) */
    uint8_t ep_out;          /* Bulk OUT endpoint (data to device) */
    uint8_t ep_int;          /* Interrupt endpoint (notifications) */
    uint16_t ep_in_max;      /* Max packet size for bulk IN */
    uint16_t ep_out_max;     /* Max packet size for bulk OUT */
    uint16_t vid;
    uint16_t pid;
    int      kernel_detached; /* Whether we detached a kernel driver */
} usb_device_t;

/* Find and open an Android RNDIS device. Returns 0 on success. */
int usb_find_rndis_device(usb_device_t *dev);

/* Send an RNDIS control message (via CDC encapsulated command). */
int usb_send_ctrl(usb_device_t *dev, const uint8_t *data, size_t len);

/* Receive an RNDIS control response (via CDC encapsulated response). */
int usb_recv_ctrl(usb_device_t *dev, uint8_t *data, size_t max_len);

/* Send bulk data to the device. Returns bytes sent or negative error. */
int usb_send_bulk(usb_device_t *dev, const uint8_t *data, size_t len);

/* Receive bulk data from the device. Returns bytes received or negative error. */
int usb_recv_bulk(usb_device_t *dev, uint8_t *data, size_t max_len, int timeout_ms);

/* Close and release the USB device. */
void usb_close_device(usb_device_t *dev);

#endif /* USB_DEVICE_H */
