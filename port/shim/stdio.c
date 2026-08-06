/*
 * stdio.c — bionic-compatible stdio for the GOF2HD shim.
 *
 * The engine was built with NDK r18b for Android API < M, so bionic's
 * `FILE` is the opaque struct
 *     struct __sFILE { char __private[84]; } __attribute__((aligned(4)));
 * and the std streams are `__sF[i]` (i = 0 stdin, 1 stdout, 2 stderr);
 * fopen() allocates new streams from the same __sF pool (indices >= 3),
 * bionic's classic __sfp() scheme.
 *
 * glibc's FILE layout/vtables are incompatible, and its vtable validation
 * kills the engine ("glibc detected an invalid stdio handle").  Therefore
 * we implement stdio completely on raw syscalls (Box64 wrappedlibc style):
 *    - every FILE* is a pointer into our __sF pool, stride 84, exactly as
 *      the engine's compiled-in &__sF[i] arithmetic expects;
 *    - no glibc FILE is ever constructed;
 *    - printf-family formats via glibc vsnprintf (FILE-free) and write
 *      through our fwrite;
 *    - variadic functions are exported with pcs("aapcs") (softfp entry)
 *      because the engine is armel; float-only-free functions need no
 *      wrapper (register usage is identical in both ABIs).
 */
#define _GNU_SOURCE
#include <stddef.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdint.h>

#define SF __attribute__((pcs("aapcs")))

#ifndef EOF
#define EOF (-1)
#endif

void* resolve(const char* n);               /* shim.c */

typedef struct { char __private[84]; } __attribute__((aligned(4))) FILE;

#define FOPEN_MAX 20
#define RBUF      131072
#define WBUF      16384

enum { F_ERR = 1, F_EOF = 2, F_UNBUF = 4, F_RD = 8, F_WR = 16 };

FILE __sF[FOPEN_MAX];

struct GStream {
    int          used;
    int          fd;
    unsigned     flags;
    long         pos;                 /* logical byte offset */
    unsigned char* rbuf; unsigned int rlen, rpos;
    unsigned char* wbuf; unsigned int wpos, wcap;
    unsigned char pb[4]; unsigned int npb;
};

static struct GStream ST[FOPEN_MAX];

#define stdin_   (&__sF[0])
#define stdout_  (&__sF[1])
#define stderr_  (&__sF[2])

__attribute__((constructor)) static void stdio_init(void) {
    ST[0].used = 1; ST[0].fd = 0; ST[0].flags = F_RD  | F_UNBUF;
    ST[1].used = 1; ST[1].fd = 1; ST[1].flags = F_WR  | F_UNBUF;
    ST[2].used = 1; ST[2].fd = 2; ST[2].flags = F_WR  | F_UNBUF;
}

/* ---------------- helpers ---------------- */

static int ftrace_on(void) { static int v = -1; if (v < 0) v = getenv("GOF_TRACE") && atoi(getenv("GOF_TRACE")) >= 2; return v; }

static void bad_fp(const char* fn, const void* fp) {
    dprintf(2, "[stdio] %s: FILE* %p not in __sF pool\n", fn, fp);
}

static int slot_of(const void* fp) {
    long d = (const char*)fp - (const char*)__sF;
    if ((unsigned long)d >= (unsigned long)sizeof(__sF)) return -1;
    if (d % (long)sizeof(FILE)) return -1;
    return (int)(d / (long)sizeof(FILE));
}

static ssize_t rd_raw(int fd, void* b, size_t n) {
    return ((ssize_t (*)(int, void*, size_t))resolve("read"))(fd, b, n);
}
static ssize_t wr_raw(int fd, const void* b, size_t n) {
    return ((ssize_t (*)(int, const void*, size_t))resolve("write"))(fd, b, n);
}
static long long lsk_raw(int fd, long long off, int whence) {
    return ((long long (*)(int, long long, int))resolve("lseek64"))(fd, off, whence);
}

static int flushw(struct GStream* st) {
    while (st->wpos) {
        ssize_t w = wr_raw(st->fd, st->wbuf, st->wpos);
        if (w <= 0) { st->flags |= F_ERR; return -1; }
        memmove(st->wbuf, st->wbuf + w, st->wpos - (unsigned int)w);
        st->wpos -= (unsigned int)w;
    }
    return 0;
}

static int parse_mode(const char* m, int* rd, int* wr) {
    int app = 0;
    *rd = *wr = 0;
    switch (*m++) {
        case 'r': *rd = 1; break;
        case 'w': *wr = 1; break;
        case 'a': *wr = 1; app = 1; break;
        default:  return -1;
    }
    for (; *m; m++) if (*m == '+') { *rd = *wr = 1; }
    return app ? 1 : 0;   /* 1 == append mode */
}

static FILE* stream_new(int fd, unsigned flags) {
    int s;
    if (fd < 0) return NULL;
    for (s = 3; s < FOPEN_MAX; s++) if (!ST[s].used) break;
    if (s == FOPEN_MAX) { errno = EMFILE; return NULL; }
    ST[s].used = 1; ST[s].fd = fd; ST[s].flags = flags; ST[s].pos = 0;
    return &__sF[s];
}

