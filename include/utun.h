#ifndef UTUN_H
#define UTUN_H

#include <stdint.h>
#include <stddef.h>

/* MTU for the utun interface (standard Ethernet minus RNDIS overhead) */
#define UTUN_MTU 1500

typedef struct {
    int fd;              /* utun file descriptor */
    int unit;            /* utun unit number (e.g., 0 for utun0) */
    char ifname[16];     /* Interface name (e.g., "utun5") */
} utun_t;

/* Create a utun interface. Returns 0 on success. */
int utun_create(utun_t *tun);

/* Read a packet from the utun interface. Returns bytes read or negative error.
   The returned packet includes a 4-byte protocol header (AF_INET/AF_INET6). */
int utun_read(utun_t *tun, uint8_t *buf, size_t buf_size);

/* Write a packet to the utun interface. Returns bytes written or negative error.
   The packet must include a 4-byte protocol header. */
int utun_write(utun_t *tun, const uint8_t *buf, size_t len);

/* Configure the utun interface with IP address and bring it up.
   local_ip: our IP address (e.g., "192.168.42.100")
   remote_ip: peer/gateway IP (e.g., "192.168.42.129")
   netmask: subnet mask (e.g., "255.255.255.0") */
int utun_configure(utun_t *tun, const char *local_ip, const char *remote_ip, const char *netmask);

/* Set up default route through the utun interface. */
int utun_set_default_route(utun_t *tun, const char *gateway);

/* Register as a macOS network service in configd for proper DNS integration.
   Uses VPN-style SupplementalMatchDomains to get DNS priority. */
int utun_register_service(utun_t *tun, const char *ip, const char *gateway,
                          const char *netmask, const char *dns1, const char *dns2);

/* Unregister the network service from configd. */
void utun_unregister_service(void);

/* Set DNS servers. Saves original DNS config for later restoration. */
int utun_set_dns(const char *dns1, const char *dns2);

/* Restore original DNS settings that were saved by utun_set_dns. */
void utun_restore_dns(void);

/* Close and destroy the utun interface. */
void utun_close(utun_t *tun);

#endif /* UTUN_H */
