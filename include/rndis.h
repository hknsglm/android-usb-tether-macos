#ifndef RNDIS_H
#define RNDIS_H

#include <stdint.h>
#include <stddef.h>

/* RNDIS message types */
#define RNDIS_MSG_INIT          0x00000002
#define RNDIS_MSG_INIT_C        0x80000002
#define RNDIS_MSG_HALT          0x00000003
#define RNDIS_MSG_QUERY         0x00000004
#define RNDIS_MSG_QUERY_C       0x80000004
#define RNDIS_MSG_SET           0x00000005
#define RNDIS_MSG_SET_C         0x80000005
#define RNDIS_MSG_RESET         0x00000006
#define RNDIS_MSG_RESET_C       0x80000006
#define RNDIS_MSG_INDICATE      0x00000001
#define RNDIS_MSG_KEEPALIVE     0x00000008
#define RNDIS_MSG_KEEPALIVE_C   0x80000008
#define RNDIS_MSG_PACKET        0x00000001

/* RNDIS status codes */
#define RNDIS_STATUS_SUCCESS        0x00000000
#define RNDIS_STATUS_FAILURE        0xC0000001
#define RNDIS_STATUS_INVALID_DATA   0xC0010015
#define RNDIS_STATUS_NOT_SUPPORTED  0xC00000BB
#define RNDIS_STATUS_MEDIA_CONNECT  0x4001000B
#define RNDIS_STATUS_MEDIA_DISCONNECT 0x4001000C

/* RNDIS OIDs */
#define OID_GEN_SUPPORTED_LIST      0x00010101
#define OID_GEN_HARDWARE_STATUS     0x00010102
#define OID_GEN_MEDIA_SUPPORTED     0x00010103
#define OID_GEN_MEDIA_IN_USE        0x00010104
#define OID_GEN_MAXIMUM_FRAME_SIZE  0x00010106
#define OID_GEN_LINK_SPEED          0x00010107
#define OID_GEN_TRANSMIT_BLOCK_SIZE 0x00010108
#define OID_GEN_RECEIVE_BLOCK_SIZE  0x00010109
#define OID_GEN_VENDOR_ID           0x0001010C
#define OID_GEN_VENDOR_DESCRIPTION  0x0001010D
#define OID_GEN_CURRENT_PACKET_FILTER 0x0001010E
#define OID_GEN_MAXIMUM_TOTAL_SIZE  0x00010111
#define OID_GEN_MEDIA_CONNECT_STATUS 0x00010114
#define OID_GEN_PHYSICAL_MEDIUM     0x00010202
#define OID_802_3_PERMANENT_ADDRESS 0x01010101
#define OID_802_3_CURRENT_ADDRESS   0x01010102
#define OID_802_3_MAXIMUM_LIST_SIZE 0x01010103

/* RNDIS packet filter flags */
#define RNDIS_PACKET_TYPE_DIRECTED      0x00000001
#define RNDIS_PACKET_TYPE_MULTICAST     0x00000002
#define RNDIS_PACKET_TYPE_ALL_MULTICAST 0x00000004
#define RNDIS_PACKET_TYPE_BROADCAST     0x00000008
#define RNDIS_PACKET_TYPE_PROMISCUOUS   0x00000020

/* RNDIS media types */
#define RNDIS_MEDIUM_802_3  0x00000000

/* Protocol constants */
#define RNDIS_MAJOR_VERSION 1
#define RNDIS_MINOR_VERSION 0
#define RNDIS_MAX_TRANSFER_SIZE 0x8000
#define RNDIS_MAX_PACKETS_PER_TRANSFER 1

/* RNDIS message headers */
typedef struct __attribute__((packed)) {
    uint32_t msg_type;
    uint32_t msg_len;
    uint32_t request_id;
    uint32_t major_version;
    uint32_t minor_version;
    uint32_t max_transfer_size;
} rndis_init_msg_t;

