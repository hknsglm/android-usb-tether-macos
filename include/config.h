#ifndef CONFIG_H
#define CONFIG_H

#include "log.h"

/*
 * Configuration system for android-tether.
 *
 * Priority (highest to lowest):
 *   1. Command-line arguments
 *   2. Config file (~/.config/android-tether/config)
 *   3. Built-in defaults
 *
 * Config file format (INI-style):
 *
 *   [network]
 *   no_route = false
 *   no_dns = false
 *   static_ip =
 *   gateway =
 *   netmask = 255.255.255.0
 *
 *   [logging]
 *   level = info
 *
 *   [protocol]
 *   driver = rndis
 */

typedef struct {
    /* Network */
    int no_route;
    int no_dns;
    char static_ip[16];
    char gateway[16];
    char netmask[16];

    /* Logging */
    log_level_t log_level;
    int verbose;

    /* Protocol */
    char protocol[16];

    /* Internal (set by CLI only) */
    int ui_pid;
    int watch_mode;    /* --watch: persistent daemon with auto-connect */
} tether_config_t;

/* Initialize config with built-in defaults */
void config_init_defaults(tether_config_t *cfg);

/* Load config from file. Returns 0 on success, -1 if file not found (not an error). */
int config_load_file(tether_config_t *cfg, const char *path);

/* Load config from default location (~/.config/android-tether/config).
   Returns 0 on success, -1 if file not found. */
int config_load_default(tether_config_t *cfg);

/* Apply command-line arguments over existing config. Returns 0 on success. */
int config_apply_cli(tether_config_t *cfg, int argc, char **argv);

/* Get the default config file path. Returns static buffer. */
const char *config_default_path(void);

#endif /* CONFIG_H */
