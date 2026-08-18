#include "game_internal.h"

void *odg_memset(void *dst, int value, size_t n) {
    uint8_t *d = (uint8_t *)dst;
    size_t i;
    for (i = 0; i < n; ++i) d[i] = (uint8_t)value;
    return dst;
}

void *odg_memcpy(void *dst, const void *src, size_t n) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    size_t i;
    for (i = 0; i < n; ++i) d[i] = s[i];
    return dst;
}

/* Freestanding wasm builds may lower structure operations to these symbols. */
void *memset(void *dst, int value, size_t n) { return odg_memset(dst, value, n); }
void *memcpy(void *dst, const void *src, size_t n) { return odg_memcpy(dst, src, n); }
void *memmove(void *dst, const void *src, size_t n) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    if (d < s) {
        size_t i; for (i = 0; i < n; ++i) d[i] = s[i];
    } else if (d > s) {
        size_t i = n; while (i != 0u) { --i; d[i] = s[i]; }
    }
    return dst;
}
