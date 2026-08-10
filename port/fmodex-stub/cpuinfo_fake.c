#define _GNU_SOURCE
#include <stdarg.h>
#include <string.h>
#include <dlfcn.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>

static int (*real_open)(const char *, int, ...) = 0;
static int (*real_open64)(const char *, int, ...) = 0;

static const char g_cpuinfo[] =
    "processor\t: 0\n"
    "BogoMIPS\t: 48.00\n"
    "Features\t: fp asimd evtstrm aes pmull sha1 sha2 crc32 neon vfp vfpv3 vfpv3d16\n"
    "CPU implementer\t: 0x41\n"
    "CPU architecture: 8\n"
    "CPU variant\t: 0x0\n"
    "CPU part\t: 0xd03\n"
    "CPU revision\t: 0\n"
    "\n"
    "Hardware\t: Rockchip RK3326\n";

static int fake_cpuinfo_fd(void) {
    int p[2];
    if (pipe(p) != 0) return -1;
    size_t len = strlen(g_cpuinfo);
    (void)write(p[1], g_cpuinfo, len);
    close(p[1]);
    return p[0];
}

int open(const char *path, int flags, ...) {
    if (path && strcmp(path, "/proc/cpuinfo") == 0)
        return fake_cpuinfo_fd();
    return real_open ? real_open(path, flags, 0) : -1;
}

int open64(const char *path, int flags, ...) {
    if (path && strcmp(path, "/proc/cpuinfo") == 0)
        return fake_cpuinfo_fd();
    return real_open64 ? real_open64(path, flags, 0) : -1;
}

void __attribute__((constructor)) init(void) {
    real_open = (int (*)(const char *, int, ...))dlsym(RTLD_NEXT, "open");
    real_open64 = (int (*)(const char *, int, ...))dlsym(RTLD_NEXT, "open64");
}
