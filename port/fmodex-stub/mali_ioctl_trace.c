/*
 * mali_ioctl_trace.so -- optional, low-overhead runtime attribution for the
 * proprietary libMali allocation path.
 *
 * Optional diagnostic preload, not included in the PortMaster package. It records
 * successful and failed KBASE_MEM_ALLOC / KBASE_MEM_FREE ioctls, together
 * with the direct return PC, allocation parameters, and the GPU handle
 * returned by kbase.  Allocation and free records can then be paired offline
 * without ptrace or strace.
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/syscall.h>
#include <unistd.h>

#define KBASE_IOCTL_MEM_ALLOC 0xc0208005UL
#define KBASE_IOCTL_MEM_FREE  0x40088007UL

static int (*real_ioctl_fn)(int, unsigned long, void *);
static int trace_fd = -1;
static int resolving;

static void trace_init(void) {
    const char *path;

    if (trace_fd >= 0 || resolving)
        return;
    resolving = 1;
    real_ioctl_fn = (int (*)(int, unsigned long, void *))dlsym(RTLD_NEXT, "ioctl");
    path = getenv("GOF_MALI_IOCTL_LOG");
    if (!path || !*path)
        path = "/root/gof2hd/mali_ioctl_calls.txt";
    trace_fd = (int)syscall(SYS_openat, AT_FDCWD, path,
                            O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (trace_fd >= 0) {
        const char header[] = "# type fd result errno return_pc image_base image\n";
        (void)syscall(SYS_write, trace_fd, header, sizeof(header) - 1);
    }
    resolving = 0;
}

static void trace_ioctl(unsigned long request, int fd, int result, int call_errno, void *pc,
                        const uint32_t input[8], const uint32_t output[8]) {
    Dl_info info;
    char line[512];
    int len;

    if (trace_fd < 0)
        return;
    memset(&info, 0, sizeof(info));
    (void)dladdr(pc, &info);
    if (request == KBASE_IOCTL_MEM_ALLOC) {
        len = snprintf(line, sizeof(line),
                       "ALLOC fd=%d result=%d errno=%d pc=%p base=%p image=%s"
                       " va=%08x%08x commit=%08x%08x extent=%08x%08x flags=%08x%08x"
                       " handle=%08x%08x out_flags=%08x%08x\n",
                       fd, result, call_errno, pc, info.dli_fbase,
                       info.dli_fname ? info.dli_fname : "?",
                       input[1], input[0], input[3], input[2], input[5], input[4],
                       input[7], input[6], output[3], output[2], output[1], output[0]);
    } else {
        len = snprintf(line, sizeof(line),
                       "FREE fd=%d result=%d errno=%d pc=%p base=%p image=%s handle=%08x%08x\n",
                       fd, result, call_errno, pc, info.dli_fbase,
                       info.dli_fname ? info.dli_fname : "?", input[1], input[0]);
    }
    if (len > 0) {
        if ((size_t)len >= sizeof(line))
            len = (int)sizeof(line) - 1;
        (void)syscall(SYS_write, trace_fd, line, (size_t)len);
    }
}

int ioctl(int fd, unsigned long request, void *arg) {
    int result;
    int saved_errno;
    void *pc;
    uint32_t input[8] = { 0 };
    uint32_t output[8] = { 0 };

    if (!real_ioctl_fn)
        trace_init();
    if (!real_ioctl_fn) {
        errno = ENOSYS;
        return -1;
    }
    if ((request == KBASE_IOCTL_MEM_ALLOC || request == KBASE_IOCTL_MEM_FREE) && arg)
        memcpy(input, arg, sizeof(input));
    result = real_ioctl_fn(fd, request, arg);
    saved_errno = result < 0 ? errno : 0;
    if (request == KBASE_IOCTL_MEM_ALLOC && arg)
        memcpy(output, arg, sizeof(output));
    if (request == KBASE_IOCTL_MEM_ALLOC || request == KBASE_IOCTL_MEM_FREE) {
        pc = __builtin_return_address(0);
        trace_ioctl(request, fd, result, saved_errno, pc, input, output);
    }
    errno = saved_errno;
    return result;
}

void __attribute__((constructor)) mali_ioctl_trace_constructor(void) {
    trace_init();
}
