/*
 * libc.so — bionic-ABI shim for GOF2HD ARMv7 binaries on armhf Linux.
 *
 * The game .so files were linked against Android's bionic libc and import
 * symbols versioned @LIBC (e.g. pthread_create@LIBC, malloc@LIBC).
 * glibc does not provide the "LIBC" version node, so we export every
 * bionic-imported symbol with version "LIBC" and forward to glibc.
 *
 * The symbol set below is EXACTLY what libgof2hdaa.so imports (checked with
 * readelf on the engine).  Everything else from bionic's libc export list was
 * dropped on purpose; the engine never references it.
 *
 * Build (armhf):  see tools/build-native.sh
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stddef.h>
#include <unistd.h>
#include <time.h>
#include <dlfcn.h>
#include <errno.h>
#include <pthread.h>

/*
 * Build note: this file is compiled hardfp (aapcs-vfp) so it can link
 * against the device's hardfp glibc and libmali.  The engine, however, is
 * softfp (bionic armeabi-v7a).  Every function the engine calls with a
 * float argument or varargs is therefore exported with
 * __attribute__((pcs("aapcs"))) (softfp entry); the glibc target is called
 * through a hardfp (aapcs-vfp) pointer inside.  Note: after moving all math
 * into libm.c none of the remaining exports carries float args, so no SF/HF
 * wrappers are needed here (int/pointers pass identically in both ABIs).
 */

void* resolve(const char* n) {
    void* p = dlsym(RTLD_NEXT, n);
    if (!p) p = dlsym(RTLD_DEFAULT, n);
    if (!p) { dprintf(2, "[bionic-shim] cannot resolve %s\n", n); abort(); }
    return p;
}

/* ================= specials (no direct glibc equivalent) ================= */

int* __errno(void) { return &errno; }

/*
 * bionic stdio (FILE, __sF, fopen, fread, printf-family, ...) lives in
 * stdio.c — fully own implementation, no glibc FILEs involved.
 */

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

/* ================= function-pointer-arg forwarders ================= */

int __cxa_atexit(void (*a)(void*), void* b, void* c) {
    return ((int (*)(void (*)(void*), void*, void*))resolve("__cxa_atexit"))(a, b, c);
}
void __cxa_finalize(void* a) { ((void (*)(void*))resolve("__cxa_finalize"))(a); }
int pthread_key_create(pthread_key_t* a, void (*b)(void*)) {
    return ((int (*)(pthread_key_t*, void (*)(void*)))resolve("pthread_key_create"))(a, b);
}
int pthread_once(pthread_once_t* a, void (*b)(void)) {
    return ((int (*)(pthread_once_t*, void (*)(void)))resolve("pthread_once"))(a, b);
}
void qsort(void* a, size_t b, size_t c, int (*d)(const void*, const void*)) {
    ((void (*)(void*, size_t, size_t, int (*)(const void*, const void*)))
     resolve("qsort"))(a, b, c, d);
}

/* ================= plain forwarders ================= */

void  abort(void)   { ((void (*)(void))resolve("abort"))(); }
void  free(void* a) { ((void (*)(void*))resolve("free"))(a); }
int   getpid(void)  { return ((int (*)(void))resolve("getpid"))(); }
int   raise(int a)  { return ((int (*)(int))resolve("raise"))(a); }
mode_t umask(mode_t a) { return ((mode_t (*)(mode_t))resolve("umask"))(a); }

int    atoi(const char* a)              { return ((int (*)(const char*))resolve("atoi"))(a); }
int    islower(int a)                    { return ((int (*)(int))resolve("islower"))(a); }
int    isxdigit(int a)                   { return ((int (*)(int))resolve("isxdigit"))(a); }
void*  malloc(size_t a)                  { return ((void* (*)(size_t))resolve("malloc"))(a); }
void*  calloc(size_t a, size_t b)        { return ((void* (*)(size_t, size_t))resolve("calloc"))(a, b); }
void*  realloc(void* a, size_t b)        { return ((void* (*)(void*, size_t))resolve("realloc"))(a, b); }
void*  memalign(size_t a, size_t b)      { return ((void* (*)(size_t, size_t))resolve("memalign"))(a, b); }
void*  memset(void* a, int b, size_t c)  { return ((void* (*)(void*, int, size_t))resolve("memset"))(a, b, c); }
void*  memcpy(void* a, const void* b, size_t c)  { return ((void* (*)(void*, const void*, size_t))resolve("memcpy"))(a, b, c); }
void*  memchr(const void* a, int b, size_t c)    { return ((void* (*)(const void*, int, size_t))resolve("memchr"))(a, b, c); }
int    memcmp(const void* a, const void* b, size_t c) { return ((int (*)(const void*, const void*, size_t))resolve("memcmp"))(a, b, c); }
char*  strdup(const char* a)             { return ((char* (*)(const char*))resolve("strdup"))(a); }
size_t strlen(const char* a)             { return ((size_t (*)(const char*))resolve("strlen"))(a); }
char*  strcpy(char* a, const char* b)    { return ((char* (*)(char*, const char*))resolve("strcpy"))(a, b); }
int    strcmp(const char* a, const char* b) { return ((int (*)(const char*, const char*))resolve("strcmp"))(a, b); }
int    strncmp(const char* a, const char* b, size_t c) { return ((int (*)(const char*, const char*, size_t))resolve("strncmp"))(a, b, c); }
int    strcasecmp(const char* a, const char* b)  { return ((int (*)(const char*, const char*))resolve("strcasecmp"))(a, b); }
char*  strrchr(const char* a, int b)     { return ((char* (*)(const char*, int))resolve("strrchr"))(a, b); }
unsigned long strtoul(const char* a, char** b, int c) { return ((unsigned long (*)(const char*, char**, int))resolve("strtoul"))(a, b, c); }
char*  strerror(int a)                   { return ((char* (*)(int))resolve("strerror"))(a); }
int    remove(const char* a)             { return ((int (*)(const char*))resolve("remove"))(a); }
int    rename(const char* a, const char* b)     { return ((int (*)(const char*, const char*))resolve("rename"))(a, b); }
int    chmod(const char* a, mode_t b)    { return ((int (*)(const char*, mode_t))resolve("chmod"))(a, b); }

int    open(const char* a, int b, ...)   { va_list ap; va_start(ap, b); int mode = va_arg(ap, int); va_end(ap); return ((int (*)(const char*, int, int))resolve("open"))(a, b, mode); }
int    close(int a)                      { return ((int (*)(int))resolve("close"))(a); }
struct tm* localtime(const time_t* a)    { return ((struct tm* (*)(const time_t*))resolve("localtime"))(a); }
time_t time(time_t* a)                   { return ((time_t (*)(time_t*))resolve("time"))(a); }
int    mkstemp(char* a)                  { return ((int (*)(char*))resolve("mkstemp"))(a); }

void* pthread_getspecific(pthread_key_t a) { return ((void* (*)(pthread_key_t))resolve("pthread_getspecific"))(a); }
int pthread_setspecific(pthread_key_t a, const void* b) { return ((int (*)(pthread_key_t, const void*))resolve("pthread_setspecific"))(a, b); }