#ifndef COMPAT_H
#define COMPAT_H

#include <stdint.h>

/*
 * macOS compatibility layer.
 *
 * Provides runtime version detection and compatibility utilities
 * for supporting macOS 10.15 (Catalina) through the latest versions.
 *
 * The utun API (PF_SYSTEM/SYSPROTO_CONTROL) has been stable since macOS 10.10.
 * The scutil DNS/route approach works across all macOS versions.
 * The main compatibility concern is newer APIs in the Swift UI.
 */

/* macOS version as major.minor (e.g., 14.0 for Sonoma) */
typedef struct {
    int major;
    int minor;
    int patch;
} macos_version_t;

/* Get the running macOS version. Caches result after first call. */
macos_version_t compat_macos_version(void);

/* Check if running macOS >= the specified version */
int compat_macos_at_least(int major, int minor);

/* Get a human-readable macOS version name */
const char *compat_macos_name(void);

#endif /* COMPAT_H */
