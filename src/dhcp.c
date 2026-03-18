#include "dhcp.h"
#include "log.h"

#include <string.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <arpa/inet.h>

#define TAG "dhcp"

void dhcp_parse_options(const uint8_t *opts, size_t len,
                        struct in_addr *gateway, struct in_addr *netmask,
                        struct in_addr *dns1, struct in_addr *dns2)
{
    size_t i = 0;
    while (i < len) {
        uint8_t opt = opts[i++];
        if (opt == 0xFF) break;
        if (opt == 0x00) continue;
        if (i >= len) break;
        uint8_t olen = opts[i++];
        if (i + olen > len) break;

        switch (opt) {
        case 1: /* Subnet mask */
            if (olen >= 4)
                memcpy(netmask, &opts[i], 4);
            break;
        case 3: /* Router/gateway */
            if (olen >= 4)
                memcpy(gateway, &opts[i], 4);
            break;
        case 6: /* DNS */
            if (olen >= 4)
                memcpy(dns1, &opts[i], 4);
            if (olen >= 8)
                memcpy(dns2, &opts[i + 4], 4);
            break;
        }
        i += olen;
    }
}

int dhcp_build_discover(uint8_t *buf, size_t buf_size,
                        const uint8_t *mac, uint32_t xid)
{
    if (buf_size < 300)
        return -1;

    memset(buf, 0, buf_size);

    /* Ethernet header */
    eth_hdr_t *eth = (eth_hdr_t *)buf;
    memset(eth->dst, 0xFF, 6);
    memcpy(eth->src, mac, 6);
    eth->ethertype = htons(0x0800);

    /* IP header */
    struct ip *iph = (struct ip *)(buf + sizeof(eth_hdr_t));
    iph->ip_v = 4;
    iph->ip_hl = 5;
    iph->ip_tos = 0;
    iph->ip_id = 0;
    iph->ip_off = 0;
    iph->ip_ttl = 64;
    iph->ip_p = IPPROTO_UDP;
    iph->ip_src.s_addr = INADDR_ANY;
    iph->ip_dst.s_addr = INADDR_BROADCAST;

    /* UDP header (manual, minimal) */
    uint8_t *udp = (uint8_t *)iph + 20;
    uint16_t sport = htons(DHCP_CLIENT_PORT);
    uint16_t dport = htons(DHCP_SERVER_PORT);
    memcpy(udp, &sport, 2);
    memcpy(udp + 2, &dport, 2);

    /* DHCP payload */
    dhcp_packet_t *dhcp = (dhcp_packet_t *)(udp + 8);
    dhcp->op = 1; /* BOOTREQUEST */
    dhcp->htype = 1; /* Ethernet */
    dhcp->hlen = 6;
    dhcp->xid = htonl(xid);
    dhcp->flags = htons(0x8000); /* Broadcast */
    memcpy(dhcp->chaddr, mac, 6);
    dhcp->magic = htonl(DHCP_MAGIC_COOKIE);

    /* DHCP options: discover */
    uint8_t *opts = dhcp->options;
    *opts++ = 53; *opts++ = 1; *opts++ = 1; /* DHCP Discover */
    *opts++ = 55; *opts++ = 3; *opts++ = 1; *opts++ = 3; *opts++ = 6; /* Parameter Request */
    *opts++ = 0xFF; /* End */

    size_t dhcp_len = (size_t)(opts - (uint8_t *)dhcp);
    if (dhcp_len < 300 - sizeof(eth_hdr_t) - 20 - 8)
        dhcp_len = 300 - sizeof(eth_hdr_t) - 20 - 8;

    uint16_t udp_len = htons((uint16_t)(8 + dhcp_len));
    memcpy(udp + 4, &udp_len, 2);
    memset(udp + 6, 0, 2);

    uint16_t ip_total = htons((uint16_t)(20 + 8 + dhcp_len));
    iph->ip_len = ip_total;
    iph->ip_sum = 0;

    /* IP checksum */
    uint32_t sum = 0;
    uint16_t *p = (uint16_t *)iph;
    for (int i = 0; i < 10; i++)
        sum += ntohs(p[i]);
    while (sum >> 16)
        sum = (sum & 0xFFFF) + (sum >> 16);
    iph->ip_sum = htons((uint16_t)~sum);

    return (int)(sizeof(eth_hdr_t) + 20 + 8 + dhcp_len);
}

