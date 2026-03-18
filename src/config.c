#include "config.h"
#include "log.h"
#include "net_types.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <getopt.h>
#include <pwd.h>

#define TAG "config"

void config_init_defaults(tether_config_t *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    strlcpy(cfg->netmask, DEFAULT_NETMASK, sizeof(cfg->netmask));
    strlcpy(cfg->protocol, "rndis", sizeof(cfg->protocol));
    cfg->log_level = LOG_LEVEL_INFO;
}

const char *config_default_path(void)
{
    static char path[512];
    const char *home = getenv("HOME");
    if (!home) {
        struct passwd *pw = getpwuid(getuid());
        if (pw)
            home = pw->pw_dir;
    }
    if (!home) return NULL;

    snprintf(path, sizeof(path), "%s/.config/android-tether/config", home);
    return path;
}

static char *strip(char *s)
{
    while (*s && isspace((unsigned char)*s)) s++;
    char *end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end)) *end-- = '\0';
    return s;
}

static int parse_bool(const char *val)
{
    if (strcasecmp(val, "true") == 0 || strcasecmp(val, "yes") == 0 ||
        strcasecmp(val, "1") == 0 || strcasecmp(val, "on") == 0)
        return 1;
    return 0;
}

static log_level_t parse_log_level(const char *val)
{
    if (strcasecmp(val, "debug") == 0) return LOG_LEVEL_DEBUG;
    if (strcasecmp(val, "info") == 0) return LOG_LEVEL_INFO;
    if (strcasecmp(val, "warn") == 0 || strcasecmp(val, "warning") == 0) return LOG_LEVEL_WARN;
    if (strcasecmp(val, "error") == 0) return LOG_LEVEL_ERROR;
    return LOG_LEVEL_INFO;
}

int config_load_file(tether_config_t *cfg, const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) return -1;

    char line[256];
    char section[32] = "";

    while (fgets(line, sizeof(line), f)) {
        char *s = strip(line);
        if (*s == '\0' || *s == '#' || *s == ';')
            continue;

        /* Section header */
        if (*s == '[') {
            char *end = strchr(s, ']');
            if (end) {
                *end = '\0';
                strlcpy(section, s + 1, sizeof(section));
            }
            continue;
        }

        /* Key = value */
        char *eq = strchr(s, '=');
        if (!eq) continue;

        *eq = '\0';
        char *key = strip(s);
        char *val = strip(eq + 1);

        if (strcmp(section, "network") == 0) {
            if (strcmp(key, "no_route") == 0) cfg->no_route = parse_bool(val);
            else if (strcmp(key, "no_dns") == 0) cfg->no_dns = parse_bool(val);
            else if (strcmp(key, "static_ip") == 0 && *val)
                strlcpy(cfg->static_ip, val, sizeof(cfg->static_ip));
            else if (strcmp(key, "gateway") == 0 && *val)
                strlcpy(cfg->gateway, val, sizeof(cfg->gateway));
            else if (strcmp(key, "netmask") == 0 && *val)
                strlcpy(cfg->netmask, val, sizeof(cfg->netmask));
        } else if (strcmp(section, "logging") == 0) {
            if (strcmp(key, "level") == 0) cfg->log_level = parse_log_level(val);
        } else if (strcmp(section, "protocol") == 0) {
            if (strcmp(key, "driver") == 0 && *val)
                strlcpy(cfg->protocol, val, sizeof(cfg->protocol));
        }
    }

    fclose(f);
    LOG_D(TAG, "loaded config from %s", path);
    return 0;
}

int config_load_default(tether_config_t *cfg)
{
    const char *path = config_default_path();
    if (!path) return -1;
    return config_load_file(cfg, path);
}

int config_apply_cli(tether_config_t *cfg, int argc, char **argv)
{
    static struct option long_opts[] = {
        {"no-route", no_argument, 0, 'n'},
        {"no-dns",   no_argument, 0, 'd'},
        {"static",   required_argument, 0, 's'},
        {"gateway",  required_argument, 0, 'g'},
        {"netmask",  required_argument, 0, 'm'},
        {"verbose",  no_argument, 0, 'v'},
        {"help",     no_argument, 0, 'h'},
        {"ui-pid",   required_argument, 0, 'p'},
        {"version",  no_argument, 0, 'V'},
        {"config",   required_argument, 0, 'c'},
        {"watch",    no_argument, 0, 'w'},
        {0, 0, 0, 0}
    };

    /* Reset getopt */
    optind = 1;

    int c;
    while ((c = getopt_long(argc, argv, "nds:g:m:vhp:Vc:w", long_opts, NULL)) != -1) {
        switch (c) {
        case 'n': cfg->no_route = 1; break;
        case 'd': cfg->no_dns = 1; break;
        case 's': strlcpy(cfg->static_ip, optarg, sizeof(cfg->static_ip)); break;
        case 'g': strlcpy(cfg->gateway, optarg, sizeof(cfg->gateway)); break;
        case 'm': strlcpy(cfg->netmask, optarg, sizeof(cfg->netmask)); break;
        case 'v': cfg->verbose = 1; cfg->log_level = LOG_LEVEL_DEBUG; break;
        case 'p': {
            char *endp;
            long val = strtol(optarg, &endp, 10);
            if (endp == optarg || *endp != '\0' || val <= 0) {
                fprintf(stderr, "error: invalid --ui-pid value: %s\n", optarg);
                return -1;
            }
            cfg->ui_pid = (int)val;
            break;
        }
        case 'c':
            if (config_load_file(cfg, optarg) < 0) {
                fprintf(stderr, "error: cannot load config file: %s\n", optarg);
                return -1;
            }
            break;
        case 'V':
#ifdef VERSION
            fprintf(stderr, "android-tether version %s\n", VERSION);
#else
            fprintf(stderr, "android-tether version dev\n");
#endif
            exit(0);
        case 'w': cfg->watch_mode = 1; break;
        case 'h':
            return 1; /* signal to print help */
        default:
            return -1;
        }
    }

    return 0;
}
