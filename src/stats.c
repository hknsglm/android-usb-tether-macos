#include "stats.h"

#include <stdio.h>
#include <unistd.h>

#define STATS_JSON_PATH     "/tmp/android_tether_stats.json"
#define STATS_JSON_TMP_PATH "/tmp/android_tether_stats.json.tmp"
#define STOP_TRIGGER_PATH   "/tmp/android_tether_stop"

void stats_write_json(const tether_stats_t *stats)
{
    FILE *f = fopen(STATS_JSON_TMP_PATH, "w");
    if (!f)
        return;

    fprintf(f, "{\n"
               "  \"tx_mbps\": %.2f,\n"
               "  \"rx_mbps\": %.2f,\n"
               "  \"tx_bytes_total\": %llu,\n"
               "  \"rx_bytes_total\": %llu,\n"
               "  \"tx_pkts_total\": %llu,\n"
               "  \"rx_pkts_total\": %llu\n"
               "}\n",
               stats->tx_mbps, stats->rx_mbps,
               (unsigned long long)stats->tx_bytes,
               (unsigned long long)stats->rx_bytes,
               (unsigned long long)stats->tx_pkts,
               (unsigned long long)stats->rx_pkts);
    fclose(f);
    rename(STATS_JSON_TMP_PATH, STATS_JSON_PATH);
}

int stats_check_stop_trigger(void)
{
    if (access(STOP_TRIGGER_PATH, F_OK) == 0) {
        unlink(STOP_TRIGGER_PATH);
        return 1;
    }
    return 0;
}

void stats_cleanup(void)
{
    unlink(STATS_JSON_PATH);
    unlink(STATS_JSON_TMP_PATH);
}
