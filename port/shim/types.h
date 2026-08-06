/*
 * types.h — manual glibc type for sem_t.
 *
 * All pthread types (pthread_t, pthread_key_t, pthread_mutex_t, ...) are
 * already provided by glibc's <bits/pthreadtypes.h> which is pulled in by
 * <sys/types.h>.  We must NOT include <pthread.h>/<semaphore.h> because
 * those declare the *functions* we redefine and attach the GLIBC_2.4
 * version to them, which blocks our LIBC-versioned exports.
 *
 * sem_t is NOT in sys/types.h, so define it here (size verified on the
 * device's armhf glibc: 16 bytes).
 */
#ifndef SHIM_TYPES_H
#define SHIM_TYPES_H

typedef struct { char _s[16]; } sem_t;

#endif
