#include "utun.h"
#include "log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/kern_control.h>
#include <sys/sys_domain.h>
#include <net/if_utun.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define TAG "utun"

int utun_create(utun_t *tun)
{
    memset(tun, 0, sizeof(*tun));

    int fd = socket(PF_SYSTEM, SOCK_DGRAM, SYSPROTO_CONTROL);
    if (fd < 0) {
        LOG_E(TAG, "failed to create control socket: %s", strerror(errno));
        return -1;
    }

    struct ctl_info ci;
    memset(&ci, 0, sizeof(ci));
    strlcpy(ci.ctl_name, UTUN_CONTROL_NAME, sizeof(ci.ctl_name));

    if (ioctl(fd, CTLIOCGINFO, &ci) == -1) {
        LOG_E(TAG, "failed to get utun control info: %s", strerror(errno));
        close(fd);
        return -1;
    }

    struct sockaddr_ctl sc;
    memset(&sc, 0, sizeof(sc));
    sc.sc_id = ci.ctl_id;
    sc.sc_len = sizeof(sc);
    sc.sc_family = AF_SYSTEM;
    sc.ss_sysaddr = AF_SYS_CONTROL;

    int connected = 0;
    for (int unit = 0; unit < 256; unit++) {
        sc.sc_unit = unit + 1;
        if (connect(fd, (struct sockaddr *)&sc, sizeof(sc)) == 0) {
            tun->unit = unit;
            connected = 1;
            break;
        }
    }

    if (!connected) {
        LOG_E(TAG, "failed to connect to any utun unit");
        close(fd);
        return -1;
    }

    tun->fd = fd;
    snprintf(tun->ifname, sizeof(tun->ifname), "utun%d", tun->unit);

    /* Increase socket buffer sizes for high throughput */
    int bufsize = 4 * 1024 * 1024; /* 4MB */
    setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &bufsize, sizeof(bufsize));
    setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &bufsize, sizeof(bufsize));

    /* Set non-blocking so utun_write() never stalls the USB read thread */
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags >= 0)
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    LOG_I(TAG, "created interface %s", tun->ifname);
    return 0;
}

int utun_read(utun_t *tun, uint8_t *buf, size_t buf_size)
{
    ssize_t n = read(tun->fd, buf, buf_size);
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return 0;
        LOG_E(TAG, "read error: %s", strerror(errno));
        return -1;
    }
    return (int)n;
}

int utun_write(utun_t *tun, const uint8_t *buf, size_t len)
{
    ssize_t n = write(tun->fd, buf, len);
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return 0;
        LOG_E(TAG, "write error: %s", strerror(errno));
        return -1;
    }
    return (int)n;
}

int utun_configure(utun_t *tun, const char *local_ip, const char *remote_ip,
                   const char *netmask)
{
    char cmd[512];

    snprintf(cmd, sizeof(cmd), "ifconfig %s inet %s %s netmask %s up",
             tun->ifname, local_ip, remote_ip, netmask);
    LOG_I(TAG, "%s", cmd);
    int ret = system(cmd);
    if (ret != 0) {
        LOG_E(TAG, "failed to configure interface (exit %d)", ret);
        return -1;
    }

    snprintf(cmd, sizeof(cmd), "ifconfig %s mtu %d", tun->ifname, UTUN_MTU);
    LOG_I(TAG, "%s", cmd);
    system(cmd);

    return 0;
}

int utun_set_default_route(utun_t *tun, const char *gateway)
{
    (void)tun;
    char cmd[256];

    snprintf(cmd, sizeof(cmd), "route add -net 0.0.0.0/1 %s", gateway);
    LOG_I(TAG, "%s", cmd);
    int ret = system(cmd);

    snprintf(cmd, sizeof(cmd), "route add -net 128.0.0.0/1 %s", gateway);
    LOG_I(TAG, "%s", cmd);
    ret |= system(cmd);

    return ret == 0 ? 0 : -1;
}

/* configd service ID for our network service */
#define SERVICE_ID "android-tether"

