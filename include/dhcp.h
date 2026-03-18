#ifndef DHCP_H
#define DHCP_H

#include "net_types.h"
#include "usb_device.h"
#include "rndis.h"

#include <netinet/in.h>

/* Result of a DHCP lease */
typedef struct {
    char ip[16];
    char gateway[16];
    char netmask[16];
    char dns1[16];
    char dns2[16];
} dhcp_lease_t;

/* Perform DHCP discover/offer/request/ack sequence over USB.
   Returns 0 on success, -1 on failure. */
int dhcp_discover(usb_device_t *usb, rndis_state_t *rndis, dhcp_lease_t *lease);

/* Parse DHCP options from an offer/ack to extract gateway, netmask, DNS */
void dhcp_parse_options(const uint8_t *opts, size_t len,
                        struct in_addr *gateway, struct in_addr *netmask,
                        struct in_addr *dns1, struct in_addr *dns2);

/* Build a DHCP discover packet inside an Ethernet frame.
   Returns frame length or -1 on error. */
int dhcp_build_discover(uint8_t *buf, size_t buf_size,
                        const uint8_t *mac, uint32_t xid);

/* Build a DHCP request packet inside an Ethernet frame.
   Returns frame length or -1 on error. */
int dhcp_build_request(uint8_t *buf, size_t buf_size,
                       const uint8_t *mac, uint32_t xid,
                       uint32_t offered_ip, uint32_t server_ip);

#endif /* DHCP_H */