int dhcp_build_request(uint8_t *buf, size_t buf_size,
                       const uint8_t *mac, uint32_t xid,
                       uint32_t offered_ip, uint32_t server_ip)
{
    if (buf_size < 300)
        return -1;

    memset(buf, 0, buf_size);

    eth_hdr_t *eth = (eth_hdr_t *)buf;
    memset(eth->dst, 0xFF, 6);
    memcpy(eth->src, mac, 6);
    eth->ethertype = htons(0x0800);

    struct ip *iph = (struct ip *)(buf + sizeof(eth_hdr_t));
    iph->ip_v = 4;
    iph->ip_hl = 5;
    iph->ip_ttl = 64;
    iph->ip_p = IPPROTO_UDP;
    iph->ip_src.s_addr = INADDR_ANY;
    iph->ip_dst.s_addr = INADDR_BROADCAST;

    uint8_t *udp = (uint8_t *)iph + 20;
    uint16_t sport = htons(DHCP_CLIENT_PORT);
    uint16_t dport = htons(DHCP_SERVER_PORT);
    memcpy(udp, &sport, 2);
    memcpy(udp + 2, &dport, 2);

    dhcp_packet_t *dhcp = (dhcp_packet_t *)(udp + 8);
    dhcp->op = 1;
    dhcp->htype = 1;
    dhcp->hlen = 6;
    dhcp->xid = htonl(xid);
    dhcp->flags = htons(0x8000);
    memcpy(dhcp->chaddr, mac, 6);
    dhcp->magic = htonl(DHCP_MAGIC_COOKIE);

    uint8_t *opts = dhcp->options;
    *opts++ = 53; *opts++ = 1; *opts++ = 3; /* DHCP Request */
    *opts++ = 50; *opts++ = 4; memcpy(opts, &offered_ip, 4); opts += 4;
    *opts++ = 54; *opts++ = 4; memcpy(opts, &server_ip, 4); opts += 4;
    *opts++ = 55; *opts++ = 3; *opts++ = 1; *opts++ = 3; *opts++ = 6;
    *opts++ = 0xFF;

    size_t dhcp_len = (size_t)(opts - (uint8_t *)dhcp);
    if (dhcp_len < 300 - sizeof(eth_hdr_t) - 20 - 8)
        dhcp_len = 300 - sizeof(eth_hdr_t) - 20 - 8;

    uint16_t udp_len = htons((uint16_t)(8 + dhcp_len));
    memcpy(udp + 4, &udp_len, 2);
    memset(udp + 6, 0, 2);

    iph->ip_len = htons((uint16_t)(20 + 8 + dhcp_len));
    iph->ip_sum = 0;
    uint32_t sum = 0;
    uint16_t *p = (uint16_t *)iph;
    for (int i = 0; i < 10; i++)
        sum += ntohs(p[i]);
    while (sum >> 16)
        sum = (sum & 0xFFFF) + (sum >> 16);
    iph->ip_sum = htons((uint16_t)~sum);

    return (int)(sizeof(eth_hdr_t) + 20 + 8 + dhcp_len);
}

