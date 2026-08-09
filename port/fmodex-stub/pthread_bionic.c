/*
 * pthread_bionic.so — translate bionic pthread/sem calls to glibc.
 *
 * The Android FMOD libs (and SDL2, and the game host) were built against
 * bionic, where pthread_mutex_t=4B, pthread_cond_t=4B, sem_t=4B,
 * pthread_attr_t=24B.  glibc's are larger (24/48/16/36).  Calling glibc's
 * pthread_mutex_init on a bionic-sized slot overflows the caller's structs,
 * corrupting FMOD's objects (this manifested as crashes in the opensl path).
 *
 * This library (LD_PRELOADed first) intercepts the bionic-sensitive pthread/
 * sem functions and translates: each bionic mutex/attr/sem address is mapped
 * to a freshly malloc'd glibc object, so glibc never writes into the caller's
 * 4-byte slots.  Other pthread functions (pthread_once, pthread_self, ...)
 * are ABI-compatible and bind straight to glibc.
 */
#define _GNU_SOURCE
#include <pthread.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <semaphore.h>
#include <dlfcn.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>

#define MAX_SLOTS 8192

typedef struct { void *addr; pthread_mutex_t *real; int type; } mut_slot;
typedef struct { void *addr; pthread_attr_t *real; } attr_slot;
typedef struct { void *addr; sem_t *real; } sem_slot;

static mut_slot g_mut[MAX_SLOTS];
static attr_slot g_attr[MAX_SLOTS];
static sem_slot g_sem[MAX_SLOTS];

static mut_slot *find_mut(void *addr) {
    for (int i = 0; i < MAX_SLOTS; i++)
        if (g_mut[i].addr == addr) return &g_mut[i];
    return 0;
}

static attr_slot *find_attr(void *addr) {
    for (int i = 0; i < MAX_SLOTS; i++)
        if (g_attr[i].addr == addr) return &g_attr[i];
    return 0;
}

static int (*R_init)(pthread_mutex_t *, const pthread_mutexattr_t *);
static int (*R_lock)(pthread_mutex_t *);
static int (*R_unlock)(pthread_mutex_t *);
static int (*R_trylock)(pthread_mutex_t *);
static int (*R_destroy)(pthread_mutex_t *);
static int (*R_attrinit)(pthread_mutexattr_t *);
static int (*R_attrsettype)(pthread_mutexattr_t *, int);
static int (*R_attrsetdetach)(pthread_attr_t *, int);
static int (*R_attrsetstack)(pthread_attr_t *, size_t);
static int (*R_attrinit2)(pthread_attr_t *);

static void resolve_all(void) {
    if (R_init) return;
    R_init = dlsym(RTLD_NEXT, "pthread_mutex_init");
    R_lock = dlsym(RTLD_NEXT, "pthread_mutex_lock");
    R_unlock = dlsym(RTLD_NEXT, "pthread_mutex_unlock");
    R_trylock = dlsym(RTLD_NEXT, "pthread_mutex_trylock");
    R_destroy = dlsym(RTLD_NEXT, "pthread_mutex_destroy");
    R_attrinit = dlsym(RTLD_NEXT, "pthread_mutexattr_init");
    R_attrsettype = dlsym(RTLD_NEXT, "pthread_mutexattr_settype");
    R_attrsetdetach = dlsym(RTLD_NEXT, "pthread_attr_setdetachstate");
    R_attrsetstack = dlsym(RTLD_NEXT, "pthread_attr_setstacksize");
    R_attrinit2 = dlsym(RTLD_NEXT, "pthread_attr_init");
}

static mut_slot *alloc_mut(void *addr) {
    mut_slot *s = find_mut(addr);
    if (s) return s;
    for (int i = 0; i < MAX_SLOTS; i++) {
        if (!g_mut[i].addr) {
            g_mut[i].addr = addr;
            g_mut[i].real = malloc(sizeof(pthread_mutex_t));
            g_mut[i].type = PTHREAD_MUTEX_NORMAL;
            resolve_all();
            R_init(g_mut[i].real, NULL);
            return &g_mut[i];
        }
    }
    return NULL;
}

static attr_slot *alloc_attr(void *addr) {
    attr_slot *s = find_attr(addr);
    if (s) return s;
    for (int i = 0; i < MAX_SLOTS; i++) {
        if (!g_attr[i].addr) {
            g_attr[i].addr = addr;
            g_attr[i].real = malloc(sizeof(pthread_attr_t));
            resolve_all();
            R_attrinit2(g_attr[i].real);
            return &g_attr[i];
        }
    }
    return NULL;
}

int pthread_mutex_init(pthread_mutex_t *m, const pthread_mutexattr_t *a) {
    mut_slot *s = alloc_mut(m);
    if (!s) return -1;
    if (a) {
        int type = *(const int *)a;
        s->type = type;
        resolve_all();
        pthread_mutexattr_t ra;
        R_attrinit(&ra);
        R_attrsettype(&ra, type);
        R_destroy(s->real);
        R_init(s->real, &ra);
    }
    return 0;
}

