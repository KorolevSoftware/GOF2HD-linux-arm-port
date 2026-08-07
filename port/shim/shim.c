/*
 * libc.so — bionic-ABI shim for GOF2HD ARMv7 binaries on armhf Linux.
 *
 * The game .so files were linked against Android's bionic libc and import
 * symbols versioned @LIBC (e.g. pthread_create@LIBC, malloc@LIBC).
 * glibc does not provide the "LIBC" version node, so we export every
 * bionic-imported symbol with version "LIBC" and forward to glibc.
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
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/utsname.h>
#include <sys/vfs.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <utime.h>
#include <time.h>
#include <dlfcn.h>
#include <errno.h>

/*
 * Build note: this file is compiled hardfp (aapcs-vfp) so it can link
 * against the device's hardfp glibc and libmali.  The engine, however, is
 * softfp (bionic armeabi-v7a).  Every function the engine calls with a
 * float/double argument or varargs is therefore exported with
 * __attribute__((pcs("aapcs"))) (softfp entry); the glibc target is called
 * through a hardfp (aapcs-vfp) pointer inside.  Integer/pointer-only
 * functions need no wrapper: register usage for those is identical in both
 * ABIs.
 */
#define SF  __attribute__((pcs("aapcs")))
#define HF  __attribute__((pcs("aapcs-vfp")))

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

/* ================= variadic forwarders ================= */

