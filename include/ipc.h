#ifndef IPC_H
#define IPC_H

#include "stats.h"

/*
 * Unix domain socket IPC between daemon and UI.
 *
 * Protocol: newline-delimited JSON over a Unix socket.
 *
 * Daemon -> UI messages:
 *   {"type":"stats","tx_mbps":1.2,"rx_mbps":5.3,...}
 *   {"type":"state","state":"connected","ip":"192.168.42.100","iface":"utun5"}
 *   {"type":"state","state":"disconnected"}
 *   {"type":"state","state":"connecting"}
 *
 * UI -> Daemon messages:
 *   {"type":"stop"}       - stop current session (keep watching in watch mode)
 *   {"type":"disable"}    - disable auto-connect
 *   {"type":"enable"}     - enable auto-connect
 *   {"type":"status"}     - request current status
 *
 * Socket path: /tmp/android-tether.sock (writable without root)
 */

#define IPC_SOCK_PATH "/tmp/android-tether.sock"

/* IPC command codes returned by ipc_server_poll() */
#define IPC_CMD_NONE     0
#define IPC_CMD_STOP     1
#define IPC_CMD_DISABLE  2
#define IPC_CMD_ENABLE   3

/* Daemon-side IPC context (opaque) */
typedef struct ipc_server ipc_server_t;

/* Create and start listening on the IPC socket.
   Returns server context or NULL on failure. Non-blocking. */
ipc_server_t *ipc_server_create(void);

/* Accept new connections and process commands from connected clients.
   Non-blocking - call periodically (e.g., every 250ms).
   Returns 1 if a stop command was received, 0 otherwise. */
int ipc_server_poll(ipc_server_t *srv);

/* Send stats update to all connected clients. */
void ipc_server_send_stats(ipc_server_t *srv, const tether_stats_t *stats);

/* Send state change to all connected clients. */
void ipc_server_send_state(ipc_server_t *srv, const char *state,
                           const char *ip, const char *iface);

/* Shutdown and cleanup the IPC server. */
void ipc_server_destroy(ipc_server_t *srv);

#endif /* IPC_H */
