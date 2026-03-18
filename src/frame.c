#include "frame.h"

#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>

int frame_ip_to_eth(const uint8_t *utun_pkt, size_t utun_len,
                    uint8_t *eth_buf, size_t eth_buf_size,
                    const uint8_t *src_mac, const uint8_t *gateway_mac)
{
    if (utun_len < 5) /* need AF header + at least 1 byte of IP */
        return -1;

    /* macOS utun uses network byte order for the AF header */
    uint32_t af_raw;
    memcpy(&af_raw, utun_pkt, 4);
    uint32_t af = ntohl(af_raw);

    size_t ip_len = utun_len - 4;
    size_t total = sizeof(eth_hdr_t) + ip_len;
    if (total > eth_buf_size)
        return -1;

    eth_hdr_t *eth = (eth_hdr_t *)eth_buf;
    memcpy(eth->dst, gateway_mac, 6);
    memcpy(eth->src, src_mac, 6);

    if (af == AF_INET) {
        eth->ethertype = htons(0x0800);
    } else if (af == AF_INET6) {
        eth->ethertype = htons(0x86DD);
    } else {
        /* Fallback: inspect IP version nibble directly */
        uint8_t ver = utun_pkt[4] >> 4;
        if (ver == 4)
            eth->ethertype = htons(0x0800);
        else if (ver == 6)
            eth->ethertype = htons(0x86DD);
        else
            return -1;
    }

    memcpy(eth_buf + sizeof(eth_hdr_t), utun_pkt + 4, ip_len);
    return (int)total;
}

int frame_eth_to_utun(const uint8_t *eth_frame, size_t eth_len,
                      uint8_t *utun_buf, size_t utun_buf_size)
{
    if (eth_len < sizeof(eth_hdr_t))
        return -1;

    const eth_hdr_t *eth = (const eth_hdr_t *)eth_frame;
    size_t ip_len = eth_len - sizeof(eth_hdr_t);
    size_t total = 4 + ip_len;

    if (total > utun_buf_size)
        return -1;

    /* macOS utun expects the protocol family in network byte order */
    uint32_t af_nbo;
    uint16_t ethertype = ntohs(eth->ethertype);
    if (ethertype == 0x0800)
        af_nbo = htonl(AF_INET);
    else if (ethertype == 0x86DD)
        af_nbo = htonl(AF_INET6);
    else
        return -1; /* Skip non-IP traffic (ARP handled separately) */

    memcpy(utun_buf, &af_nbo, 4);
    memcpy(utun_buf + 4, eth_frame + sizeof(eth_hdr_t), ip_len);

    return (int)total;
}