long syscall(long n_, ...) {
    va_list ap; va_start(ap, n_);
    long a1 = va_arg(ap, long), a2 = va_arg(ap, long), a3 = va_arg(ap, long);
    long a4 = va_arg(ap, long), a5 = va_arg(ap, long), a6 = va_arg(ap, long);
    va_end(ap);
    return ((long (*)(long, long, long, long, long, long, long))
            resolve("syscall"))(n_, a1, a2, a3, a4, a5, a6);
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
void  _exit(int a)  { ((void (*)(int))resolve("_exit"))(a); }
void  free(void* a) { ((void (*)(void*))resolve("free"))(a); }
int   getpid(void)  { return ((int (*)(void))resolve("getpid"))(); }
long  lrand48(void) { return ((long (*)(void))resolve("lrand48"))(); }
pthread_t pthread_self(void) { return ((pthread_t (*)(void))resolve("pthread_self"))(); }
int   raise(int a)  { return ((int (*)(int))resolve("raise"))(a); }
void  srand48(long a) { ((void (*)(long))resolve("srand48"))(a); }
mode_t umask(mode_t a) { return ((mode_t (*)(mode_t))resolve("umask"))(a); }

int    atoi(const char* a)              { return ((int (*)(const char*))resolve("atoi"))(a); }
long long atoll(const char* a)           { return ((long long (*)(const char*))resolve("atoll"))(a); }
int    islower(int a)                    { return ((int (*)(int))resolve("islower"))(a); }
int    isxdigit(int a)                   { return ((int (*)(int))resolve("isxdigit"))(a); }
void*  malloc(size_t a)                  { return ((void* (*)(size_t))resolve("malloc"))(a); }
void*  calloc(size_t a, size_t b)        { return ((void* (*)(size_t, size_t))resolve("calloc"))(a, b); }
void*  realloc(void* a, size_t b)        { return ((void* (*)(void*, size_t))resolve("realloc"))(a, b); }
void*  memalign(size_t a, size_t b)      { return ((void* (*)(size_t, size_t))resolve("memalign"))(a, b); }
void*  memset(void* a, int b, size_t c)  { return ((void* (*)(void*, int, size_t))resolve("memset"))(a, b, c); }
void*  memcpy(void* a, const void* b, size_t c)  { return ((void* (*)(void*, const void*, size_t))resolve("memcpy"))(a, b, c); }
void*  memmove(void* a, const void* b, size_t c) { return ((void* (*)(void*, const void*, size_t))resolve("memmove"))(a, b, c); }
void*  memchr(const void* a, int b, size_t c)    { return ((void* (*)(const void*, int, size_t))resolve("memchr"))(a, b, c); }
int    memcmp(const void* a, const void* b, size_t c) { return ((int (*)(const void*, const void*, size_t))resolve("memcmp"))(a, b, c); }
void*  memmem(const void* a, size_t b, const void* c, size_t d) { return ((void* (*)(const void*, size_t, const void*, size_t))resolve("memmem"))(a, b, c, d); }
char*  strdup(const char* a)             { return ((char* (*)(const char*))resolve("strdup"))(a); }
size_t strlen(const char* a)             { return ((size_t (*)(const char*))resolve("strlen"))(a); }
char*  strcpy(char* a, const char* b)    { return ((char* (*)(char*, const char*))resolve("strcpy"))(a, b); }
char*  strncpy(char* a, const char* b, size_t c) { return ((char* (*)(char*, const char*, size_t))resolve("strncpy"))(a, b, c); }
char*  strcat(char* a, const char* b)    { return ((char* (*)(char*, const char*))resolve("strcat"))(a, b); }
int    strcmp(const char* a, const char* b) { return ((int (*)(const char*, const char*))resolve("strcmp"))(a, b); }
int    strncmp(const char* a, const char* b, size_t c) { return ((int (*)(const char*, const char*, size_t))resolve("strncmp"))(a, b, c); }
int    strcasecmp(const char* a, const char* b)  { return ((int (*)(const char*, const char*))resolve("strcasecmp"))(a, b); }
int    strncasecmp(const char* a, const char* b, size_t c) { return ((int (*)(const char*, const char*, size_t))resolve("strncasecmp"))(a, b, c); }
char*  strchr(const char* a, int b)      { return ((char* (*)(const char*, int))resolve("strchr"))(a, b); }
char*  strrchr(const char* a, int b)     { return ((char* (*)(const char*, int))resolve("strrchr"))(a, b); }
long   strtol(const char* a, char** b, int c)    { return ((long (*)(const char*, char**, int))resolve("strtol"))(a, b, c); }
unsigned long strtoul(const char* a, char** b, int c) { return ((unsigned long (*)(const char*, char**, int))resolve("strtoul"))(a, b, c); }
char*  strerror(int a)                   { return ((char* (*)(int))resolve("strerror"))(a); }
int    remove(const char* a)             { return ((int (*)(const char*))resolve("remove"))(a); }
int    rename(const char* a, const char* b)     { return ((int (*)(const char*, const char*))resolve("rename"))(a, b); }
int    chmod(const char* a, mode_t b)    { return ((int (*)(const char*, mode_t))resolve("chmod"))(a, b); }
int    chown(const char* a, uid_t b, gid_t c)   { return ((int (*)(const char*, uid_t, gid_t))resolve("chown"))(a, b, c); }
int    unlink(const char* a)             { return ((int (*)(const char*))resolve("unlink"))(a); }
int    utime(const char* a, const struct utimbuf* b) { return ((int (*)(const char*, const struct utimbuf*))resolve("utime"))(a, b); }

int    fsync(int a)                      { return ((int (*)(int))resolve("fsync"))(a); }
int    open(const char* a, int b, ...)   { va_list ap; va_start(ap, b); int mode = va_arg(ap, int); va_end(ap); return ((int (*)(const char*, int, int))resolve("open"))(a, b, mode); }
int    close(int a)                      { return ((int (*)(int))resolve("close"))(a); }
ssize_t read(int a, void* b, size_t c)   { return ((ssize_t (*)(int, void*, size_t))resolve("read"))(a, b, c); }
ssize_t write(int a, const void* b, size_t c) { return ((ssize_t (*)(int, const void*, size_t))resolve("write"))(a, b, c); }
int    fcntl(int a, int b, ...)          { va_list ap; va_start(ap, b); long c = va_arg(ap, long); va_end(ap); return ((int (*)(int, int, long))resolve("fcntl"))(a, b, c); }
int    sigaction(int a, const struct sigaction* b, struct sigaction* c) { return ((int (*)(int, const struct sigaction*, struct sigaction*))resolve("sigaction"))(a, b, c); }
int    sigprocmask(int a, const sigset_t* b, sigset_t* c) { return ((int (*)(int, const sigset_t*, sigset_t*))resolve("sigprocmask"))(a, b, c); }
struct tm* localtime(const time_t* a)    { return ((struct tm* (*)(const time_t*))resolve("localtime"))(a); }
time_t time(time_t* a)                   { return ((time_t (*)(time_t*))resolve("time"))(a); }
int    mkstemp(char* a)                  { return ((int (*)(char*))resolve("mkstemp"))(a); }
long   sysconf(int a)                    { return ((long (*)(int))resolve("sysconf"))(a); }
int    uname(struct utsname* a)          { return ((int (*)(struct utsname*))resolve("uname"))(a); }
int    gettimeofday(struct timeval* a, void* b) { return ((int (*)(struct timeval*, void*))resolve("gettimeofday"))(a, b); }
/*
 * dlsym — must NOT forward through resolve()/dlsym, because internal calls
 * in this library bind to our own exported symbols (self-interposition),
 * which would recurse forever.  Resolve the real glibc dlsym via dlvsym.
 */
void* dlsym(void* handle, const char* name) {
    static void* (*real)(void*, const char*) = NULL;
    if (real == NULL) {
        void* h = dlvsym(RTLD_NEXT, "dlsym", "GLIBC_2.34");
        if (h == NULL) h = dlvsym(RTLD_NEXT, "dlsym", "GLIBC_2.4");
        if (h == NULL) { dprintf(2, "[bionic-shim] no real dlsym\n"); abort(); }
        real = (void* (*)(void*, const char*))h;
    }
    return real(handle, name);
}
size_t wcslen(const wchar_t* a)          { return ((size_t (*)(const wchar_t*))resolve("wcslen"))(a); }
int    usleep(useconds_t a)              { return ((int (*)(useconds_t))resolve("usleep"))(a); }

SF double floor(double a)   { return ((HF double (*)(double))resolve("floor"))(a); }
SF double ceil(double a)    { return ((HF double (*)(double))resolve("ceil"))(a); }
SF double exp(double a)     { return ((HF double (*)(double))resolve("exp"))(a); }
SF double log(double a)     { return ((HF double (*)(double))resolve("log"))(a); }
SF double log10(double a)   { return ((HF double (*)(double))resolve("log10"))(a); }
SF float  log10f(float a)   { return ((HF float (*)(float))resolve("log10f"))(a); }
SF double ldexp(double a, int b) { return ((HF double (*)(double, int))resolve("ldexp"))(a, b); }
SF double frexp(double a, int* b) { return ((HF double (*)(double, int*))resolve("frexp"))(a, b); }
SF double sqrt(double a)    { return ((HF double (*)(double))resolve("sqrt"))(a); }
SF double sin(double a)     { return ((HF double (*)(double))resolve("sin"))(a); }
SF double cos(double a)     { return ((HF double (*)(double))resolve("cos"))(a); }
SF double tan(double a)     { return ((HF double (*)(double))resolve("tan"))(a); }
SF double atan2(double a, double b) { return ((HF double (*)(double, double))resolve("atan2"))(a, b); }
SF double acos(double a)    { return ((HF double (*)(double))resolve("acos"))(a); }
SF double pow(double a, double b) { return ((HF double (*)(double, double))resolve("pow"))(a, b); }
SF double rint(double a)    { return ((HF double (*)(double))resolve("rint"))(a); }
SF float  sqrtf(float a)    { return ((HF float (*)(float))resolve("sqrtf"))(a); }
SF float  sinf(float a)     { return ((HF float (*)(float))resolve("sinf"))(a); }
SF float  cosf(float a)     { return ((HF float (*)(float))resolve("cosf"))(a); }
SF float  tanf(float a)     { return ((HF float (*)(float))resolve("tanf"))(a); }
SF float  atanf(float a)    { return ((HF float (*)(float))resolve("atanf"))(a); }
SF float  acosf(float a)    { return ((HF float (*)(float))resolve("acosf"))(a); }
SF float  powf(float a, float b) { return ((HF float (*)(float, float))resolve("powf"))(a, b); }
SF long   lrintf(float a)   { return ((HF long (*)(float))resolve("lrintf"))(a); }

int socket(int a, int b, int c) { return ((int (*)(int, int, int))resolve("socket"))(a, b, c); }
int bind(int a, const struct sockaddr* b, socklen_t c) { return ((int (*)(int, const struct sockaddr*, socklen_t))resolve("bind"))(a, b, c); }
int connect(int a, const struct sockaddr* b, socklen_t c) { return ((int (*)(int, const struct sockaddr*, socklen_t))resolve("connect"))(a, b, c); }
int listen(int a, int b) { return ((int (*)(int, int))resolve("listen"))(a, b); }
int accept(int a, struct sockaddr* b, socklen_t* c) { return ((int (*)(int, struct sockaddr*, socklen_t*))resolve("accept"))(a, b, c); }
ssize_t send(int a, const void* b, size_t c, int d) { return ((ssize_t (*)(int, const void*, size_t, int))resolve("send"))(a, b, c, d); }
ssize_t recv(int a, void* b, size_t c, int d) { return ((ssize_t (*)(int, void*, size_t, int))resolve("recv"))(a, b, c, d); }
int setsockopt(int a, int b, int c, const void* d, socklen_t e) { return ((int (*)(int, int, int, const void*, socklen_t))resolve("setsockopt"))(a, b, c, d, e); }
int select(int a, fd_set* b, fd_set* c, fd_set* d, struct timeval* e) { return ((int (*)(int, fd_set*, fd_set*, fd_set*, struct timeval*))resolve("select"))(a, b, c, d, e); }
struct hostent* gethostbyname(const char* a) { return ((struct hostent* (*)(const char*))resolve("gethostbyname"))(a); }
in_addr_t inet_addr(const char* a) { return ((in_addr_t (*)(const char*))resolve("inet_addr"))(a); }

void* pthread_getspecific(pthread_key_t a) { return ((void* (*)(pthread_key_t))resolve("pthread_getspecific"))(a); }
int pthread_key_delete(pthread_key_t a) { return ((int (*)(pthread_key_t))resolve("pthread_key_delete"))(a); }
int pthread_setspecific(pthread_key_t a, const void* b) { return ((int (*)(pthread_key_t, const void*))resolve("pthread_setspecific"))(a, b); }
