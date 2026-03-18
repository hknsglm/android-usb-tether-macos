#include "arp.h"
#include "log.h"

#include <string.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define TAG "arp"

int arp_build_reply(uint8_t *buf, size_t buf_size,
                    const uint8_t *our_mac, uint32_t our_ip,
                    const uint8_t *their_mac, uint32_t their_ip)
{
    size_t total = sizeof(eth_hdr_t) + sizeof(arp_packet_t);
    if (buf_size < total)
        return -1;

    memset(buf, 0, total);

    eth_hdr_t *eth = (eth_hdr_t *)buf;
    memcpy(eth->dst, their_mac, 6);
    memcpy(eth->src, our_mac, 6);
    eth->ethertype = htons(ARP_ETHERTYPE);

    arp_packet_t *arp = (arp_packet_t *)(buf + sizeof(eth_hdr_t));
    arp->hw_type = htons(ARP_HW_ETHERNET);
    arp->proto_type = htons(0x0800);
    arp->hw_len = 6;
    arp->proto_len = 4;
    arp->opcode = htons(ARP_OP_REPLY);
    memcpy(arp->sender_mac, our_mac, 6);
    arp->sender_ip = our_ip;
    memcpy(arp->target_mac, their_mac, 6);
    arp->target_ip = their_ip;

    return (int)total;
}

int arp_handle_request(const uint8_t *frame, size_t frame_len,
                       uint8_t *reply_buf, size_t reply_buf_size,
                       const uint8_t *our_mac, uint32_t our_ip)
{
    if (frame_len < sizeof(eth_hdr_t) + sizeof(arp_packet_t))
        return 0;

    const eth_hdr_t *eth = (const eth_hdr_t *)frame;
    if (ntohs(eth->ethertype) != ARP_ETHERTYPE)
        return 0;

    const arp_packet_t *arp = (const arp_packet_t *)(frame + sizeof(eth_hdr_t));

    if (ntohs(arp->opcode) == ARP_OP_REQUEST && arp->target_ip == our_ip) {
        struct in_addr tgt, src;
        tgt.s_addr = arp->target_ip;
        src.s_addr = arp->sender_ip;
        char tgt_str[16], src_str[16];
        strlcpy(tgt_str, inet_ntoa(tgt), sizeof(tgt_str));
        strlcpy(src_str, inet_ntoa(src), sizeof(src_str));
        LOG_D(TAG, "%s asks who-has %s -> replying with our MAC", src_str, tgt_str);

        return arp_build_reply(reply_buf, reply_buf_size,
                               our_mac, our_ip,
                               arp->sender_mac, arp->sender_ip);
    }

    return 0;
}

void arp_send_gratuitous(usb_device_t *usb, const uint8_t *mac, uint32_t ip_n)
{
    uint8_t garp_eth[ETH_BUF_SIZE];
    uint8_t garp_rndis[RNDIS_BUF_SIZE];

    memset(garp_eth, 0, sizeof(eth_hdr_t) + sizeof(arp_packet_t));
    eth_hdr_t *geth = (eth_hdr_t *)garp_eth;
    memset(geth->dst, 0xFF, 6);
    memcpy(geth->src, mac, 6);
    geth->ethertype = htons(ARP_ETHERTYPE);

    arp_packet_t *garp = (arp_packet_t *)(garp_eth + sizeof(eth_hdr_t));
    garp->hw_type = htons(ARP_HW_ETHERNET);
    garp->proto_type = htons(0x0800);
    garp->hw_len = 6;
    garp->proto_len = 4;
    garp->opcode = htons(ARP_OP_REPLY);
    memcpy(garp->sender_mac, mac, 6);
    garp->sender_ip = ip_n;
    memset(garp->target_mac, 0xFF, 6);
    garp->target_ip = ip_n;

    int garp_len = sizeof(eth_hdr_t) + sizeof(arp_packet_t);
    int grndis_len = rndis_build_data_packet(garp_rndis, sizeof(garp_rndis),
                                              garp_eth, garp_len);
    if (grndis_len > 0) {
        usb_send_bulk(usb, garp_rndis, grndis_len);
        LOG_I("net", "sent gratuitous ARP");
    }
}
