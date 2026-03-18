#include "rndis.h"
#include "usb_device.h"
#include "utun.h"
#include "log.h"
#include "net_types.h"
#include "dhcp.h"
#include "arp.h"
#include "frame.h"
#include "stats.h"
#include "proto_driver.h"
#include "config.h"
#include "ipc.h"
#include "compat.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <poll.h>
#include <pthread.h>
#include <stdatomic.h>
#include <netinet/in.h>
#include <arpa/inet.h>

/* Max consecutive USB errors before giving up */
#define MAX_USB_ERRORS  20

static volatile int g_running = 1;
static int g_no_route = 0;
static int g_no_dns = 0;
static int g_network_bound = 0;
static pid_t g_ui_pid = 0;
static int g_watch_mode = 0;
static volatile int g_watch_enabled = 1;

static void restore_network_state(void)
{
    if (!g_network_bound) return;

    LOG_I("net", "restoring network state...");
    if (!g_no_route) {
        system("route delete -net 0.0.0.0/1 2>/dev/null");
        system("route delete -net 128.0.0.0/1 2>/dev/null");
    }
    utun_unregister_service();
    if (!g_no_dns) {
        utun_restore_dns();
    }
    g_network_bound = 0;
}

static void cleanup_at_exit(void)
{
    LOG_I("main", "performing emergency cleanup...");
    restore_network_state();
    stats_cleanup();
}

static void signal_handler(int sig)
{
    (void)sig;
    log_signal_safe("\n[INFO] [main] received signal, shutting down cleanly...\n");
    g_running = 0;
}


static void print_usage(const char *prog)
{
    fprintf(stderr,
        "Usage: %s [options]\n"
        "\n"
        "Android USB Tethering for macOS (RNDIS)\n"
        "\n"
        "Options:\n"
        "  -n, --no-route      Don't set up default route (manual configuration)\n"
        "  -d, --no-dns        Don't modify DNS settings\n"
        "  -s, --static IP     Use static IP instead of DHCP\n"
        "  -g, --gateway IP    Gateway IP (with --static)\n"
        "  -m, --netmask IP    Netmask (default: 255.255.255.0)\n"
        "  -c, --config FILE   Load config from custom file\n"
        "  -v, --verbose       Verbose / debug-level logging\n"
        "  -w, --watch         Watch mode: auto-connect when device detected\n"
        "  -V, --version       Show version and exit\n"
        "  -h, --help          Show this help\n"
#ifdef VERSION
        "\nVersion: %s\n"
#endif
        "\nThis tool must be run as root (sudo).\n"
        "Make sure USB tethering is enabled on your Android device.\n"
        "Config file: ~/.config/android-tether/config\n",
        prog
#ifdef VERSION
        , VERSION
#endif
    );
}

/* ---- Async Bridge ---- */

/* Number of async USB transfers to keep in flight */
#define NUM_RX_XFERS 16
#define NUM_TX_XFERS 64

typedef struct bridge_ctx bridge_ctx_t;

typedef struct {
    struct libusb_transfer *xfer;
    uint8_t *buf;
    bridge_ctx_t *ctx;
} rx_xfer_t;

typedef struct {
    struct libusb_transfer *xfer;
    uint8_t *buf;
    bridge_ctx_t *ctx;
    int in_use;
} tx_xfer_t;

struct bridge_ctx {
    usb_device_t *usb;
    utun_t *tun;
    proto_driver_t *drv;
    uint8_t mac[6];
    uint8_t gw_mac[6];
    atomic_int gw_mac_learned;
    uint32_t our_ip_n;
    atomic_int running;
    atomic_int disconnected;
    atomic_uint_fast64_t rx_pkts, rx_bytes;
    atomic_uint_fast64_t tx_pkts, tx_bytes;

    rx_xfer_t rx_pool[NUM_RX_XFERS];
    atomic_int active_rx;

    tx_xfer_t tx_pool[NUM_TX_XFERS];
    pthread_mutex_t tx_mutex;
    pthread_cond_t tx_cond;
    atomic_int active_tx;
};

