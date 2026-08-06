/*
 * abi.c — bionic<->glibc ABI translation layer for the GOF2HD port.
 *
 * The engine was compiled against bionic (NDK r18b) headers.  Several libc
 * types/sizes differ from glibc, so forwarding directly corrupts memory.
 * Here we translate the affected calls:
 *   - off_t is 64-bit on bionic (even 32-bit ARM): fseeko/ftello/lseek/
 *     ftruncate/mmap must use the glibc 64-bit variants.
 *   - struct stat (bionic 96 bytes) vs glibc stat64 (104 bytes).
 *   - pthread_mutex_t: bionic 4 bytes vs glibc 24.
 *   - pthread_cond_t: bionic 4 bytes vs glibc 48.
 *   - sem_t: bionic 4 bytes vs glibc 16.
 *   - pthread_attr_t: bionic 24 bytes vs glibc 36.
 *   - struct tm: bionic 36 bytes vs glibc 44 (mktime).
 *   - struct statfs matches glibc statfs64 (88 bytes) exactly.
 * sigaction (140) and struct utsname (390) match, so they forward directly.
 */
#define _GNU_SOURCE
#define _LARGEFILE64_SOURCE
#define _FILE_OFFSET_BITS 64

/* rename glibc declarations away so our bionic-signature definitions win */
#define stat            abi_hidden_stat
#define fstat           abi_hidden_fstat
#define statfs          abi_hidden_statfs
#define fseeko          abi_hidden_fseeko
#define ftello          abi_hidden_ftello
#define lseek           abi_hidden_lseek
#define ftruncate       abi_hidden_ftruncate
#define mmap            abi_hidden_mmap
#define mktime          abi_hidden_mkt

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <time.h>

#undef stat
#undef fstat
#undef statfs
#undef fseeko
#undef ftello
#undef lseek
#undef ftruncate
#undef mmap
#undef mktime

void* resolve(const char* n);
#include "types.h"

/* ============================= bionic types ============================= */

struct bionic_stat {
    uint64_t st_dev;
    unsigned char __pad0[4];
    uint32_t __st_ino;
    uint32_t st_mode;
    uint32_t st_nlink;
    uint32_t st_uid;
    uint32_t st_gid;
    uint64_t st_rdev;
    unsigned char __pad3[4];
    int64_t st_size;
    uint32_t st_blksize;
    uint64_t st_blocks;
    struct timespec st_atim;
    struct timespec st_mtim;
    struct timespec st_ctim;
    uint64_t st_ino;
};

struct bionic_attr {
    int flags;
    void* stack_base;
    size_t stack_size;
    size_t guard_size;
    int sched_policy;
    int sched_priority;
};

#define BIONIC_ATTR_FLAG_DETACHED 0x00000001

/* ==================== stat / fstat (bionic struct) ==================== */

static void stat_to_bionic(const struct stat64* gs, struct bionic_stat* bs) {
    memset(bs, 0, sizeof(*bs));
    bs->st_dev = gs->st_dev;
    bs->__st_ino = gs->st_ino;
    bs->st_mode = gs->st_mode;
    bs->st_nlink = gs->st_nlink;
    bs->st_uid = gs->st_uid;
    bs->st_gid = gs->st_gid;
    bs->st_rdev = gs->st_rdev;
    bs->st_size = gs->st_size;
    bs->st_blksize = gs->st_blksize;
    bs->st_blocks = gs->st_blocks;
    bs->st_atim = gs->st_atim;
    bs->st_mtim = gs->st_mtim;
    bs->st_ctim = gs->st_ctim;
    bs->st_ino = gs->st_ino;
}

int stat(const char* path, struct bionic_stat* bs) {
    static int (*f)(const char*, struct stat64*);
    if (!f) f = (int (*)(const char*, struct stat64*))resolve("stat64");
    struct stat64 gs;
    int r = f(path, &gs);
    if (r == 0) {
        stat_to_bionic(&gs, bs);
        fprintf(stderr, "[shim-stat] %s size=%lld mode=%o\n", path,
                (long long)gs.st_size, (unsigned)gs.st_mode);
    }
    return r;
}

