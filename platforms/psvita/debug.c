#include <stdbool.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>

#include <psp2/kernel/clib.h>
#include "debug.h"

void vita_debug_log(const char *s, ...) {
    char* buffer = malloc(8192);
    if (!buffer) {
        return;
    }

    va_list va;
    va_start(va, s);
    vsnprintf(buffer, 8192, s, va);
    va_end(va);


    sceClibPrintf("%s\n", buffer);


    free(buffer);
}

void vita_log_connectivity(const char *event_type, const char *details) {
    char* buffer = malloc(8192);
    if (!buffer) {
        return;
    }

    snprintf(buffer, 8192, "[CONNECTIVITY] %s: %s\n", event_type, details);


    sceClibPrintf("%s", buffer);


    free(buffer);
}