/* ---------------- open/close ---------------- */

FILE* fopen(const char* path, const char* mode) {
    int rd, wr;
    if (parse_mode(mode, &rd, &wr) < 0) return NULL;
    int flags = (rd && wr) ? O_RDWR : (wr ? O_WRONLY : O_RDONLY);
    if (wr) flags |= O_CREAT;
    if (parse_mode(mode, &rd, &wr) > 0) flags |= O_APPEND;   /* append */
    else if (wr) flags |= O_TRUNC;
    int fd = ((int (*)(const char*, int, int))resolve("open"))(path, flags, 0666);
    if (fd < 0) return NULL;
    FILE* f = stream_new(fd, (rd ? F_RD : 0) | (wr ? F_WR : 0));
    if (!f) ((int (*)(int))resolve("close"))(fd);
    if (ftrace_on()) dprintf(2, "[stdio] fopen(%s,%s)=%p fd=%d\n", path, mode, f, fd);
    return f;
}

FILE* fdopen(int fd, const char* mode) {
    int rd, wr;
    if (parse_mode(mode, &rd, &wr) < 0) return NULL;
    return stream_new(fd, (rd ? F_RD : 0) | (wr ? F_WR : 0));
}

int fclose(FILE* fp) {
    int s = slot_of(fp);
    if (s < 0) { bad_fp("fclose", fp); return -1; }
    struct GStream* st = &ST[s];
    int r = st->wpos ? flushw(st) : 0;
    if (st->fd > 2) {
        if (((int (*)(int))resolve("close"))(st->fd) && !r) r = -1;
    }
    free(st->rbuf); free(st->wbuf);
    memset(st, 0, sizeof(*st));
    return r;
}

int fileno(FILE* fp) {
    int s = slot_of(fp);
    if (s < 0) { bad_fp("fileno", fp); return -1; }
    if (ftrace_on()) dprintf(2, "[stdio] fileno(%p)=%d\n", fp, ST[s].fd);
    return ST[s].fd;
}

/* ---------------- read/write ---------------- */

size_t fread(void* ptr, size_t size, size_t nmemb, FILE* fp) {
    int s = slot_of(fp);
    if (s < 0) { bad_fp("fread", fp); return 0; }
    struct GStream* st = &ST[s];
    if (size == 0 || nmemb == 0) return 0;
    if (!(st->flags & F_RD)) { errno = EBADF; return 0; }
    size_t rv = 0;
    char* d = ptr;
    size_t total = size * nmemb, done = 0;
    while (done < total) {
        if (st->npb) { d[done++] = st->pb[--st->npb]; continue; }
        if (st->rpos >= st->rlen) {
            if (!st->rbuf) {
                st->rbuf = malloc(RBUF);
                if (!st->rbuf) { st->flags |= F_ERR; break; }
            }
            ssize_t n = rd_raw(st->fd, st->rbuf, RBUF);
            if (n < 0) { st->flags |= F_ERR; break; }
            if (n == 0) { st->flags |= F_EOF; break; }
            st->rpos = 0; st->rlen = (unsigned int)n;
        }
        unsigned int take = st->rlen - st->rpos;
        if (take > total - done) take = (unsigned int)(total - done);
        memcpy(d + done, st->rbuf + st->rpos, take);
        st->rpos += take; done += take; st->pos += take;
    }
    rv = done / size;
    if (ftrace_on() && rv != nmemb)
        dprintf(2, "[stdio] fread(%p,%zu,%zu,%p)=%zu (short!)\n", ptr, size, nmemb, fp, rv);
    return rv;
}

size_t fwrite(const void* ptr, size_t size, size_t nmemb, FILE* fp) {
    int s = slot_of(fp);
    if (s < 0) { bad_fp("fwrite", fp); return 0; }
    struct GStream* st = &ST[s];
    if (size == 0 || nmemb == 0) return 0;
    if (!(st->flags & F_WR)) { errno = EBADF; return 0; }
    const char* src = ptr;
    size_t total = size * nmemb, done = 0;
    while (done < total) {
        if (st->flags & F_UNBUF) {
            ssize_t w = wr_raw(st->fd, src + done, total - done);
            if (w <= 0) { st->flags |= F_ERR; break; }
            done += (size_t)w; st->pos += (long)w;
        } else {
            if (!st->wbuf) {
                st->wbuf = malloc(WBUF);
                if (!st->wbuf) { st->flags |= F_ERR; break; }
                st->wcap = WBUF;
            }
            unsigned int take = st->wcap - st->wpos;
            if (take > total - done) take = (unsigned int)(total - done);
            if (take == 0) {
                if (flushw(st)) break;
                continue;
            }
            memcpy(st->wbuf + st->wpos, src + done, take);
            st->wpos += take; done += take; st->pos += (long)take;
        }
    }
    return done / size;
}