int fstat(int fd, struct bionic_stat* bs) {
    static int (*f)(int, struct stat64*);
    if (!f) f = (int (*)(int, struct stat64*))resolve("fstat64");
    struct stat64 gs;
    int r = f(fd, &gs);
    if (r == 0) stat_to_bionic(&gs, bs);
    return r;
}

int statfs(const char* path, struct statfs* buf) {
    /* glibc statfs64 layout (88B) == bionic statfs layout. */
    static int (*f)(const char*, struct statfs64*);
    if (!f) f = (int (*)(const char*, struct statfs64*))resolve("statfs64");
    return f(path, (struct statfs64*)buf);
}

/* ========================= 64-bit off_t calls ========================= */

off64_t lseek(int fd, off64_t off, int whence) {
    static off64_t (*fn)(int, off64_t, int);
    if (!fn) fn = (off64_t (*)(int, off64_t, int))resolve("lseek64");
    return fn(fd, off, whence);
}

int ftruncate(int fd, off64_t len) {
    static int (*fn)(int, off64_t);
    if (!fn) fn = (int (*)(int, off64_t))resolve("ftruncate64");
    return fn(fd, len);
}

void* mmap(void* addr, size_t len, int prot, int flags, int fd, off64_t off) {
    static void* (*fn)(void*, size_t, int, int, int, off64_t);
    if (!fn) fn = (void* (*)(void*, size_t, int, int, int, off64_t))resolve("mmap64");
    return fn(addr, len, prot, flags, fd, off);
}

/* ============ bionic 4-byte pthread_mutex_t / cond / sem ============ */

static pthread_mutex_t g_reg_lock_mem;
static pthread_mutex_t* g_reg_lock = NULL;
static void reg_lock(void) {
    static int (*f)(pthread_mutex_t*);
    static int (*fi)(pthread_mutex_t*, const pthread_mutexattr_t*);
    if (!g_reg_lock) {
        g_reg_lock = &g_reg_lock_mem;
        if (!fi) fi = (int (*)(pthread_mutex_t*, const pthread_mutexattr_t*))resolve("pthread_mutex_init");
        fi(g_reg_lock, NULL);
    }
    if (!f) f = (int (*)(pthread_mutex_t*))resolve("pthread_mutex_lock");
    f(g_reg_lock);
}
static void reg_unlock(void) {
    static int (*f)(pthread_mutex_t*);
    if (!f) f = (int (*)(pthread_mutex_t*))resolve("pthread_mutex_unlock");
    if (g_reg_lock) f(g_reg_lock);
}

#define REG_SIZE 16384
static struct { const void* key; void* val; } g_map[REG_SIZE];

static unsigned reg_hash(const void* p) {
    return (unsigned)(((uintptr_t)p >> 2) ^ ((uintptr_t)p >> 14)) & (REG_SIZE - 1);
}
static void reg_put(const void* key, void* val) {
    unsigned h = reg_hash(key);
    while (g_map[h].key != NULL) h = (h + 1) & (REG_SIZE - 1);
    g_map[h].key = key;
    g_map[h].val = val;
}
static void* reg_get(const void* key) {
    unsigned h = reg_hash(key);
    while (g_map[h].key != NULL) {
        if (g_map[h].key == key) return g_map[h].val;
        h = (h + 1) & (REG_SIZE - 1);
    }
    return NULL;
}

static void glibc_mutex_init(pthread_mutex_t* m) {
    static int (*f)(pthread_mutex_t*, const pthread_mutexattr_t*);
    if (!f) f = (int (*)(pthread_mutex_t*, const pthread_mutexattr_t*))resolve("pthread_mutex_init");
    f(m, NULL);
}
static void glibc_mutex_lock(pthread_mutex_t* m) {
    static int (*f)(pthread_mutex_t*);
    if (!f) f = (int (*)(pthread_mutex_t*))resolve("pthread_mutex_lock");
    f(m);
}
static void glibc_mutex_unlock(pthread_mutex_t* m) {
    static int (*f)(pthread_mutex_t*);
    if (!f) f = (int (*)(pthread_mutex_t*))resolve("pthread_mutex_unlock");
    f(m);
}