int dhcp_discover(usb_device_t *usb, rndis_state_t *rndis, dhcp_lease_t *lease)
{
    uint8_t rndis_buf[RNDIS_BUF_SIZE];
    uint8_t eth_buf[ETH_BUF_SIZE];
    uint32_t xid = 0x12345678;
    int ret, len;

    LOG_I(TAG, "sending DHCP discover...");

    /* Build and send DHCP discover */
    len = dhcp_build_discover(eth_buf, sizeof(eth_buf), rndis->mac_addr, xid);
    if (len < 0) return -1;

    int rndis_len = rndis_build_data_packet(rndis_buf, sizeof(rndis_buf), eth_buf, len);
    if (rndis_len < 0) return -1;

    ret = usb_send_bulk(usb, rndis_buf, rndis_len);
    if (ret < 0) return -1;

    /* Wait for DHCP offer */
    struct in_addr offered_addr = {0};
    struct in_addr server_addr = {0};
    struct in_addr gw = {0}, mask = {0}, d1 = {0}, d2 = {0};

    for (int attempt = 0; attempt < 50; attempt++) {
        ret = usb_recv_bulk(usb, rndis_buf, sizeof(rndis_buf), 200);
        if (ret <= 0) continue;

        uint32_t offset = 0;
        int found_offer = 0;
        while (offset + 8 <= (uint32_t)ret) {
            const uint8_t *pkt_ptr = rndis_buf + offset;
            uint32_t msg_type, msg_len;
            memcpy(&msg_type, pkt_ptr, 4);
            memcpy(&msg_len, pkt_ptr + 4, 4);

            if (msg_len == 0 || offset + msg_len > (uint32_t)ret) break;

            const uint8_t *frame;
            size_t frame_len;
            if (rndis_parse_data_packet(pkt_ptr, msg_len, &frame, &frame_len) == 0) {
                if (frame_len >= sizeof(eth_hdr_t) + 20 + 8 + sizeof(dhcp_packet_t)) {
                    const eth_hdr_t *eth = (const eth_hdr_t *)frame;
                    if (ntohs(eth->ethertype) == 0x0800) {
                        const struct ip *iph = (const struct ip *)(frame + sizeof(eth_hdr_t));
                        if (iph->ip_p == IPPROTO_UDP) {
                            const uint8_t *udp = (const uint8_t *)iph + (iph->ip_hl * 4);
                            uint16_t dport;
                            memcpy(&dport, udp + 2, 2);
                            if (ntohs(dport) == DHCP_CLIENT_PORT) {
                                const dhcp_packet_t *dhcp = (const dhcp_packet_t *)(udp + 8);
                                if (ntohl(dhcp->magic) == DHCP_MAGIC_COOKIE && ntohl(dhcp->xid) == xid) {
                                    size_t opts_offset = (size_t)((const uint8_t *)dhcp->options - frame);
                                    size_t opts_len = frame_len - opts_offset;
                                    const uint8_t *opts = dhcp->options;

                                    int msg_type_dhcp = 0;
                                    for (size_t i = 0; i < opts_len; ) {
                                        if (opts[i] == 0xFF) break;
                                        if (opts[i] == 0x00) { i++; continue; }
                                        if (i + 1 >= opts_len) break;
                                        uint8_t olen = opts[i + 1];
                                        if (opts[i] == 53 && olen >= 1)
                                            msg_type_dhcp = opts[i + 2];
                                        if (opts[i] == 54 && olen >= 4)
                                            memcpy(&server_addr, &opts[i + 2], 4);
                                        i += 2 + olen;
                                    }

                                    if (msg_type_dhcp == 2) { /* DHCP Offer */
                                        offered_addr.s_addr = dhcp->yiaddr;
                                        dhcp_parse_options(dhcp->options, opts_len, &gw, &mask, &d1, &d2);
                                        LOG_I(TAG, "offer: %s", inet_ntoa(offered_addr));
                                        found_offer = 1;
                                    }
                                }
                            }
                        }
                    }
                }
            }
            if (found_offer) break;
            offset += msg_len;
        }
        if (found_offer) break;
    }

    if (offered_addr.s_addr == 0) {
        LOG_E(TAG, "no offer received");
        return -1;
    }

    /* Send DHCP request */
    LOG_I(TAG, "sending DHCP request for %s...", inet_ntoa(offered_addr));

    len = dhcp_build_request(eth_buf, sizeof(eth_buf), rndis->mac_addr, xid,
                             offered_addr.s_addr, server_addr.s_addr);
    if (len < 0) return -1;

    rndis_len = rndis_build_data_packet(rndis_buf, sizeof(rndis_buf), eth_buf, len);
    if (rndis_len < 0) return -1;

    ret = usb_send_bulk(usb, rndis_buf, rndis_len);
    if (ret < 0) return -1;

    /* Wait for DHCP ACK */
    for (int attempt = 0; attempt < 50; attempt++) {
        ret = usb_recv_bulk(usb, rndis_buf, sizeof(rndis_buf), 200);
        if (ret <= 0) continue;

        uint32_t offset = 0;
        while (offset + 8 <= (uint32_t)ret) {
            const uint8_t *pkt_ptr = rndis_buf + offset;
            uint32_t msg_type, msg_len;
            memcpy(&msg_type, pkt_ptr, 4);
            memcpy(&msg_len, pkt_ptr + 4, 4);

            if (msg_len == 0 || offset + msg_len > (uint32_t)ret) break;

            const uint8_t *frame;
            size_t frame_len;
            if (rndis_parse_data_packet(pkt_ptr, msg_len, &frame, &frame_len) == 0) {
                if (frame_len >= sizeof(eth_hdr_t) + 20 + 8 + sizeof(dhcp_packet_t)) {
                    const eth_hdr_t *eth = (const eth_hdr_t *)frame;
                    if (ntohs(eth->ethertype) == 0x0800) {
                        const struct ip *iph = (const struct ip *)(frame + sizeof(eth_hdr_t));
                        if (iph->ip_p == IPPROTO_UDP) {
                            const uint8_t *udp = (const uint8_t *)iph + (iph->ip_hl * 4);
                            uint16_t dport;
                            memcpy(&dport, udp + 2, 2);
                            if (ntohs(dport) == DHCP_CLIENT_PORT) {
                                const dhcp_packet_t *dhcp = (const dhcp_packet_t *)(udp + 8);
                                if (ntohl(dhcp->magic) == DHCP_MAGIC_COOKIE && ntohl(dhcp->xid) == xid) {
                                    size_t opts_offset = (size_t)((const uint8_t *)dhcp->options - frame);
                                    size_t opts_len = frame_len - opts_offset;

                                    int msg_type_dhcp = 0;
                                    for (size_t i = 0; i < opts_len; ) {
                                        if (dhcp->options[i] == 0xFF) break;
                                        if (dhcp->options[i] == 0x00) { i++; continue; }
                                        if (i + 1 >= opts_len) break;
                                        uint8_t olen = dhcp->options[i + 1];
                                        if (dhcp->options[i] == 53 && olen >= 1)
                                            msg_type_dhcp = dhcp->options[i + 2];
                                        i += 2 + olen;
                                    }

                                    if (msg_type_dhcp == 5) { /* DHCP ACK */
                                        dhcp_parse_options(dhcp->options, opts_len, &gw, &mask, &d1, &d2);

                                        struct in_addr addr;
                                        addr.s_addr = dhcp->yiaddr;
                                        strlcpy(lease->ip, inet_ntoa(addr), sizeof(lease->ip));

                                        if (gw.s_addr)
                                            strlcpy(lease->gateway, inet_ntoa(gw), sizeof(lease->gateway));
                                        else
                                            strlcpy(lease->gateway, DEFAULT_GATEWAY, sizeof(lease->gateway));

                                        if (mask.s_addr)
                                            strlcpy(lease->netmask, inet_ntoa(mask), sizeof(lease->netmask));
                                        else
                                            strlcpy(lease->netmask, DEFAULT_NETMASK, sizeof(lease->netmask));

                                        if (d1.s_addr)
                                            strlcpy(lease->dns1, inet_ntoa(d1), sizeof(lease->dns1));
                                        else
                                            strlcpy(lease->dns1, DEFAULT_DNS1, sizeof(lease->dns1));

                                        if (d2.s_addr)
                                            strlcpy(lease->dns2, inet_ntoa(d2), sizeof(lease->dns2));
                                        else
                                            strlcpy(lease->dns2, DEFAULT_DNS2, sizeof(lease->dns2));

                                        LOG_I(TAG, "ACK: ip=%s gw=%s mask=%s dns=%s,%s",
                                              lease->ip, lease->gateway, lease->netmask,
                                              lease->dns1, lease->dns2);
                                        return 0;
                                    }
                                }
                            }
                        }
                    }
                }
            }
            offset += msg_len;
        }
    }

    LOG_E(TAG, "no ACK received");
    return -1;
}
