#include "usb_device.h"
#include "log.h"
#include <string.h>

#define TAG "usb"

static int is_rndis_interface(const struct libusb_interface_descriptor *iface)
{
    /* Standard RNDIS: Wireless Controller class */
    if (iface->bInterfaceClass == USB_CLASS_WIRELESS &&
        iface->bInterfaceSubClass == USB_SUBCLASS_RNDIS &&
        iface->bInterfaceProtocol == USB_PROTOCOL_RNDIS)
        return 1;

    /* Alternative RNDIS: CDC with vendor-specific subclass (common on Android) */
    if (iface->bInterfaceClass == USB_CLASS_CDC &&
        iface->bInterfaceSubClass == USB_SUBCLASS_ACM &&
        iface->bInterfaceProtocol == USB_PROTOCOL_RNDIS)
        return 1;

    /* Some Android devices use CDC + vendor-specific */
    if (iface->bInterfaceClass == USB_CLASS_CDC &&
        iface->bInterfaceSubClass == USB_SUBCLASS_VENDOR &&
        iface->bInterfaceProtocol == USB_PROTOCOL_RNDIS)
        return 1;

    /* Vendor-specific class with RNDIS protocol */
    if (iface->bInterfaceClass == 0xFF &&
        iface->bInterfaceSubClass == 0x01 &&
        iface->bInterfaceProtocol == 0x03)
        return 1;

    return 0;
}

static int is_data_interface(const struct libusb_interface_descriptor *iface)
{
    return iface->bInterfaceClass == USB_CLASS_CDC_DATA;
}

static int find_endpoints(usb_device_t *dev, const struct libusb_interface_descriptor *iface,
                          int is_comm)
{
    for (int i = 0; i < iface->bNumEndpoints; i++) {
        const struct libusb_endpoint_descriptor *ep = &iface->endpoint[i];
        uint8_t type = ep->bmAttributes & LIBUSB_TRANSFER_TYPE_MASK;
        uint8_t dir = ep->bEndpointAddress & LIBUSB_ENDPOINT_DIR_MASK;

        if (type == LIBUSB_TRANSFER_TYPE_INTERRUPT && is_comm) {
            dev->ep_int = ep->bEndpointAddress;
        } else if (type == LIBUSB_TRANSFER_TYPE_BULK) {
            if (dir == LIBUSB_ENDPOINT_IN) {
                dev->ep_in = ep->bEndpointAddress;
                dev->ep_in_max = ep->wMaxPacketSize;
            } else {
                dev->ep_out = ep->bEndpointAddress;
                dev->ep_out_max = ep->wMaxPacketSize;
            }
        }
    }
    return 0;
}

