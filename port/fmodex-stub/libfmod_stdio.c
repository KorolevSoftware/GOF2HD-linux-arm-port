/*
 * libfmod_stdio.so — caller-aware stdio bridge for real (bionic) FMOD.
 *
 * FMOD (Android bionic build) reads the bionic __sFILE._flags field directly
 * from memory at FILE+12 after fread/fseek. When bound to glibc FILE, +12 is
 * _IO_read_base (a pointer) -> garbage EOF/ERR bits -> spurious FILE_EOF(22).
 *
 * Route stdio calls originating from libfmodex/libfmodevent to the shim
 * libc.so (bionic FILE pool); all other callers keep glibc (RTLD_NEXT).
 *
 * The caller check uses /proc/self/maps (low-level open/read, never dladdr —
 * dladdr inside an interposed stdio function crashes early startup).
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

static FILE* (*next_fopen)(const char*, const char*);
static size_t(*next_fread)(void*, size_t, size_t, FILE*);
static int   (*next_fseek)(FILE*, long, int);
static long  (*next_ftell)(FILE*);
static int   (*next_fclose)(FILE*);

static FILE* (*shim_fopen)(const char*, const char*);
static size_t(*shim_fread)(void*, size_t, size_t, FILE*);
static int   (*shim_fseek)(FILE*, long, int);
static long  (*shim_ftell)(FILE*);
static int   (*shim_fclose)(FILE*);

static unsigned long g_fx_lo, g_fx_hi, g_fv_lo, g_fv_hi;
static unsigned long g_ge_lo, g_ge_hi;
static int g_maps_done;
static int dbg_on(void);

static void load_fmod_ranges(void) {
    int fd = open("/proc/self/maps", O_RDONLY);
    if (fd < 0) { g_maps_done = 1; return; }
    static char buf[262144];
    ssize_t total = 0;
    ssize_t n;
    while ((n = read(fd, buf + total, sizeof(buf) - 1 - total)) > 0)
        total += n;
    close(fd);
    if (total <= 0) { g_maps_done = 1; return; }
    buf[total] = 0;
    char *p = buf;
    while (*p) {
        char *e = strchr(p, '\n');
        if (e) *e = 0;
        unsigned long lo, hi;
        if (strstr(p, "libfmodex.so") || strstr(p, "libfmodevent.so")) {
            if (dbg_on())
                fprintf(stderr, "[fsio] maps line: %s\n", p);
        }
        if (sscanf(p, "%lx-%lx", &lo, &hi) == 2) {
            if (strstr(p, "libfmodex.so")) {
                if (!g_fx_lo || lo < g_fx_lo) g_fx_lo = lo;
                if (hi > g_fx_hi) g_fx_hi = hi;
            } else if (strstr(p, "libfmodevent.so")) {
                if (!g_fv_lo || lo < g_fv_lo) g_fv_lo = lo;
                if (hi > g_fv_hi) g_fv_hi = hi;
            } else if (strstr(p, "libgof2hdaa.so")) {
                if (!g_ge_lo || lo < g_ge_lo) g_ge_lo = lo;
                if (hi > g_ge_hi) g_ge_hi = hi;
            }
        }
        p = e ? e + 1 : p + strlen(p);
    }
    g_maps_done = 1;
}

static int dbg_on(void) { static int v = -1; if (v < 0) v = getenv("GOF_FMOD_STDIO_DEBUG") ? 1 : 0; return v; }

static int is_fmod_ra(void *ra) {
    if (!g_maps_done || !g_fx_hi || !g_fv_hi || !g_ge_hi) load_fmod_ranges();
    unsigned long r = (unsigned long)ra;
    /* engine (libgof2hdaa) and FMOD must use the shim (bionic FILE) — that is
     * what the base setup does (shim precedes glibc in the engine's scope).
     * SDL2/ALSA/host keep glibc. */
    return (r >= g_fx_lo && r < g_fx_hi) ||
           (r >= g_fv_lo && r < g_fv_hi) ||
           (r >= g_ge_lo && r < g_ge_hi);
}