/* Path to save/restore original DNS config */
#define DNS_BACKUP_FILE "/tmp/.android-tether-dns-backup"

int utun_register_service(utun_t *tun, const char *ip, const char *gateway,
                          const char *netmask, const char *dns1, const char *dns2)
{
    char cmd[2048];

    snprintf(cmd, sizeof(cmd),
        "scutil <<'EOF'\n"
        "d.init\n"
        "d.add Addresses * %s\n"
        "d.add SubnetMasks * %s\n"
        "d.add Router %s\n"
        "d.add InterfaceName %s\n"
        "set State:/Network/Service/" SERVICE_ID "/IPv4\n"
        "quit\n"
        "EOF", ip, netmask, gateway, tun->ifname);
    system(cmd);

    snprintf(cmd, sizeof(cmd),
        "scutil <<'EOF'\n"
        "d.init\n"
        "d.add DeviceName %s\n"
        "d.add Type utun\n"
        "set State:/Network/Service/" SERVICE_ID "/Interface\n"
        "quit\n"
        "EOF", tun->ifname);
    system(cmd);

    snprintf(cmd, sizeof(cmd),
        "scutil <<'EOF'\n"
        "d.init\n"
        "d.add ServerAddresses * %s%s%s\n"
        "d.add SupplementalMatchDomains * \"\"\n"
        "d.add SearchOrder # 1\n"
        "set State:/Network/Service/" SERVICE_ID "/DNS\n"
        "quit\n"
        "EOF",
        dns1,
        dns2 ? " " : "",
        dns2 ? dns2 : "");
    system(cmd);

    LOG_I("net", "registered network service '%s' on %s", SERVICE_ID, tun->ifname);
    return 0;
}

void utun_unregister_service(void)
{
    system(
        "scutil <<'EOF' 2>/dev/null\n"
        "remove State:/Network/Service/" SERVICE_ID "/IPv4\n"
        "remove State:/Network/Service/" SERVICE_ID "/DNS\n"
        "remove State:/Network/Service/" SERVICE_ID "/Interface\n"
        "quit\n"
        "EOF");
    LOG_I("net", "unregistered network service");
}

int utun_set_dns(const char *dns1, const char *dns2)
{
    char cmd[1024];

    snprintf(cmd, sizeof(cmd),
        "scutil <<'EOF' > " DNS_BACKUP_FILE " 2>/dev/null\n"
        "show State:/Network/Global/DNS\n"
        "quit\n"
        "EOF");
    system(cmd);

    snprintf(cmd, sizeof(cmd),
        "scutil <<'EOF'\n"
        "d.init\n"
        "d.add ServerAddresses * %s%s%s\n"
        "set State:/Network/Global/DNS\n"
        "quit\n"
        "EOF",
        dns1,
        dns2 ? " " : "",
        dns2 ? dns2 : "");

    LOG_I("dns", "overriding global DNS to %s %s", dns1, dns2 ? dns2 : "");
    int ret = system(cmd);
    if (ret != 0) {
        LOG_W("dns", "scutil returned %d, trying fallback", ret);
    }

    system("dscacheutil -flushcache 2>/dev/null");
    system("killall -HUP mDNSResponder 2>/dev/null");

    LOG_I("dns", "DNS configured and cache flushed");
    return 0;
}

void utun_restore_dns(void)
{
    LOG_I("dns", "restoring original DNS settings...");

    system(
        "scutil <<'EOF' 2>/dev/null\n"
        "remove State:/Network/Global/DNS\n"
        "quit\n"
        "EOF");

    system("dscacheutil -flushcache 2>/dev/null");
    system("killall -HUP mDNSResponder 2>/dev/null");

    unlink(DNS_BACKUP_FILE);
    LOG_I("dns", "original DNS restored");
}

void utun_close(utun_t *tun)
{
    if (tun->fd >= 0) {
        close(tun->fd);
        tun->fd = -1;
        LOG_I(TAG, "closed interface %s", tun->ifname);
    }
}
