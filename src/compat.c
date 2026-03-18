#include "compat.h"

#include <stdio.h>
#include <string.h>
#include <sys/sysctl.h>

static macos_version_t g_version = {0, 0, 0};
static int g_version_detected = 0;

macos_version_t compat_macos_version(void)
{
    if (g_version_detected)
        return g_version;

    /* Use kern.osproductversion sysctl (available since macOS 10.13.4) */
    char ver[32] = {0};
    size_t len = sizeof(ver);
    if (sysctlbyname("kern.osproductversion", ver, &len, NULL, 0) == 0) {
        sscanf(ver, "%d.%d.%d", &g_version.major, &g_version.minor, &g_version.patch);
    } else {
        /* Fallback: parse kern.osrelease (Darwin version) and map to macOS */
        char osrel[32] = {0};
        len = sizeof(osrel);
        if (sysctlbyname("kern.osrelease", osrel, &len, NULL, 0) == 0) {
            int darwin_major = 0;
            sscanf(osrel, "%d", &darwin_major);
            /* Darwin 19 = macOS 10.15, 20 = 11.0, 21 = 12.0, etc. */
            if (darwin_major >= 20) {
                g_version.major = darwin_major - 9;
                g_version.minor = 0;
            } else {
                g_version.major = 10;
                g_version.minor = darwin_major - 4;
            }
        }
    }

    g_version_detected = 1;
    return g_version;
}

int compat_macos_at_least(int major, int minor)
{
    macos_version_t v = compat_macos_version();
    if (v.major > major) return 1;
    if (v.major == major && v.minor >= minor) return 1;
    return 0;
}

const char *compat_macos_name(void)
{
    macos_version_t v = compat_macos_version();

    if (v.major >= 26) return "macOS (future)";
    if (v.major >= 15) return "macOS Sequoia";
    if (v.major >= 14) return "macOS Sonoma";
    if (v.major >= 13) return "macOS Ventura";
    if (v.major >= 12) return "macOS Monterey";
    if (v.major >= 11) return "macOS Big Sur";
    if (v.major == 10) {
        if (v.minor >= 15) return "macOS Catalina";
        if (v.minor >= 14) return "macOS Mojave";
        return "macOS (legacy)";
    }
    return "macOS (unknown)";
}