int usb_find_rndis_device(usb_device_t *dev)
{
    int ret;

    memset(dev, 0, sizeof(*dev));

    ret = libusb_init(&dev->ctx);
    if (ret < 0) {
        LOG_E(TAG, "failed to init libusb: %s", libusb_strerror(ret));
        return -1;
    }

    libusb_device **list;
    ssize_t count = libusb_get_device_list(dev->ctx, &list);
    if (count < 0) {
        LOG_E(TAG, "failed to get device list");
        libusb_exit(dev->ctx);
        return -1;
    }

    int found = 0;
    for (ssize_t i = 0; i < count && !found; i++) {
        struct libusb_device_descriptor desc;
        ret = libusb_get_device_descriptor(list[i], &desc);
        if (ret < 0)
            continue;

        struct libusb_config_descriptor *config;
        ret = libusb_get_active_config_descriptor(list[i], &config);
        if (ret < 0)
            continue;

        /* Look for RNDIS communication interface */
        int comm_iface = -1;
        int data_iface = -1;

        for (int j = 0; j < config->bNumInterfaces; j++) {
            const struct libusb_interface *iface = &config->interface[j];
            for (int k = 0; k < iface->num_altsetting; k++) {
                const struct libusb_interface_descriptor *alt = &iface->altsetting[k];
                if (is_rndis_interface(alt)) {
                    comm_iface = alt->bInterfaceNumber;
                    LOG_I(TAG, "found RNDIS comm interface %d on %04x:%04x "
                          "(class=%02x sub=%02x proto=%02x)",
                          comm_iface, desc.idVendor, desc.idProduct,
                          alt->bInterfaceClass, alt->bInterfaceSubClass,
                          alt->bInterfaceProtocol);
                }
                if (is_data_interface(alt) && comm_iface >= 0) {
                    data_iface = alt->bInterfaceNumber;
                }
            }
        }

        /* If we didn't find a separate data interface, try the next interface */
        if (comm_iface >= 0 && data_iface < 0) {
            for (int j = 0; j < config->bNumInterfaces; j++) {
                const struct libusb_interface *iface = &config->interface[j];
                for (int k = 0; k < iface->num_altsetting; k++) {
                    const struct libusb_interface_descriptor *alt = &iface->altsetting[k];
                    if ((int)alt->bInterfaceNumber == comm_iface + 1) {
                        data_iface = alt->bInterfaceNumber;
                    }
                }
            }
        }

        if (comm_iface >= 0 && data_iface >= 0) {
            ret = libusb_open(list[i], &dev->handle);
            if (ret < 0) {
                LOG_E(TAG, "failed to open device: %s", libusb_strerror(ret));
                libusb_free_config_descriptor(config);
                continue;
            }

            dev->vid = desc.idVendor;
            dev->pid = desc.idProduct;
            dev->iface_comm = (uint8_t)comm_iface;
            dev->iface_data = (uint8_t)data_iface;

            /* Detach kernel driver if necessary */
            if (libusb_kernel_driver_active(dev->handle, comm_iface) == 1) {
                libusb_detach_kernel_driver(dev->handle, comm_iface);
                dev->kernel_detached = 1;
            }
            if (libusb_kernel_driver_active(dev->handle, data_iface) == 1) {
                libusb_detach_kernel_driver(dev->handle, data_iface);
            }

            /* Claim interfaces */
            ret = libusb_claim_interface(dev->handle, comm_iface);
            if (ret < 0) {
                LOG_E(TAG, "failed to claim comm interface: %s", libusb_strerror(ret));
                libusb_close(dev->handle);
                libusb_free_config_descriptor(config);
                continue;
            }

            ret = libusb_claim_interface(dev->handle, data_iface);
            if (ret < 0) {
                LOG_E(TAG, "failed to claim data interface: %s", libusb_strerror(ret));
                libusb_release_interface(dev->handle, comm_iface);
                libusb_close(dev->handle);
                libusb_free_config_descriptor(config);
                continue;
            }

            /* Find endpoints */
            for (int j = 0; j < config->bNumInterfaces; j++) {
                const struct libusb_interface *iface = &config->interface[j];
                for (int k = 0; k < iface->num_altsetting; k++) {
                    const struct libusb_interface_descriptor *alt = &iface->altsetting[k];
                    if ((int)alt->bInterfaceNumber == comm_iface)
                        find_endpoints(dev, alt, 1);
                    if ((int)alt->bInterfaceNumber == data_iface)
                        find_endpoints(dev, alt, 0);
                }
            }

            /* Try alternate setting 1 on data interface if no bulk endpoints found */
            if (dev->ep_in == 0 || dev->ep_out == 0) {
                ret = libusb_set_interface_alt_setting(dev->handle, data_iface, 1);
                if (ret == 0) {
                    for (int j = 0; j < config->bNumInterfaces; j++) {
                        const struct libusb_interface *iface = &config->interface[j];
                        for (int k = 0; k < iface->num_altsetting; k++) {
                            const struct libusb_interface_descriptor *alt = &iface->altsetting[k];
                            if ((int)alt->bInterfaceNumber == data_iface &&
                                alt->bAlternateSetting == 1)
                                find_endpoints(dev, alt, 0);
                        }
                    }
                }
            }

            LOG_I(TAG, "endpoints: IN=0x%02x OUT=0x%02x INT=0x%02x",
                  dev->ep_in, dev->ep_out, dev->ep_int);

            if (dev->ep_in && dev->ep_out) {
                found = 1;
            } else {
                LOG_W(TAG, "missing bulk endpoints, skipping device");
                libusb_release_interface(dev->handle, data_iface);
                libusb_release_interface(dev->handle, comm_iface);
                libusb_close(dev->handle);
                dev->handle = NULL;
            }
        }

        libusb_free_config_descriptor(config);
    }

    libusb_free_device_list(list, 1);

    if (!found) {
        LOG_E(TAG, "no Android RNDIS device found");
        LOG_I(TAG, "make sure USB tethering is enabled on your Android device");
        libusb_exit(dev->ctx);
        return -1;
    }

    LOG_I(TAG, "opened device %04x:%04x", dev->vid, dev->pid);
    return 0;
}

