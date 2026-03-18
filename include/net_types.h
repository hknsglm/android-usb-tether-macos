#ifndef NET_TYPES_H
#define NET_TYPES_H

#include <stdint.h>
#include <stddef.h>

/* Buffer sizes - must be >= device's max_transfer_size (seen 23700) */
#define RNDIS_BUF_SIZE  32768
#define ETH_BUF_SIZE    2048

/* ARP constants */
#define ARP_ETHERTYPE   0x0806
#define ARP_HW_ETHERNET 1
#define ARP_OP_REQUEST  1
#define ARP_OP_REPLY    2

/* DHCP constants */
#define DHCP_SERVER_PORT  67
#define DHCP_CLIENT_PORT  68
#define DHCP_MAGIC_COOKIE 0x63825363

/* Ethernet header */
typedef struct __attribute__((packed)) {
    uint8_t  dst[6];
    uint8_t  src[6];
    uint16_t ethertype;
} eth_hdr_t;

/* ARP packet (for IPv4 over Ethernet) */
typedef struct __attribute__((packed)) {
    uint16_t hw_type;
    uint16_t proto_type;
    uint8_t  hw_len;
    uint8_t  proto_len;
    uint16_t opcode;
    uint8_t  sender_mac[6];
    uint32_t sender_ip;
    uint8_t  target_mac[6];
    uint32_t target_ip;
} arp_packet_t;

/* Minimal DHCP packet for parsing offers */
typedef struct __attribute__((packed)) {
    uint8_t  op;
    uint8_t  htype;
    uint8_t  hlen;
    uint8_t  hops;
    uint32_t xid;
    uint16_t secs;
    uint16_t flags;
    uint32_t ciaddr;
    uint32_t yiaddr;
    uint32_t siaddr;
    uint32_t giaddr;
    uint8_t  chaddr[16];
    uint8_t  sname[64];
    uint8_t  file[128];
    uint32_t magic;
    uint8_t  options[];
} dhcp_packet_t;

/* Default network configuration (used as DHCP fallback) */
#define DEFAULT_STATIC_IP   "192.168.42.100"
#define DEFAULT_GATEWAY     "192.168.42.129"
#define DEFAULT_NETMASK     "255.255.255.0"
#define DEFAULT_DNS1        "8.8.8.8"
#define DEFAULT_DNS2        "8.8.4.4"

#endif /* NET_TYPES_H */
