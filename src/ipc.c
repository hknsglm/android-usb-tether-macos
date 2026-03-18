#include "ipc.h"
#include "log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <poll.h>

#define TAG "ipc"
#define MAX_CLIENTS 4
#define IPC_BUF_SIZE 1024

struct ipc_server {
    int listen_fd;
    int client_fds[MAX_CLIENTS];
    int num_clients;
};

static void set_nonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags >= 0)
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

ipc_server_t *ipc_server_create(void)
{
    ipc_server_t *srv = calloc(1, sizeof(ipc_server_t));
    if (!srv) return NULL;

    for (int i = 0; i < MAX_CLIENTS; i++)
        srv->client_fds[i] = -1;

    /* Remove stale socket */
    unlink(IPC_SOCK_PATH);

    srv->listen_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (srv->listen_fd < 0) {
        LOG_W(TAG, "failed to create socket: %s", strerror(errno));
        free(srv);
        return NULL;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strlcpy(addr.sun_path, IPC_SOCK_PATH, sizeof(addr.sun_path));

    if (bind(srv->listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        LOG_W(TAG, "failed to bind socket: %s", strerror(errno));
        close(srv->listen_fd);
        free(srv);
        return NULL;
    }

    /* Make socket accessible to the console user.
       Try SUDO_UID first (manual sudo), then SCDynamicStoreCopyConsoleUser,
       then fall back to world-writable (safe for local Unix socket). */
    const char *sudo_uid = getenv("SUDO_UID");
    const char *sudo_gid = getenv("SUDO_GID");
    if (sudo_uid && sudo_gid) {
        uid_t uid = (uid_t)strtol(sudo_uid, NULL, 10);
        gid_t gid = (gid_t)strtol(sudo_gid, NULL, 10);
        chown(IPC_SOCK_PATH, uid, gid);
        chmod(IPC_SOCK_PATH, 0660);
    } else {
        /* LaunchDaemon context: no SUDO_UID. Use 0666 which is safe
           for a local Unix socket on a single-user workstation. */
        chmod(IPC_SOCK_PATH, 0666);
    }

    if (listen(srv->listen_fd, 2) < 0) {
        LOG_W(TAG, "failed to listen: %s", strerror(errno));
        close(srv->listen_fd);
        unlink(IPC_SOCK_PATH);
        free(srv);
        return NULL;
    }

    set_nonblocking(srv->listen_fd);

    LOG_I(TAG, "IPC server listening on %s", IPC_SOCK_PATH);
    return srv;
}

static void send_to_client(int fd, const char *json)
{
    size_t len = strlen(json);
    /* Write the JSON line + newline */
    ssize_t n = write(fd, json, len);
    if (n > 0 && json[len - 1] != '\n') {
        write(fd, "\n", 1);
    }
}

static void send_to_all(ipc_server_t *srv, const char *json)
{
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (srv->client_fds[i] >= 0) {
            send_to_client(srv->client_fds[i], json);
        }
    }
}

static void close_client(ipc_server_t *srv, int idx)
{
    if (srv->client_fds[idx] >= 0) {
        close(srv->client_fds[idx]);
        srv->client_fds[idx] = -1;
        srv->num_clients--;
        LOG_D(TAG, "client disconnected (slot %d)", idx);
    }
}

int ipc_server_poll(ipc_server_t *srv)
{
    if (!srv) return 0;

    int stop_requested = 0;

    /* Accept new connections */
    int client_fd = accept(srv->listen_fd, NULL, NULL);
    if (client_fd >= 0) {
        set_nonblocking(client_fd);

        int slot = -1;
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (srv->client_fds[i] < 0) {
                slot = i;
                break;
            }
        }

        if (slot >= 0) {
            srv->client_fds[slot] = client_fd;
            srv->num_clients++;
            LOG_D(TAG, "client connected (slot %d)", slot);
        } else {
            /* Too many clients */
            close(client_fd);
        }
    }

    /* Read commands from connected clients */
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (srv->client_fds[i] < 0) continue;

        char buf[IPC_BUF_SIZE];
        ssize_t n = read(srv->client_fds[i], buf, sizeof(buf) - 1);
        if (n > 0) {
            buf[n] = '\0';
            if (strstr(buf, "\"stop\""))
                stop_requested = IPC_CMD_STOP;
            else if (strstr(buf, "\"disable\""))
                stop_requested = IPC_CMD_DISABLE;
            else if (strstr(buf, "\"enable\""))
                stop_requested = IPC_CMD_ENABLE;
        } else if (n == 0) {
            /* Client disconnected */
            close_client(srv, i);
        } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
            close_client(srv, i);
        }
    }

    return stop_requested;
}

void ipc_server_send_stats(ipc_server_t *srv, const tether_stats_t *stats)
{
    if (!srv || srv->num_clients == 0) return;

    char json[512];
    snprintf(json, sizeof(json),
        "{\"type\":\"stats\","
        "\"tx_mbps\":%.2f,\"rx_mbps\":%.2f,"
        "\"tx_bytes\":%llu,\"rx_bytes\":%llu,"
        "\"tx_pkts\":%llu,\"rx_pkts\":%llu}\n",
        stats->tx_mbps, stats->rx_mbps,
        (unsigned long long)stats->tx_bytes,
        (unsigned long long)stats->rx_bytes,
        (unsigned long long)stats->tx_pkts,
        (unsigned long long)stats->rx_pkts);

    send_to_all(srv, json);
}

void ipc_server_send_state(ipc_server_t *srv, const char *state,
                           const char *ip, const char *iface)
{
    if (!srv) return;

    char json[256];
    if (ip && iface) {
        snprintf(json, sizeof(json),
            "{\"type\":\"state\",\"state\":\"%s\",\"ip\":\"%s\",\"iface\":\"%s\"}\n",
            state, ip, iface);
    } else {
        snprintf(json, sizeof(json),
            "{\"type\":\"state\",\"state\":\"%s\"}\n", state);
    }

    send_to_all(srv, json);
}

void ipc_server_destroy(ipc_server_t *srv)
{
    if (!srv) return;

    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (srv->client_fds[i] >= 0)
            close(srv->client_fds[i]);
    }

    if (srv->listen_fd >= 0)
        close(srv->listen_fd);

    unlink(IPC_SOCK_PATH);
    free(srv);

    LOG_I(TAG, "IPC server stopped");
}