int usb_send_ctrl(usb_device_t *dev, const uint8_t *data, size_t len)
{
    int ret = libusb_control_transfer(
        dev->handle,
        LIBUSB_REQUEST_TYPE_CLASS | LIBUSB_RECIPIENT_INTERFACE | LIBUSB_ENDPOINT_OUT,
        CDC_SEND_ENCAPSULATED,
        0,
        dev->iface_comm,
        (uint8_t *)data,
        (uint16_t)len,
        USB_CTRL_TIMEOUT
    );

    if (ret < 0) {
        LOG_E(TAG, "ctrl send failed: %s", libusb_strerror(ret));
        return -1;
    }

    return ret;
}

int usb_recv_ctrl(usb_device_t *dev, uint8_t *data, size_t max_len)
{
    int ret = libusb_control_transfer(
        dev->handle,
        LIBUSB_REQUEST_TYPE_CLASS | LIBUSB_RECIPIENT_INTERFACE | LIBUSB_ENDPOINT_IN,
        CDC_GET_ENCAPSULATED,
        0,
        dev->iface_comm,
        data,
        (uint16_t)max_len,
        USB_CTRL_TIMEOUT
    );

    if (ret < 0) {
        LOG_E(TAG, "ctrl recv failed: %s", libusb_strerror(ret));
        return -1;
    }

    return ret;
}

int usb_send_bulk(usb_device_t *dev, const uint8_t *data, size_t len)
{
    int transferred = 0;
    int ret = libusb_bulk_transfer(
        dev->handle,
        dev->ep_out,
        (uint8_t *)data,
        (int)len,
        &transferred,
        USB_BULK_TIMEOUT
    );

    if (ret < 0) {
        LOG_E(TAG, "bulk send failed: %s", libusb_strerror(ret));
        return -1;
    }

    return transferred;
}

int usb_recv_bulk(usb_device_t *dev, uint8_t *data, size_t max_len, int timeout_ms)
{
    int transferred = 0;
    int ret = libusb_bulk_transfer(
        dev->handle,
        dev->ep_in,
        data,
        (int)max_len,
        &transferred,
        timeout_ms
    );

    if (ret == LIBUSB_ERROR_TIMEOUT)
        return 0;

    if (ret == LIBUSB_ERROR_NO_DEVICE) {
        LOG_E(TAG, "device disconnected");
        return -2;
    }

    if (ret < 0) {
        LOG_E(TAG, "bulk recv failed: %s", libusb_strerror(ret));
        return -1;
    }

    return transferred;
}

void usb_close_device(usb_device_t *dev)
{
    if (!dev->handle)
        return;

    libusb_release_interface(dev->handle, dev->iface_data);
    libusb_release_interface(dev->handle, dev->iface_comm);

    if (dev->kernel_detached)
        libusb_attach_kernel_driver(dev->handle, dev->iface_comm);

    libusb_close(dev->handle);
    dev->handle = NULL;

    if (dev->ctx) {
        libusb_exit(dev->ctx);
        dev->ctx = NULL;
    }

    LOG_I(TAG, "device closed");
}
