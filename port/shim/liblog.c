/*
 * liblog.so — Android log bridge. The engine calls __android_log_print;
 * we route it to stderr so the game's own logs appear in the terminal.
 */
#include <stdio.h>
#include <stdarg.h>

static int log_buf_write(int buf, int prio, const char* tag, const char* msg) {
    (void)buf; (void)prio;
    fprintf(stderr, "[%s] %s\n", tag ? tag : "gof2", msg ? msg : "");
    return 0;
}

int __android_log_print(int prio, const char* tag, const char* fmt, ...) {
    char buf[4096];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    return log_buf_write(3, prio, tag, buf);
}

int __android_log_vprint(int prio, const char* tag, const char* fmt, va_list ap) {
    char buf[4096];
    vsnprintf(buf, sizeof(buf), fmt, ap);
    return log_buf_write(3, prio, tag, buf);
}

int __android_log_write(int prio, const char* tag, const char* msg) {
    return log_buf_write(3, prio, tag, msg);
}

int __android_log_buf_write(int buf, int prio, const char* tag, const char* msg) {
    return log_buf_write(buf, prio, tag, msg);
}

int __android_log_vprint_buf(int buf, int prio, const char* tag, const char* fmt, va_list ap) {
    char tmp[4096];
    vsnprintf(tmp, sizeof(tmp), fmt, ap);
    return log_buf_write(buf, prio, tag, tmp);
}
