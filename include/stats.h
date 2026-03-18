#ifndef STATS_H
#define STATS_H

#include <stdint.h>

/* Tethering session statistics */
typedef struct {
    double tx_mbps;
    double rx_mbps;
    uint64_t tx_bytes;
    uint64_t rx_bytes;
    uint64_t tx_pkts;
    uint64_t rx_pkts;
} tether_stats_t;

/* Write stats to JSON file for UI consumption.
   Uses atomic rename to prevent partial reads. */
void stats_write_json(const tether_stats_t *stats);

/* Check if the stop trigger file exists.
   Returns 1 if stop was requested, 0 otherwise.
   Removes the trigger file if found. */
int stats_check_stop_trigger(void);

/* Remove stats files on cleanup */
void stats_cleanup(void);

#endif /* STATS_H */
