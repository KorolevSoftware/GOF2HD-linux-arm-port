#define _GNU_SOURCE
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>

static int (*real_open)(const char *, int, ...) = 0;
static int (*real_open64)(const char *, int, ...) = 0;

static char g_cpuinfo[2048];

static void build_cpuinfo(void) {
    const char *feat = getenv("FMOD_FAKE_FEATURES");
    if (!feat || !*feat) feat = "neon vfp vfpv3 vfpv3d16";
    snprintf(g_cpuinfo, sizeof(g_cpuinfo),
        "processor\t: 0\n"
        "BogoMIPS\t: 48.00\n"
        "Features\t: fp asimd evtstrm aes pmull sha1 sha2 crc32 %s\n"
        "CPU implementer\t: 0x41\n"
        "CPU architecture: 8\n"
        "CPU variant\t: 0x0\n"
        "CPU part\t: 0xd05\n"
        "CPU revision\t: 0\n"
        "\n"
        "Hardware\t: Rockchip RK3326\n", feat);
}

static int fake_cpuinfo_fd(void) {
    int p[2];
    if (pipe(p) != 0) return -1;
    size_t len = strlen(g_cpuinfo);
    (void)write(p[1], g_cpuinfo, len);
    close(p[1]);
    return p[0];
}

static int g_trace;
static void trace_open(const char *path, int fd) {
    if (!g_trace) return;
    fprintf(stderr, "[open] fd=%d %s\n", fd, path ? path : "(null)");
}

int open(const char *path, int flags, ...) {
    if (path && strcmp(path, "/proc/cpuinfo") == 0) {
        int fd = fake_cpuinfo_fd();
        trace_open(path, fd);
        return fd;
    }
    int fd = real_open ? real_open(path, flags, 0) : -1;
    trace_open(path, fd);
    return fd;
}

int open64(const char *path, int flags, ...) {
    if (path && strcmp(path, "/proc/cpuinfo") == 0) {
        int fd = fake_cpuinfo_fd();
        trace_open(path, fd);
        return fd;
    }
    int fd = real_open64 ? real_open64(path, flags, 0) : -1;
    trace_open(path, fd);
    return fd;
}

void __attribute__((constructor)) init(void) {
    real_open = (int (*)(const char *, int, ...))dlsym(RTLD_NEXT, "open");
    real_open64 = (int (*)(const char *, int, ...))dlsym(RTLD_NEXT, "open64");
    g_trace = getenv("FMOD_OPEN_TRACE") ? 1 : 0;
    build_cpuinfo();
    if (getenv("FMOD_FAKE_DEBUG")) fprintf(stderr, "[cpuinfo_fake] fake content: %s", g_cpuinfo);
}