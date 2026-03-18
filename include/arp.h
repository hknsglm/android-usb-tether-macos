#ifndef ARP_H
#define ARP_H

#include "net_types.h"
#include "usb_device.h"
#include "rndis.h"

/* Build an ARP reply Ethernet frame.
   Returns frame length or -1 on error. */
int arp_build_reply(uint8_t *buf, size_t buf_size,
                    const uint8_t *our_mac, uint32_t our_ip,
                    const uint8_t *their_mac, uint32_t their_ip);

/* Handle an incoming ARP request - reply if it's asking for our IP.
   Returns the length of the reply frame in reply_buf, or 0 if not for us. */
int arp_handle_request(const uint8_t *frame, size_t frame_len,
                       uint8_t *reply_buf, size_t reply_buf_size,
                       const uint8_t *our_mac, uint32_t our_ip);

/* Send a gratuitous ARP broadcast to announce our presence.
   Uses synchronous USB bulk transfer. */
void arp_send_gratuitous(usb_device_t *usb, const uint8_t *mac, uint32_t ip_n);

#endif /* ARP_H */
