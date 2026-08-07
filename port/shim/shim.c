/*
 * libc.so — bionic specials shim for GOF2HD ARMv7 binaries on armhf Linux.
 *
 * The engine was linked against Android's bionic libc and used to import
 * symbols versioned @LIBC.  We now strip all versioning from the engine
 * (see tools/patch-versions.py), so the dynamic linker binds imports by name
 * to the first provider: glibc for plain functions, this library for the
 * symbols below that (a) glibc does not implement under these names, or
 * (b) need bionic<->glibc ABI translation (FILE, stat, pthread sizes,
 * float-passing) which lives in stdio.c / abi.c / sscanf.c.
 *
 * This file keeps only the "specials" with no glibc equivalent plus resolve().
 * Everything else (malloc, strlen, ... ) is bound straight to glibc now.
 *
 * Build (armhf):  see tools/build-native.sh
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <dlfcn.h>
#include <errno.h>

void* resolve(const char* n) {
    void* p = dlsym(RTLD_NEXT, n);
    if (!p) p = dlsym(RTLD_DEFAULT, n);
    if (!p) { dprintf(2, "[bionic-shim] cannot resolve %s\n", n); abort(); }
    return p;
}

/* ================= specials (no direct glibc equivalent) ================= */

int* __errno(void) { return &errno; }

void __assert2(const char* file, int line, const char* func, const char* expr) {
    dprintf(2, "Assertion failed: %s, function %s, file %s, line %d\n",
            expr, func, file, line);
    abort();
}

unsigned long __stack_chk_guard = 0xdeadbeef;
void __stack_chk_fail(void) { dprintf(2, "stack smashing detected\n"); abort(); }

/* gcc-4.x helper not present in modern libgcc_s */
long long __gnu_ldivmod_helper(long long a, long long b, long long* remainder) {
    long long q = a / b;
    *remainder = a % b;
    return q;
}