int fputc(int c, FILE* fp) {
    return fwrite(&c, 1, 1, fp) == 1 ? (unsigned char)c : EOF;
}

int putc(int c, FILE* fp) {
    return fputc(c, fp);
}

int fputs(const char* s, FILE* fp) {
    size_t n = strlen(s);
    return fwrite(s, 1, n, fp) == n ? 0 : EOF;
}

int puts(const char* s) {
    size_t n = strlen(s);
    if (fwrite(s, 1, n, stdout_) != n) return EOF;
    return fwrite("\n", 1, 1, stdout_) == 1 ? 0 : EOF;
}

/* ---------------- position ---------------- */

static int gseek(struct GStream* st, long off, int whence) {
    if (st->wpos && flushw(st)) return -1;
    long long base;
    if (whence == SEEK_SET) base = 0;
    else if (whence == SEEK_CUR) base = st->pos;
    else if (whence == SEEK_END) {
        long long e = lsk_raw(st->fd, 0, SEEK_END);
        if (e < 0) return -1;
        base = e;
    } else return -1;
    if (lsk_raw(st->fd, base + off, SEEK_SET) < 0) return -1;
    st->pos = base + off;
    st->rpos = st->rlen = 0;
    st->npb = 0;
    st->flags &= ~F_EOF;
    return 0;
}

int fseek(FILE* fp, long off, int whence) {
    int s = slot_of(fp);
    if (s < 0) { bad_fp("fseek", fp); return -1; }
    int r = gseek(&ST[s], off, whence);
    if (ftrace_on()) dprintf(2, "[stdio] fseek(%p,%ld,%d)=%d\n", fp, off, whence, r);
    return r;
}

int fseeko(FILE* fp, long off, int whence) {
    return fseek(fp, off, whence);
}

long ftell(FILE* fp) {
    int s = slot_of(fp);
    if (s < 0) { bad_fp("ftell", fp); return -1; }
    return ST[s].pos;
}

long ftello(FILE* fp) {
    return ftell(fp);
}

/* ---------------- status ---------------- */

void clearerr(FILE* fp) {
    int s = slot_of(fp);
    if (s < 0) { bad_fp("clearerr", fp); return; }
    ST[s].flags &= ~(F_ERR | F_EOF);
}

int ferror(FILE* fp) {
    int s = slot_of(fp);
    if (s < 0) { bad_fp("ferror", fp); return -1; }
    return (ST[s].flags & F_ERR) != 0;
}

/* ---------------- printf family ---------------- */

int vfprintf(FILE* fp, const char* fmt, va_list ap) {
    int s = slot_of(fp);
    if (s < 0) { bad_fp("vfprintf", fp); return -1; }
    va_list aq;
    va_copy(aq, ap);
    int n = ((int (*)(char*, size_t, const char*, va_list))resolve("vsnprintf"))
            (NULL, 0, fmt, aq);
    va_end(aq);
    if (n < 0) return -1;
    char* buf = malloc((size_t)n + 1);
    if (!buf) return -1;
    int r = ((int (*)(char*, size_t, const char*, va_list))resolve("vsnprintf"))
            (buf, (size_t)n + 1, fmt, ap);
    if (r < 0) { free(buf); return -1; }
    size_t w = fwrite(buf, 1, (size_t)r, fp);
    free(buf);
    return w == (size_t)r ? r : -1;
}

int vsnprintf(char* buf, size_t n, const char* fmt, va_list ap) {
    return ((int (*)(char*, size_t, const char*, va_list))resolve("vsnprintf"))
            (buf, n, fmt, ap);
}

int vasprintf(char** sp, const char* fmt, va_list ap) {
    return ((int (*)(char**, const char*, va_list))resolve("vasprintf"))
            (sp, fmt, ap);
}

SF int printf(const char* fmt, ...) {
    va_list ap; va_start(ap, fmt);
    int r = vfprintf(stdout_, fmt, ap);
    va_end(ap);
    return r;
}

SF int fprintf(FILE* fp, const char* fmt, ...) {
    va_list ap; va_start(ap, fmt);
    int r = vfprintf(fp, fmt, ap);
    va_end(ap);
    return r;
}

SF int sprintf(char* buf, const char* fmt, ...) {
    va_list ap, aq; va_start(ap, fmt);
    va_copy(aq, ap);
    int n = ((int (*)(char*, size_t, const char*, va_list))resolve("vsnprintf"))
            (NULL, 0, fmt, aq);
    va_end(aq);
    if (n >= 0)
        ((int (*)(char*, size_t, const char*, va_list))resolve("vsnprintf"))
            (buf, (size_t)n + 1, fmt, ap);
    va_end(ap);
    return n;
}

SF int snprintf(char* buf, size_t n, const char* fmt, ...) {
    va_list ap; va_start(ap, fmt);
    int r = ((int (*)(char*, size_t, const char*, va_list))resolve("vsnprintf"))
            (buf, n, fmt, ap);
    va_end(ap);
    return r;
}