static pthread_mutex_t* get_mutex(const void* key, int create) {
    pthread_mutex_t* m = reg_get(key);
    if (m) return m;
    if (!create) return NULL;
    m = malloc(sizeof(pthread_mutex_t));
    glibc_mutex_init(m);
    reg_lock();
    reg_put(key, m);
    reg_unlock();
    return m;
}

static pthread_cond_t* get_cond(const void* key, int create) {
    pthread_cond_t* c = reg_get(key);
    if (c) return c;
    if (!create) return NULL;
    c = malloc(sizeof(pthread_cond_t));
    {
        static int (*f)(pthread_cond_t*, const pthread_condattr_t*);
        if (!f) f = (int (*)(pthread_cond_t*, const pthread_condattr_t*))resolve("pthread_cond_init");
        f(c, NULL);
    }
    reg_lock();
    reg_put(key, c);
    reg_unlock();
    return c;
}

static sem_t* get_sem(const void* key, int create) {
    sem_t* s = reg_get(key);
    if (s) return s;
    if (!create) return NULL;
    s = malloc(sizeof(sem_t));
    reg_lock();
    reg_put(key, s);
    reg_unlock();
    return s;
}

/* ---- pthread_mutex ---- */

int pthread_mutex_init(void* bm, const int* battr) {
    pthread_mutex_t* m = malloc(sizeof(pthread_mutex_t));
    int type = battr ? *battr : 0;
    /* bionic: 0=normal 1=recursive 2=errorcheck; glibc: 0=normal 1=errorcheck 2=recursive */
    int gtype = (type == 1) ? 2 : (type == 2 ? 1 : 0);
    if (gtype == 0) {
        glibc_mutex_init(m);
    } else {
        static int (*f)(pthread_mutex_t*, const pthread_mutexattr_t*);
        static int (*ai)(pthread_mutexattr_t*);
        static int (*st)(pthread_mutexattr_t*, int);
        if (!f) f = (int (*)(pthread_mutex_t*, const pthread_mutexattr_t*))resolve("pthread_mutex_init");
        if (!ai) ai = (int (*)(pthread_mutexattr_t*))resolve("pthread_mutexattr_init");
        if (!st) st = (int (*)(pthread_mutexattr_t*, int))resolve("pthread_mutexattr_settype");
        pthread_mutexattr_t ga;
        ai(&ga);
        st(&ga, gtype);
        f(m, &ga);
    }
    reg_lock();
    reg_put(bm, m);
    reg_unlock();
    return 0;
}

int pthread_mutex_lock(void* bm) {
    glibc_mutex_lock(get_mutex(bm, 1));
    return 0;
}
int pthread_mutex_unlock(void* bm) {
    glibc_mutex_unlock(get_mutex(bm, 1));
    return 0;
}
int pthread_mutex_destroy(void* bm) {
    pthread_mutex_t* m = get_mutex(bm, 0);
    if (m) {
        static int (*f)(pthread_mutex_t*);
        if (!f) f = (int (*)(pthread_mutex_t*))resolve("pthread_mutex_destroy");
        f(m);
    }
    return 0;
}

/* ---- pthread_cond ---- */

int pthread_cond_broadcast(void* bc) {
    pthread_cond_t* c = get_cond(bc, 1);
    static int (*f)(pthread_cond_t*);
    if (!f) f = (int (*)(pthread_cond_t*))resolve("pthread_cond_broadcast");
    return f(c);
}

int pthread_cond_wait(void* bc, void* bm) {
    pthread_cond_t* c = get_cond(bc, 1);
    pthread_mutex_t* m = get_mutex(bm, 1);
    static int (*f)(pthread_cond_t*, pthread_mutex_t*);
    if (!f) f = (int (*)(pthread_cond_t*, pthread_mutex_t*))resolve("pthread_cond_wait");
    return f(c, m);
}