/* Completion callback for async ARP reply sends */
static void LIBUSB_CALL arp_tx_complete(struct libusb_transfer *xfer)
{
    free(xfer->buffer);
    libusb_free_transfer(xfer);
}

/* Process a single received Ethernet frame from USB */
static void process_rx_frame(bridge_ctx_t *ctx, const uint8_t *frame, size_t frame_len)
{
    /* Learn gateway MAC from first incoming packet.
       memcpy of gw_mac is safe here: it is written once (before gw_mac_learned
       is set) and only read by the TX thread after gw_mac_learned == 1. The
       atomic_store with release ordering ensures the MAC bytes are visible. */
    if (!atomic_load(&ctx->gw_mac_learned) && frame_len >= sizeof(eth_hdr_t)) {
        const eth_hdr_t *eth = (const eth_hdr_t *)frame;
        if (eth->src[0] != 0xFF) {
            memcpy(ctx->gw_mac, eth->src, 6);
            atomic_store_explicit(&ctx->gw_mac_learned, 1, memory_order_release);
            LOG_I("bridge", "learned gateway MAC: %02x:%02x:%02x:%02x:%02x:%02x",
                  ctx->gw_mac[0], ctx->gw_mac[1], ctx->gw_mac[2],
                  ctx->gw_mac[3], ctx->gw_mac[4], ctx->gw_mac[5]);
        }
    }

    /* Handle ARP requests */
    if (frame_len >= sizeof(eth_hdr_t)) {
        const eth_hdr_t *eth = (const eth_hdr_t *)frame;
        if (ntohs(eth->ethertype) == ARP_ETHERTYPE) {
            uint8_t abuf[ETH_BUF_SIZE];
            int arp_len = arp_handle_request(frame, frame_len, abuf, sizeof(abuf),
                                             ctx->mac, ctx->our_ip_n);
            if (arp_len > 0) {
                uint8_t *txbuf = malloc(RNDIS_BUF_SIZE);
                if (txbuf) {
                    int rlen = ctx->drv->wrap_frame(ctx->drv, abuf, arp_len,
                                                    txbuf, RNDIS_BUF_SIZE);
                    if (rlen > 0) {
                        struct libusb_transfer *tx = libusb_alloc_transfer(0);
                        if (tx) {
                            libusb_fill_bulk_transfer(tx, ctx->usb->handle,
                                                     ctx->usb->ep_out,
                                                     txbuf, rlen,
                                                     arp_tx_complete, NULL, 1000);
                            if (libusb_submit_transfer(tx) != 0) {
                                libusb_free_transfer(tx);
                                free(txbuf);
                            }
                        } else {
                            free(txbuf);
                        }
                    } else {
                        free(txbuf);
                    }
                }
            }
            return;
        }
    }

    /* Convert Ethernet frame to utun packet and inject into macOS stack */
    uint8_t tbuf[ETH_BUF_SIZE + 256];
    int tlen = frame_eth_to_utun(frame, frame_len, tbuf, sizeof(tbuf));
    if (tlen > 0) {
        utun_write(ctx->tun, tbuf, tlen);
        atomic_fetch_add(&ctx->rx_pkts, 1);
        atomic_fetch_add(&ctx->rx_bytes, (uint_fast64_t)frame_len);
    }
}

/* Callback for proto_driver unwrap_data */
static void rx_frame_callback(const uint8_t *frame, size_t len, void *user_ctx)
{
    bridge_ctx_t *ctx = (bridge_ctx_t *)user_ctx;
    process_rx_frame(ctx, frame, len);
}

