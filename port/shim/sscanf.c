/*
 * sscanf for the bionic shim.
 * glibc's <stdio.h> redirects sscanf -> __isoc99_sscanf via an asm label
 * when __USE_ISOC99 is active, so it cannot be defined in shim.c cleanly.
 * This TU avoids stdio.h and binds the vsscanf helper by explicit symbol.
 */
#include <stdarg.h>
#include <stddef.h>

extern int __vsscanf(const char*, const char*, va_list) __asm__("__isoc99_vsscanf");

__attribute__((pcs("aapcs"))) int sscanf(const char* str, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = __vsscanf(str, fmt, ap);
    va_end(ap);
    return r;
}