/* ---- sem ---- */

int sem_init(void* bs, int pshared, unsigned value) {
    sem_t* s = malloc(sizeof(sem_t));
    static int (*f)(sem_t*, int, unsigned);
    if (!f) f = (int (*)(sem_t*, int, unsigned))resolve("sem_init");
    int r = f(s, pshared, value);
    reg_lock();
    reg_put(bs, s);
    reg_unlock();
    return r;
}
int sem_wait(void* bs) {
    static int (*f)(sem_t*);
    if (!f) f = (int (*)(sem_t*))resolve("sem_wait");
    return f(get_sem(bs, 1));
}
int sem_post(void* bs) {
    static int (*f)(sem_t*);
    if (!f) f = (int (*)(sem_t*))resolve("sem_post");
    return f(get_sem(bs, 1));
}
int sem_destroy(void* bs) {
    sem_t* s = get_sem(bs, 0);
    if (s) {
        static int (*f)(sem_t*);
        if (!f) f = (int (*)(sem_t*))resolve("sem_destroy");
        f(s);
    }
    return 0;
}

/* ==================== pthread_attr (bionic 24B) ==================== */

int pthread_attr_init(struct bionic_attr* a) {
    a->flags = 0;
    a->stack_base = NULL;
    a->stack_size = 0;
    a->guard_size = 0;
    a->sched_policy = 0;
    a->sched_priority = 0;
    return 0;
}
int pthread_attr_destroy(struct bionic_attr* a) {
    memset(a, 0x42, sizeof(*a));
    return 0;
}
int pthread_attr_setdetachstate(struct bionic_attr* a, int state) {
    if (state == 1) a->flags |= BIONIC_ATTR_FLAG_DETACHED;      /* PTHREAD_CREATE_DETACHED */
    else a->flags &= ~BIONIC_ATTR_FLAG_DETACHED;
    return 0;
}
int pthread_attr_setstacksize(struct bionic_attr* a, size_t sz) {
    a->stack_size = sz;
    return 0;
}

int pthread_create(void* bthread, const struct bionic_attr* battr,
                   void* (*start)(void*), void* arg) {
    static int (*f)(pthread_t*, const pthread_attr_t*, void* (*)(void*), void*);
    if (!f) f = (int (*)(pthread_t*, const pthread_attr_t*, void* (*)(void*), void*))resolve("pthread_create");
    pthread_attr_t ga;
    pthread_attr_t* gaptr = NULL;
    if (battr) {
        static int (*ai)(pthread_attr_t*);
        static int (*sd)(pthread_attr_t*, int);
        static int (*ss)(pthread_attr_t*, size_t);
        static int (*sg)(pthread_attr_t*, size_t);
        if (!ai) ai = (int (*)(pthread_attr_t*))resolve("pthread_attr_init");
        if (!sd) sd = (int (*)(pthread_attr_t*, int))resolve("pthread_attr_setdetachstate");
        if (!ss) ss = (int (*)(pthread_attr_t*, size_t))resolve("pthread_attr_setstacksize");
        if (!sg) sg = (int (*)(pthread_attr_t*, size_t))resolve("pthread_attr_setguardsize");
        ai(&ga);
        if (battr->flags & BIONIC_ATTR_FLAG_DETACHED) sd(&ga, 1);
        if (battr->stack_size) ss(&ga, battr->stack_size);
        if (battr->guard_size) sg(&ga, battr->guard_size);
        gaptr = &ga;
    }
    return f((pthread_t*)bthread, gaptr, start, arg);
}

/* ==================== mktime (struct tm 36 vs 44) ==================== */

time_t mktime(struct tm* bt) {
    static time_t (*f)(struct tm*);
    if (!f) f = (time_t (*)(struct tm*))resolve("mktime");
    struct tm gt;
    memcpy(&gt, bt, 9 * sizeof(int));
    gt.tm_gmtoff = 0;
    gt.tm_zone = NULL;
    time_t r = f(&gt);
    memcpy(bt, &gt, 9 * sizeof(int));
    return r;
}