/* Async RX completion callback */
static void LIBUSB_CALL rx_complete(struct libusb_transfer *xfer)
{
    rx_xfer_t *rx = (rx_xfer_t *)xfer->user_data;
    bridge_ctx_t *ctx = rx->ctx;

    switch (xfer->status) {
    case LIBUSB_TRANSFER_COMPLETED:
        if (xfer->actual_length > 0) {
            ctx->drv->unwrap_data(ctx->drv, rx->buf, xfer->actual_length,
                                  rx_frame_callback, ctx);
        }
        break;
    case LIBUSB_TRANSFER_NO_DEVICE:
        LOG_E("bridge", "USB device disconnected");
        atomic_store(&ctx->disconnected, 1);
        atomic_store(&ctx->running, 0);
        atomic_fetch_sub(&ctx->active_rx, 1);
        return;
    case LIBUSB_TRANSFER_CANCELLED:
        atomic_fetch_sub(&ctx->active_rx, 1);
        return;
    case LIBUSB_TRANSFER_STALL:
        libusb_clear_halt(ctx->usb->handle, ctx->usb->ep_in);
        break;
    case LIBUSB_TRANSFER_TIMED_OUT:
    case LIBUSB_TRANSFER_ERROR:
    case LIBUSB_TRANSFER_OVERFLOW:
        break;
    }

    /* Resubmit transfer to keep the pipeline full */
    if (atomic_load(&ctx->running) && g_running) {
        if (libusb_submit_transfer(xfer) == 0)
            return;
    }
    atomic_fetch_sub(&ctx->active_rx, 1);
}

/* Thread: USB -> utun (Android -> Mac) */
static void *usb_to_tun(void *arg)
{
    bridge_ctx_t *ctx = (bridge_ctx_t *)arg;

    for (int i = 0; i < NUM_RX_XFERS; i++) {
        rx_xfer_t *rx = &ctx->rx_pool[i];
        rx->buf = malloc(RNDIS_BUF_SIZE);
        rx->ctx = ctx;
        rx->xfer = libusb_alloc_transfer(0);
        if (!rx->buf || !rx->xfer) {
            LOG_E("bridge", "failed to allocate RX transfer %d", i);
            free(rx->buf); rx->buf = NULL;
            if (rx->xfer) { libusb_free_transfer(rx->xfer); rx->xfer = NULL; }
            continue;
        }
        libusb_fill_bulk_transfer(rx->xfer, ctx->usb->handle, ctx->usb->ep_in,
                                  rx->buf, RNDIS_BUF_SIZE, rx_complete, rx, 0);
        if (libusb_submit_transfer(rx->xfer) == 0) {
            atomic_fetch_add(&ctx->active_rx, 1);
        } else {
            LOG_E("bridge", "failed to submit RX transfer %d", i);
        }
    }

    LOG_I("bridge", "%d async RX transfers in flight", atomic_load(&ctx->active_rx));

    while (atomic_load(&ctx->running) && g_running && atomic_load(&ctx->active_rx) > 0) {
        struct timeval tv = { 0, 250000 };
        int ret = libusb_handle_events_timeout(ctx->usb->ctx, &tv);
        if (ret < 0 && ret != LIBUSB_ERROR_TIMEOUT && ret != LIBUSB_ERROR_INTERRUPTED) {
            LOG_E("bridge", "libusb event error: %s", libusb_strerror(ret));
            if (ret == LIBUSB_ERROR_NO_DEVICE) {
                atomic_store(&ctx->disconnected, 1);
                atomic_store(&ctx->running, 0);
                break;
            }
        }
    }

    /* If we drop out of the loop due to a USB event read error, tear down the bridge natively */
    atomic_store(&ctx->running, 0);

    /* Cancel all in-flight RX transfers */
    for (int i = 0; i < NUM_RX_XFERS; i++) {
        if (ctx->rx_pool[i].xfer)
            libusb_cancel_transfer(ctx->rx_pool[i].xfer);
    }

    /* Cancel all in-flight TX transfers */
    pthread_mutex_lock(&ctx->tx_mutex);
    for (int i = 0; i < NUM_TX_XFERS; i++) {
        if (ctx->tx_pool[i].in_use && ctx->tx_pool[i].xfer)
            libusb_cancel_transfer(ctx->tx_pool[i].xfer);
    }
    pthread_mutex_unlock(&ctx->tx_mutex);

    /* Drain cancellation callbacks */
    time_t drain_start = time(NULL);
    while ((atomic_load(&ctx->active_rx) > 0 || atomic_load(&ctx->active_tx) > 0) && (time(NULL) - drain_start < 2)) {
        struct timeval tv = { 0, 100000 };
        libusb_handle_events_timeout(ctx->usb->ctx, &tv);
    }

    for (int i = 0; i < NUM_RX_XFERS; i++) {
        if (ctx->rx_pool[i].xfer)
            libusb_free_transfer(ctx->rx_pool[i].xfer);
        free(ctx->rx_pool[i].buf);
    }

    return NULL;
}

