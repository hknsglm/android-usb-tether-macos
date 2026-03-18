#ifndef FRAME_H
#define FRAME_H

#include "net_types.h"

/* Convert an IP packet (from utun) into an Ethernet frame for sending over RNDIS.
   utun gives us: [4-byte AF header in NETWORK byte order][IP packet]
   We need: [14-byte Ethernet header][IP packet]
   Returns total frame length or -1 on error. */
int frame_ip_to_eth(const uint8_t *utun_pkt, size_t utun_len,
                    uint8_t *eth_buf, size_t eth_buf_size,
                    const uint8_t *src_mac, const uint8_t *gateway_mac);

/* Convert an Ethernet frame (from RNDIS) to a utun packet.
   We take: [14-byte Ethernet header][IP packet]
   And produce: [4-byte AF header in NETWORK byte order][IP packet]
   Returns total packet length or -1 on error. */
int frame_eth_to_utun(const uint8_t *eth_frame, size_t eth_len,
                      uint8_t *utun_buf, size_t utun_buf_size);

#endif /* FRAME_H */