typedef struct __attribute__((packed)) {
    uint32_t msg_type;
    uint32_t msg_len;
    uint32_t request_id;
    uint32_t status;
    uint32_t major_version;
    uint32_t minor_version;
    uint32_t device_flags;
    uint32_t medium;
    uint32_t max_packets_per_transfer;
    uint32_t max_transfer_size;
    uint32_t packet_alignment_factor;
    uint32_t reserved1;
    uint32_t reserved2;
} rndis_init_cmplt_t;

typedef struct __attribute__((packed)) {
    uint32_t msg_type;
    uint32_t msg_len;
    uint32_t request_id;
    uint32_t oid;
    uint32_t len;
    uint32_t offset;
    uint32_t reserved;
    /* OID data follows */
} rndis_query_msg_t;

typedef struct __attribute__((packed)) {
    uint32_t msg_type;
    uint32_t msg_len;
    uint32_t request_id;
    uint32_t status;
    uint32_t len;
    uint32_t offset;
} rndis_query_cmplt_t;

typedef struct __attribute__((packed)) {
    uint32_t msg_type;
    uint32_t msg_len;
    uint32_t request_id;
    uint32_t oid;
    uint32_t len;
    uint32_t offset;
    uint32_t reserved;
    /* OID data follows */
} rndis_set_msg_t;

typedef struct __attribute__((packed)) {
    uint32_t msg_type;
    uint32_t msg_len;
    uint32_t request_id;
    uint32_t status;
} rndis_set_cmplt_t;

typedef struct __attribute__((packed)) {
    uint32_t msg_type;
    uint32_t msg_len;
    uint32_t status;
    uint32_t addressing_reset;
} rndis_reset_cmplt_t;

typedef struct __attribute__((packed)) {
    uint32_t msg_type;
    uint32_t msg_len;
    uint32_t request_id;
} rndis_keepalive_msg_t;

typedef struct __attribute__((packed)) {
    uint32_t msg_type;
    uint32_t msg_len;
    uint32_t request_id;
    uint32_t status;
} rndis_keepalive_cmplt_t;

typedef struct __attribute__((packed)) {
    uint32_t msg_type;
    uint32_t msg_len;
    uint32_t data_offset;
    uint32_t data_len;
    uint32_t oob_data_offset;
    uint32_t oob_data_len;
    uint32_t num_oob_elements;
    uint32_t per_packet_info_offset;
    uint32_t per_packet_info_len;
    uint32_t reserved1;
    uint32_t reserved2;
    /* Payload follows at data_offset from start of data_offset field */
} rndis_data_packet_t;

typedef struct __attribute__((packed)) {
    uint32_t msg_type;
    uint32_t msg_len;
    uint32_t status;
    uint32_t status_buf_len;
    uint32_t status_buf_offset;
} rndis_indicate_status_t;

/* RNDIS context for a connection */
typedef struct {
    uint32_t request_id;
    uint32_t max_transfer_size;
    uint32_t medium;
    uint8_t  mac_addr[6];
    int      link_up;
} rndis_state_t;

/* API */
int rndis_build_init(uint8_t *buf, size_t buf_size, rndis_state_t *state);
int rndis_parse_init_cmplt(const uint8_t *buf, size_t len, rndis_state_t *state);
int rndis_build_query(uint8_t *buf, size_t buf_size, uint32_t oid, rndis_state_t *state);
int rndis_parse_query_cmplt(const uint8_t *buf, size_t len, uint8_t *data_out, size_t *data_len);
int rndis_build_set(uint8_t *buf, size_t buf_size, uint32_t oid,
                    const uint8_t *data, size_t data_len, rndis_state_t *state);
int rndis_parse_set_cmplt(const uint8_t *buf, size_t len);
int rndis_build_keepalive(uint8_t *buf, size_t buf_size, rndis_state_t *state);
int rndis_build_data_packet(uint8_t *buf, size_t buf_size,
                            const uint8_t *eth_frame, size_t eth_len);
int rndis_parse_data_packet(const uint8_t *buf, size_t len,
                            const uint8_t **eth_frame, size_t *eth_len);
int rndis_parse_indicate_status(const uint8_t *buf, size_t len, uint32_t *status);

#endif /* RNDIS_H */