/* Async TX completion callback */
static void LIBUSB_CALL tx_complete(struct libusb_transfer *xfer)
{
    tx_xfer_t *tx = (tx_xfer_t *)xfer->user_data;
    bridge_ctx_t *ctx = tx->ctx;

    if (xfer->status == LIBUSB_TRANSFER_NO_DEVICE) {
        atomic_store(&ctx->disconnected, 1);
        atomic_store(&ctx->running, 0);
    } else if (xfer->status == LIBUSB_TRANSFER_STALL) {
        libusb_clear_halt(ctx->usb->handle, ctx->usb->ep_out);
    }

    pthread_mutex_lock(&ctx->tx_mutex);
    tx->in_use = 0;
    atomic_fetch_sub(&ctx->active_tx, 1);
    pthread_cond_signal(&ctx->tx_cond);
    pthread_mutex_unlock(&ctx->tx_mutex);
}

/* Thread: utun -> USB (Mac -> Android) */
static void *tun_to_usb(void *arg)
{
    bridge_ctx_t *ctx = (bridge_ctx_t *)arg;
    uint8_t tbuf[ETH_BUF_SIZE + 256];
    uint8_t ebuf[ETH_BUF_SIZE];

    struct pollfd pfd;
    pfd.fd = ctx->tun->fd;
    pfd.events = POLLIN;

    while (atomic_load(&ctx->running) && g_running) {
        int pret = poll(&pfd, 1, 100);
        if (pret <= 0 || !(pfd.revents & POLLIN))
            continue;

        for (int batch = 0; batch < 64; batch++) {
            if (batch > 0) {
                pret = poll(&pfd, 1, 0);
                if (pret <= 0 || !(pfd.revents & POLLIN))
                    break;
            }

            int tlen = utun_read(ctx->tun, tbuf, sizeof(tbuf));
            if (tlen <= 0)
                break;

            int elen = frame_ip_to_eth(tbuf, tlen, ebuf, sizeof(ebuf),
                                       ctx->mac, ctx->gw_mac);
            if (elen <= 0) {
                LOG_D("bridge", "dropped outbound packet (len=%d, frame_ip_to_eth=%d)", tlen, elen);
            }
            if (elen > 0) {
                tx_xfer_t *tx = NULL;
                pthread_mutex_lock(&ctx->tx_mutex);
                while (atomic_load(&ctx->running) && g_running) {
                    for (int i = 0; i < NUM_TX_XFERS; i++) {
                        if (!ctx->tx_pool[i].in_use) {
                            tx = &ctx->tx_pool[i];
                            tx->in_use = 1;
                            break;
                        }
                    }
                    if (tx) break;

                    struct timespec ts;
                    clock_gettime(CLOCK_REALTIME, &ts);
                    ts.tv_nsec += 100000000;
                    if (ts.tv_nsec >= 1000000000) {
                        ts.tv_sec++;
                        ts.tv_nsec -= 1000000000;
                    }
                    pthread_cond_timedwait(&ctx->tx_cond, &ctx->tx_mutex, &ts);
                }
                pthread_mutex_unlock(&ctx->tx_mutex);

                if (!tx) break;

                int rlen = ctx->drv->wrap_frame(ctx->drv, ebuf, elen, tx->buf, RNDIS_BUF_SIZE);
                if (rlen > 0) {
                    libusb_fill_bulk_transfer(tx->xfer, ctx->usb->handle, ctx->usb->ep_out,
                                              tx->buf, rlen, tx_complete, tx, 1000);

                    if (libusb_submit_transfer(tx->xfer) == 0) {
                        atomic_fetch_add(&ctx->active_tx, 1);

                        atomic_fetch_add(&ctx->tx_pkts, 1);
                        atomic_fetch_add(&ctx->tx_bytes, (uint_fast64_t)elen);
                    } else {
                        pthread_mutex_lock(&ctx->tx_mutex);
                        tx->in_use = 0;
                        pthread_mutex_unlock(&ctx->tx_mutex);
                    }
                } else {
                    pthread_mutex_lock(&ctx->tx_mutex);
                    tx->in_use = 0;
                    pthread_mutex_unlock(&ctx->tx_mutex);
                }
            }
        }
    }
    return NULL;
}