int pthread_mutex_destroy(pthread_mutex_t *m) {
    mut_slot *s = find_mut(m);
    if (!s) return 0;
    resolve_all();
    R_destroy(s->real);
    return 0;
}

int pthread_mutex_lock(pthread_mutex_t *m) {
    mut_slot *s = alloc_mut(m);
    if (!s) return -1;
    resolve_all();
    return R_lock(s->real);
}

int pthread_mutex_unlock(pthread_mutex_t *m) {
    mut_slot *s = find_mut(m);
    if (!s) return -1;
    resolve_all();
    return R_unlock(s->real);
}

int pthread_mutex_trylock(pthread_mutex_t *m) {
    mut_slot *s = alloc_mut(m);
    if (!s) return -1;
    resolve_all();
    return R_trylock(s->real);
}

int pthread_mutexattr_init(pthread_mutexattr_t *a) {
    *(int *)a = PTHREAD_MUTEX_NORMAL;
    return 0;
}

int pthread_mutexattr_settype(pthread_mutexattr_t *a, int t) {
    *(int *)a = t;
    return 0;
}

int pthread_mutexattr_destroy(pthread_mutexattr_t *a) {
    return 0;
}

int pthread_attr_init(pthread_attr_t *a) {
    alloc_attr(a);
    return 0;
}

int pthread_attr_destroy(pthread_attr_t *a) {
    return 0;
}

int pthread_attr_setdetachstate(pthread_attr_t *a, int s) {
    attr_slot *sl = alloc_attr(a);
    if (!sl) return -1;
    resolve_all();
    return R_attrsetdetach(sl->real, s);
}

int pthread_attr_setstacksize(pthread_attr_t *a, size_t sz) {
    attr_slot *sl = alloc_attr(a);
    if (!sl) return -1;
    resolve_all();
    return R_attrsetstack(sl->real, sz);
}

static sem_slot *find_sem(void *addr) {
    for (int i = 0; i < MAX_SLOTS; i++)
        if (g_sem[i].addr == addr) return &g_sem[i];
    return 0;
}

static sem_slot *alloc_sem(void *addr) {
    sem_slot *s = find_sem(addr);
    if (s) return s;
    for (int i = 0; i < MAX_SLOTS; i++) {
        if (!g_sem[i].addr) {
            g_sem[i].addr = addr;
            g_sem[i].real = malloc(sizeof(sem_t));
            return &g_sem[i];
        }
    }
    return NULL;
}

static int (*R_sem_init)(sem_t *, int, unsigned int);
static int (*R_sem_destroy)(sem_t *);
static int (*R_sem_post)(sem_t *);
static int (*R_sem_wait)(sem_t *);
static int (*R_sem_trywait)(sem_t *);
static int (*R_sem_timedwait)(sem_t *, const struct timespec *);
static void resolve_sem(void) {
    if (R_sem_init) return;
    R_sem_init = dlsym(RTLD_NEXT, "sem_init");
    R_sem_destroy = dlsym(RTLD_NEXT, "sem_destroy");
    R_sem_post = dlsym(RTLD_NEXT, "sem_post");
    R_sem_wait = dlsym(RTLD_NEXT, "sem_wait");
    R_sem_trywait = dlsym(RTLD_NEXT, "sem_trywait");
    R_sem_timedwait = dlsym(RTLD_NEXT, "sem_timedwait");
}

int sem_init(sem_t *s, int pshared, unsigned int value) {
    resolve_sem();
    sem_slot *sl = alloc_sem(s);
    if (!sl) return -1;
    return R_sem_init(sl->real, pshared, value);
}

int sem_destroy(sem_t *s) {
    resolve_sem();
    sem_slot *sl = find_sem(s);
    if (!sl) return -1;
    return R_sem_destroy(sl->real);
}

int sem_post(sem_t *s) {
    resolve_sem();
    sem_slot *sl = find_sem(s);
    if (!sl) return -1;
    return R_sem_post(sl->real);
}

int sem_wait(sem_t *s) {
    resolve_sem();
    sem_slot *sl = find_sem(s);
    if (!sl) return -1;
    return R_sem_wait(sl->real);
}

int sem_trywait(sem_t *s) {
    resolve_sem();
    sem_slot *sl = find_sem(s);
    if (!sl) return -1;
    return R_sem_trywait(sl->real);
}

int sem_timedwait(sem_t *s, const struct timespec *t) {
    resolve_sem();
    sem_slot *sl = find_sem(s);
    if (!sl) return -1;
    return R_sem_timedwait(sl->real, t);
}

int pthread_create(pthread_t *tid, const pthread_attr_t *a, void *(*fn)(void *), void *arg) {
    static int (*real)(pthread_t *, const pthread_attr_t *, void *(*)(void *), void *);
    if (!real) real = dlsym(RTLD_NEXT, "pthread_create");
    pthread_attr_t *ra = NULL;
    if (a) {
        attr_slot *sl = find_attr((void *)a);
        ra = sl ? sl->real : NULL;
    }
    return real(tid, ra, fn, arg);
}