static void resolve_glibc(void) {
    if (next_fopen) return;
    next_fopen  = (FILE* (*)(const char*, const char*))dlsym(RTLD_NEXT, "fopen");
    next_fread  = (size_t (*)(void*, size_t, size_t, FILE*))dlsym(RTLD_NEXT, "fread");
    next_fseek  = (int   (*)(FILE*, long, int))dlsym(RTLD_NEXT, "fseek");
    next_ftell  = (long  (*)(FILE*))dlsym(RTLD_NEXT, "ftell");
    next_fclose = (int   (*)(FILE*))dlsym(RTLD_NEXT, "fclose");
    if (dbg_on()) {
        void *g = dlopen("/lib/arm-linux-gnueabihf/libc.so.6", RTLD_NOW | RTLD_NOLOAD | RTLD_LOCAL);
        void *gf = g ? dlsym(g, "fopen") : 0;
        void *sh = dlopen("/root/gof2hd/port/run-native/libc.so", RTLD_NOW | RTLD_NOLOAD | RTLD_LOCAL);
        void *sf = sh ? dlsym(sh, "fopen") : 0;
        fprintf(stderr, "[fsio] next_fopen=%p glibc_fopen=%p shim_fopen=%p\n",
                (void*)next_fopen, gf, sf);
    }
}

static void resolve_shim(void) {
    if (shim_fopen) return;
    void *h = dlopen("/root/gof2hd/port/run-native/libc.so", RTLD_NOW | RTLD_NOLOAD | RTLD_LOCAL);
    if (dbg_on())
        fprintf(stderr, "[fsio] resolve_shim h=%p err=%s\n", h, dlerror() ? dlerror() : "none");
    if (!h) return;
    shim_fopen  = (FILE* (*)(const char*, const char*))dlvsym(h, "fopen", "LIBC");
    shim_fread  = (size_t (*)(void*, size_t, size_t, FILE*))dlvsym(h, "fread", "LIBC");
    shim_fseek  = (int   (*)(FILE*, long, int))dlvsym(h, "fseek", "LIBC");
    shim_ftell  = (long  (*)(FILE*))dlvsym(h, "ftell", "LIBC");
    shim_fclose = (int   (*)(FILE*))dlvsym(h, "fclose", "LIBC");
    if (dbg_on())
        fprintf(stderr, "[fsio] shim fopen=%p fread=%p\n", (void*)shim_fopen, (void*)shim_fread);
}

FILE *fopen(const char *path, const char *mode) {
    resolve_glibc();
    void *ra = __builtin_return_address(0);
    int fmod = is_fmod_ra(ra);
    FILE *r;
    if (fmod) {
        resolve_shim();
        if (shim_fopen) {
            r = shim_fopen(path, mode);
            if (!r && dbg_on() && (strstr(path, "FMOD_GOF2") || strstr(path, ".fev")))
                fprintf(stderr, "[fsio] shim_fopen %s FAILED errno=%d\n", path, errno);
            goto out;
        }
    }
    r = next_fopen(path, mode);
out:
    if (dbg_on() && (!strncmp(path, "/root/gof2hd", 12) || strstr(path, ".obb") || strstr(path, "base.apk") || strstr(path, ".fsb") || strstr(path, ".fev")))
        fprintf(stderr, "[fsio] fopen %s fmod=%d -> %p\n", path, fmod, r);
    return r;
}

size_t fread(void *ptr, size_t sz, size_t n, FILE *f) {
    resolve_glibc();
    void *ra = __builtin_return_address(0);
    int fmod = is_fmod_ra(ra);
    if (dbg_on())
        fprintf(stderr, "[fsio] fread fmod=%d fp=%p sz=%zu n=%zu\n", fmod, f, sz, n);
    if (fmod) { resolve_shim(); if (shim_fread) return shim_fread(ptr, sz, n, f); }
    return next_fread(ptr, sz, n, f);
}

int fseek(FILE *f, long off, int whence) {
    resolve_glibc();
    void *ra = __builtin_return_address(0);
    if (is_fmod_ra(ra)) { resolve_shim(); if (shim_fseek) return shim_fseek(f, off, whence); }
    return next_fseek(f, off, whence);
}

long ftell(FILE *f) {
    resolve_glibc();
    void *ra = __builtin_return_address(0);
    if (is_fmod_ra(ra)) { resolve_shim(); if (shim_ftell) return shim_ftell(f); }
    return next_ftell(f);
}

int fclose(FILE *f) {
    resolve_glibc();
    void *ra = __builtin_return_address(0);
    if (is_fmod_ra(ra)) { resolve_shim(); if (shim_fclose) return shim_fclose(f); }
    return next_fclose(f);
}