/* Run one tethering session: find device, init, DHCP, bridge.
   Returns 0 if user requested stop, 1 if should reconnect. */
static int run_session(int no_route, int no_dns,
                       const char *static_ip_arg, const char *static_gw_arg,
                       const char *static_mask_arg,
                       ipc_server_t *ipc)
{
    /* Step 1: Create protocol driver */
    proto_driver_t *drv = proto_rndis_create();
    if (!drv || proto_driver_validate(drv) != 0) {
        LOG_E("main", "failed to create protocol driver");
        if (drv) drv->destroy(drv);
        return g_running ? 1 : 0;
    }

    /* Step 2: Find and open the USB device */
    LOG_I("main", "looking for Android %s device...", drv->name);
    usb_device_t usb;
    if (usb_find_rndis_device(&usb) < 0) {
        drv->destroy(drv);
        return g_running ? 1 : 0;
    }

    /* Step 3: Initialize protocol */
    LOG_I("main", "initializing %s...", drv->name);

    if (drv->init(drv, &usb) < 0) {
        usb_close_device(&usb);
        drv->destroy(drv);
        return g_running ? 1 : 0;
    }

    /* Get MAC address from driver */
    uint8_t device_mac[6];
    if (drv->get_mac(drv, device_mac) != 0) {
        LOG_E("main", "failed to get MAC address from driver");
        usb_close_device(&usb);
        drv->destroy(drv);
        return g_running ? 1 : 0;
    }

    /* Step 4: Get IP configuration (DHCP or static) */
    char ip[16], gateway[16], netmask[16], dns1[16], dns2[16];

    if (static_ip_arg && static_ip_arg[0]) {
        strlcpy(ip, static_ip_arg, sizeof(ip));
        strlcpy(gateway, (static_gw_arg && static_gw_arg[0]) ? static_gw_arg : DEFAULT_GATEWAY,
                sizeof(gateway));
        strlcpy(netmask, static_mask_arg, sizeof(netmask));
        strlcpy(dns1, DEFAULT_DNS1, sizeof(dns1));
        strlcpy(dns2, DEFAULT_DNS2, sizeof(dns2));
        LOG_I("main", "using static IP: %s", ip);
    } else {
        LOG_I("main", "performing DHCP...");
        /* DHCP still uses RNDIS state directly for now - the RNDIS driver
           stores its state in drv->priv which is a rndis_state_t */
        rndis_state_t *rndis = (rndis_state_t *)drv->priv;
        dhcp_lease_t lease;
        memset(&lease, 0, sizeof(lease));
        if (dhcp_discover(&usb, rndis, &lease) < 0) {
            LOG_W("main", "DHCP failed, using default static config");
            strlcpy(ip, DEFAULT_STATIC_IP, sizeof(ip));
            strlcpy(gateway, DEFAULT_GATEWAY, sizeof(gateway));
            strlcpy(netmask, DEFAULT_NETMASK, sizeof(netmask));
            strlcpy(dns1, DEFAULT_DNS1, sizeof(dns1));
            strlcpy(dns2, DEFAULT_DNS2, sizeof(dns2));
        } else {
            strlcpy(ip, lease.ip, sizeof(ip));
            strlcpy(gateway, lease.gateway, sizeof(gateway));
            strlcpy(netmask, lease.netmask, sizeof(netmask));
            strlcpy(dns1, lease.dns1, sizeof(dns1));
            strlcpy(dns2, lease.dns2, sizeof(dns2));
        }
    }

    uint32_t our_ip_n;
    if (inet_pton(AF_INET, ip, &our_ip_n) != 1) {
        LOG_E("main", "invalid IP address: %s", ip);
        usb_close_device(&usb);
        drv->destroy(drv);
        return g_running ? 1 : 0;
    }

    /* Step 5: Create utun interface */
    LOG_I("main", "creating network interface...");
    utun_t tun;
    if (utun_create(&tun) < 0) {
        usb_close_device(&usb);
        drv->destroy(drv);
        return g_running ? 1 : 0;
    }

    /* Step 6: Configure the interface */
    if (utun_configure(&tun, ip, gateway, netmask) < 0) {
        utun_close(&tun);
        usb_close_device(&usb);
        drv->destroy(drv);
        return g_running ? 1 : 0;
    }

    /* Step 7: Register network service */
    if (!no_dns) {
        LOG_I("main", "setting DNS servers...");
        utun_set_dns(dns1, dns2);
        utun_register_service(&tun, ip, gateway, netmask, dns1, dns2);
    } else if (!no_route) {
        utun_register_service(&tun, ip, gateway, netmask, "", "");
    }

    /* Wait for configd to process interface registration before routing */
    usleep(500000);

    /* Step 8: Set up routing */
    if (!no_route) {
        LOG_I("main", "setting up routing...");
        utun_set_default_route(&tun, gateway);
    }

    g_network_bound = 1;

    /* Send gratuitous ARP */
    arp_send_gratuitous(&usb, device_mac, our_ip_n);

    LOG_I("main", "tethering active on %s (%s)", tun.ifname, ip);
    LOG_I("main", "gateway: %s, DNS: %s, %s", gateway, dns1, dns2);
    LOG_I("main", "press Ctrl+C to stop");

    if (ipc)
        ipc_server_send_state(ipc, "connected", ip, tun.ifname);

    /* Step 9: Start threaded packet bridge */
    bridge_ctx_t bctx;
    memset(&bctx, 0, sizeof(bctx));
    bctx.usb = &usb;
    bctx.tun = &tun;
    bctx.drv = drv;
    memcpy(bctx.mac, device_mac, 6);
    memset(bctx.gw_mac, 0xFF, 6);
    bctx.our_ip_n = our_ip_n;
    atomic_store(&bctx.running, 1);

    pthread_mutex_init(&bctx.tx_mutex, NULL);
    pthread_cond_init(&bctx.tx_cond, NULL);
    for (int i = 0; i < NUM_TX_XFERS; i++) {
        bctx.tx_pool[i].xfer = libusb_alloc_transfer(0);
        bctx.tx_pool[i].buf = malloc(RNDIS_BUF_SIZE);
        bctx.tx_pool[i].ctx = &bctx;
        bctx.tx_pool[i].in_use = 0;
    }

    pthread_t t_rx, t_tx;
    pthread_create(&t_rx, NULL, usb_to_tun, &bctx);
    pthread_create(&t_tx, NULL, tun_to_usb, &bctx);

    /* Main thread: keepalive and stats */
    time_t last_keepalive = 0;
    time_t last_stats = 0;
    uint64_t prev_tx_bytes = 0, prev_rx_bytes = 0;

    while (g_running && atomic_load(&bctx.running)) {
        time_t now = time(NULL);

        if (now - last_keepalive >= 30) {
            if (drv->keepalive)
                drv->keepalive(drv, &usb);
            last_keepalive = now;
        }

        if (now - last_stats >= 1) {
            time_t elapsed = now - last_stats;
            if (elapsed > 0 && last_stats > 0) {
                uint64_t cur_tx_bytes = atomic_load(&bctx.tx_bytes);
                uint64_t cur_rx_bytes = atomic_load(&bctx.rx_bytes);
                tether_stats_t st;
                st.tx_mbps = (double)(cur_tx_bytes - prev_tx_bytes) * 8.0
                             / (double)elapsed / 1e6;
                st.rx_mbps = (double)(cur_rx_bytes - prev_rx_bytes) * 8.0
                             / (double)elapsed / 1e6;
                st.tx_bytes = cur_tx_bytes;
                st.rx_bytes = cur_rx_bytes;
                st.tx_pkts = atomic_load(&bctx.tx_pkts);
                st.rx_pkts = atomic_load(&bctx.rx_pkts);

                LOG_I("speed", "TX: %.1f Mbps, RX: %.1f Mbps (total: %llu/%llu pkts)",
                      st.tx_mbps, st.rx_mbps,
                      (unsigned long long)st.tx_pkts,
                      (unsigned long long)st.rx_pkts);

                stats_write_json(&st);

                /* Send stats over IPC */
                if (ipc)
                    ipc_server_send_stats(ipc, &st);
            }
            prev_tx_bytes = atomic_load(&bctx.tx_bytes);
            prev_rx_bytes = atomic_load(&bctx.rx_bytes);
            last_stats = now;

            /* Check if UI application quit unexpectedly (only in non-watch mode) */
            if (!g_watch_mode && g_ui_pid > 0 && kill(g_ui_pid, 0) != 0) {
                LOG_I("main", "UI process %d died, shutting down daemon...", g_ui_pid);
                g_running = 0;
                atomic_store(&bctx.running, 0);
            }

            /* Check for stop trigger file (legacy) */
            if (stats_check_stop_trigger()) {
                LOG_I("main", "stop trigger detected, shutting down daemon...");
                g_running = 0;
                atomic_store(&bctx.running, 0);
            }

            /* Check for IPC commands */
            if (ipc) {
                int cmd = ipc_server_poll(ipc);
                if (cmd == IPC_CMD_STOP || cmd == IPC_CMD_DISABLE) {
                    LOG_I("main", "IPC %s received, ending session...",
                          cmd == IPC_CMD_STOP ? "stop" : "disable");
                    atomic_store(&bctx.running, 0);
                    if (cmd == IPC_CMD_DISABLE)
                        g_watch_enabled = 0;
                    if (!g_watch_mode)
                        g_running = 0;
                } else if (cmd == IPC_CMD_ENABLE) {
                    g_watch_enabled = 1;
                }
            }
        }

        usleep(250000);
    }

    /* Stop threads and wait */
    atomic_store(&bctx.running, 0);

    pthread_mutex_lock(&bctx.tx_mutex);
    pthread_cond_signal(&bctx.tx_cond);
    pthread_mutex_unlock(&bctx.tx_mutex);

    pthread_join(t_tx, NULL);
    pthread_join(t_rx, NULL);

    int disconnected = atomic_load(&bctx.disconnected);

    for (int i = 0; i < NUM_TX_XFERS; i++) {
        if (bctx.tx_pool[i].xfer)
            libusb_free_transfer(bctx.tx_pool[i].xfer);
        free(bctx.tx_pool[i].buf);
    }
    pthread_mutex_destroy(&bctx.tx_mutex);
    pthread_cond_destroy(&bctx.tx_cond);

    LOG_I("main", "session ended. TX: %llu pkts, RX: %llu pkts",
          (unsigned long long)atomic_load(&bctx.tx_pkts),
          (unsigned long long)atomic_load(&bctx.rx_pkts));

    if (ipc)
        ipc_server_send_state(ipc, "disconnected", NULL, NULL);

    restore_network_state();

    utun_close(&tun);
    usb_close_device(&usb);
    drv->destroy(drv);

    return (disconnected && g_running) ? 1 : 0;
}

int main(int argc, char **argv)
{
    /* Load configuration: defaults -> config file -> CLI args */
    tether_config_t cfg;
    config_init_defaults(&cfg);
    config_load_default(&cfg);

    int cli_ret = config_apply_cli(&cfg, argc, argv);
    if (cli_ret == 1) { /* --help */
        print_usage(argv[0]);
        return 0;
    }
    if (cli_ret < 0) {
        print_usage(argv[0]);
        return 1;
    }

    if (getuid() != 0) {
        fprintf(stderr, "error: this tool must be run as root (use sudo)\n");
        return 1;
    }

    /* Initialize logging */
    log_init(cfg.log_level);

    g_no_route = cfg.no_route;
    g_no_dns = cfg.no_dns;
    g_ui_pid = (pid_t)cfg.ui_pid;
    g_watch_mode = cfg.watch_mode;

    atexit(cleanup_at_exit);

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    macos_version_t ver = compat_macos_version();
    LOG_I("main", "=== Android USB Tethering for macOS ===");
#ifdef VERSION
    LOG_I("main", "version %s on %s %d.%d.%d%s", VERSION,
          compat_macos_name(), ver.major, ver.minor, ver.patch,
          g_watch_mode ? " (watch mode)" : "");
#else
    LOG_I("main", "running on %s %d.%d.%d%s",
          compat_macos_name(), ver.major, ver.minor, ver.patch,
          g_watch_mode ? " (watch mode)" : "");
#endif

    /* Create IPC server once (persists across sessions) */
    ipc_server_t *ipc = ipc_server_create();

    if (g_watch_mode) {
        /* Watch mode: run persistently, auto-connect when device appears */
        if (ipc)
            ipc_server_send_state(ipc, "watching", NULL, NULL);

        while (g_running) {
            /* Process IPC commands */
            if (ipc) {
                int cmd = ipc_server_poll(ipc);
                if (cmd == IPC_CMD_DISABLE) {
                    g_watch_enabled = 0;
                    ipc_server_send_state(ipc, "idle", NULL, NULL);
                    LOG_I("main", "auto-connect disabled");
                } else if (cmd == IPC_CMD_ENABLE) {
                    g_watch_enabled = 1;
                    ipc_server_send_state(ipc, "watching", NULL, NULL);
                    LOG_I("main", "auto-connect enabled");
                } else if (cmd == IPC_CMD_STOP) {
                    /* In watch mode idle, stop is a no-op */
                }
            }

            if (!g_watch_enabled) {
                usleep(500000);
                continue;
            }

            /* Try to run a tethering session */
            int result = run_session(cfg.no_route, cfg.no_dns,
                                     cfg.static_ip, cfg.gateway, cfg.netmask, ipc);

            if (result == 1 && g_running) {
                /* Device not found or disconnected — wait before retry */
                if (ipc && g_watch_enabled)
                    ipc_server_send_state(ipc, "watching", NULL, NULL);

                for (int i = 0; i < 6 && g_running && g_watch_enabled; i++) {
                    if (ipc) {
                        int cmd = ipc_server_poll(ipc);
                        if (cmd == IPC_CMD_DISABLE) {
                            g_watch_enabled = 0;
                            ipc_server_send_state(ipc, "idle", NULL, NULL);
                            LOG_I("main", "auto-connect disabled");
                        } else if (cmd == IPC_CMD_ENABLE) {
                            g_watch_enabled = 1;
                        }
                    }
                    usleep(500000);
                }
            }
        }
    } else {
        /* Legacy one-shot mode */
        while (g_running) {
            int result = run_session(cfg.no_route, cfg.no_dns,
                                     cfg.static_ip, cfg.gateway, cfg.netmask, ipc);
            if (result == 0)
                break;

            LOG_I("main", "waiting for device to reconnect (3s)...");
            for (int i = 0; i < 6 && g_running; i++) {
                if (g_ui_pid > 0 && kill(g_ui_pid, 0) != 0) {
                    LOG_I("main", "UI process %d died, shutting down daemon...", g_ui_pid);
                    g_running = 0;
                }
                if (stats_check_stop_trigger()) {
                    LOG_I("main", "stop trigger detected, shutting down daemon...");
                    g_running = 0;
                }
                if (ipc && ipc_server_poll(ipc) == IPC_CMD_STOP) {
                    g_running = 0;
                }
                usleep(500000);
            }
        }
    }

    if (ipc)
        ipc_server_destroy(ipc);

    LOG_I("main", "done.");
    return 0;
}
