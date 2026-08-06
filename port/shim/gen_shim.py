#!/usr/bin/env python3
"""Generate libc.so bionic shim: bionic-versioned (@LIBC) symbols forwarding to glibc."""
import re

# args strings contain NAMED parameters already
SIG = {
    '__cxa_atexit': ('int', 'void (*a)(void*), void* b, void* c', 'MANUAL'),
    '__cxa_finalize': ('void', 'void* a', 'MANUAL'),
    '__errno': ('int*', 'void', 'SPECIAL'),
    '__sF': ('SPECIAL', 'SPECIAL', 'SPECIAL'),
    '__stack_chk_fail': ('void', 'void', 'SPECIAL'),
    '__stack_chk_guard': ('SPECIAL', 'SPECIAL', 'SPECIAL'),
    '__assert2': ('void', 'const char* a, int b, const char* c, const char* d', 'MANUAL'),
    'abort': ('void', 'void', 'auto'),
    'accept': ('int', 'int a, struct sockaddr* b, socklen_t* c', 'auto'),
    'acos': ('double', 'double a', 'auto'),
    'acosf': ('float', 'float a', 'auto'),
    'atan2': ('double', 'double a, double b', 'auto'),
    'atanf': ('float', 'float a', 'auto'),
    'atoi': ('int', 'const char* a', 'auto'),
    'atoll': ('long long', 'const char* a', 'auto'),
    'bind': ('int', 'int a, const struct sockaddr* b, socklen_t c', 'auto'),
    'calloc': ('void*', 'size_t a, size_t b', 'auto'),
    'ceil': ('double', 'double a', 'auto'),
    'chmod': ('int', 'const char* a, mode_t b', 'auto'),
    'chown': ('int', 'const char* a, uid_t b, gid_t c', 'auto'),
    'clearerr': ('void', 'FILE* a', 'auto'),
    'close': ('int', 'int a', 'auto'),
    'connect': ('int', 'int a, const struct sockaddr* b, socklen_t c', 'auto'),
    'cos': ('double', 'double a', 'auto'),
    'cosf': ('float', 'float a', 'auto'),
    'dlsym': ('void*', 'void* a, const char* b', 'auto'),
    'exp': ('double', 'double a', 'auto'),
    'fclose': ('int', 'FILE* a', 'auto'),
    'fcntl': ('int', 'int a, int b', 'auto'),
    'fdopen': ('FILE*', 'int a, const char* b', 'auto'),
    'ferror': ('int', 'FILE* a', 'auto'),
    'fileno': ('int', 'FILE* a', 'auto'),
    'floor': ('double', 'double a', 'auto'),
    'fopen': ('FILE*', 'const char* a, const char* b', 'auto'),
    'fprintf': ('int', 'FILE* f, const char* fmt', 'VARIADIC'),
    'fputc': ('int', 'int a, FILE* b', 'auto'),
    'fputs': ('int', 'const char* a, FILE* b', 'auto'),
    'fread': ('size_t', 'void* a, size_t b, size_t c, FILE* d', 'auto'),
    'free': ('void', 'void* a', 'auto'),
    'frexp': ('double', 'double a, int* b', 'auto'),
    'fseek': ('int', 'FILE* a, long b, int c', 'auto'),
    'fseeko': ('int', 'FILE* a, off_t b, int c', 'auto'),
    'fstat': ('int', 'int a, struct stat* b', 'auto'),
    'fsync': ('int', 'int a', 'auto'),
    'ftell': ('long', 'FILE* a', 'auto'),
    'ftello': ('off_t', 'FILE* a', 'auto'),
    'ftruncate': ('int', 'int a, off_t b', 'auto'),
    'fwrite': ('size_t', 'const void* a, size_t b, size_t c, FILE* d', 'auto'),
    'gethostbyname': ('struct hostent*', 'const char* a', 'auto'),
    'getpid': ('int', 'void', 'auto'),
    'gettimeofday': ('int', 'struct timeval* a, struct timezone* b', 'auto'),
    'inet_addr': ('unsigned long', 'const char* a', 'auto'),
    'islower': ('int', 'int a', 'auto'),
    'isxdigit': ('int', 'int a', 'auto'),
    'ldexp': ('double', 'double a, int b', 'auto'),
    'listen': ('int', 'int a, int b', 'auto'),
    'localtime': ('struct tm*', 'const time_t* a', 'auto'),
    'log': ('double', 'double a', 'auto'),
    'log10': ('double', 'double a', 'auto'),
    'log10f': ('float', 'float a', 'auto'),
    'lrand48': ('long', 'void', 'auto'),
    'lrintf': ('long', 'float a', 'auto'),
    'lseek': ('off_t', 'int a, off_t b, int c', 'auto'),
    'malloc': ('void*', 'size_t a', 'auto'),
    'memalign': ('void*', 'size_t a, size_t b', 'auto'),
    'memchr': ('void*', 'const void* a, int b, size_t c', 'auto'),
    'memcmp': ('int', 'const void* a, const void* b, size_t c', 'auto'),
    'memcpy': ('void*', 'void* a, const void* b, size_t c', 'auto'),
    'memmem': ('void*', 'const void* a, size_t b, const void* c, size_t d', 'auto'),
    'memmove': ('void*', 'void* a, const void* b, size_t c', 'auto'),
    'memset': ('void*', 'void* a, int b, size_t c', 'auto'),
    'mkstemp': ('int', 'char* a', 'auto'),
    'mktime': ('time_t', 'struct tm* a', 'auto'),
    'mmap': ('void*', 'void* a, size_t b, int c, int d, int e, off_t f', 'auto'),
    'open': ('int', 'const char* a, int b', 'auto'),
    'pow': ('double', 'double a, double b', 'auto'),
    'powf': ('float', 'float a, float b', 'auto'),
    'printf': ('int', 'const char* fmt', 'VARIADIC'),
    'pthread_cond_broadcast': ('int', 'pthread_cond_t* a', 'auto'),
    'pthread_cond_wait': ('int', 'pthread_cond_t* a, pthread_mutex_t* b', 'auto'),
    'pthread_create': ('int', 'pthread_t* a, const pthread_attr_t* b, void* (*c)(void*), void* d', 'MANUAL'),
    'pthread_getspecific': ('void*', 'pthread_key_t a', 'auto'),
    'pthread_key_create': ('int', 'pthread_key_t* a, void (*b)(void*)', 'MANUAL'),
    'pthread_key_delete': ('int', 'pthread_key_t a', 'auto'),
    'pthread_mutex_destroy': ('int', 'pthread_mutex_t* a', 'auto'),
    'pthread_mutex_init': ('int', 'pthread_mutex_t* a, const pthread_mutexattr_t* b', 'auto'),
    'pthread_mutex_lock': ('int', 'pthread_mutex_t* a', 'auto'),
    'pthread_mutex_unlock': ('int', 'pthread_mutex_t* a', 'auto'),
    'pthread_mutexattr_init': ('int', 'pthread_mutexattr_t* a', 'auto'),
    'pthread_mutexattr_settype': ('int', 'pthread_mutexattr_t* a, int b', 'auto'),
    'pthread_once': ('int', 'pthread_once_t* a, void (*b)(void)', 'MANUAL'),
    'pthread_self': ('pthread_t', 'void', 'auto'),
    'pthread_setspecific': ('int', 'pthread_key_t a, const void* b', 'auto'),
    'pthread_attr_destroy': ('int', 'pthread_attr_t* a', 'auto'),
    'pthread_attr_init': ('int', 'pthread_attr_t* a', 'auto'),
    'pthread_attr_setdetachstate': ('int', 'pthread_attr_t* a, int b', 'auto'),
    'pthread_attr_setstacksize': ('int', 'pthread_attr_t* a, size_t b', 'auto'),
    'putc': ('int', 'int a, FILE* b', 'auto'),
    'puts': ('int', 'const char* a', 'auto'),
    'qsort': ('void', 'void* a, size_t b, size_t c, int (*d)(const void*, const void*)', 'MANUAL'),
    'raise': ('int', 'int a', 'auto'),
    'read': ('ssize_t', 'int a, void* b, size_t c', 'auto'),
    'realloc': ('void*', 'void* a, size_t b', 'auto'),
    'recv': ('ssize_t', 'int a, void* b, size_t c, int d', 'auto'),
    'remove': ('int', 'const char* a', 'auto'),
    'rename': ('int', 'const char* a, const char* b', 'auto'),
    'rint': ('double', 'double a', 'auto'),
    'select': ('int', 'int a, fd_set* b, fd_set* c, fd_set* d, struct timeval* e', 'auto'),
    'sem_destroy': ('int', 'sem_t* a', 'auto'),
    'sem_init': ('int', 'sem_t* a, int b, unsigned c', 'auto'),
    'sem_post': ('int', 'sem_t* a', 'auto'),
    'sem_wait': ('int', 'sem_t* a', 'auto'),
    'send': ('ssize_t', 'int a, const void* b, size_t c, int d', 'auto'),
    'setsockopt': ('int', 'int a, int b, int c, const void* d, socklen_t e', 'auto'),
    'sigaction': ('int', 'int a, const struct sigaction* b, struct sigaction* c', 'auto'),
    'sigprocmask': ('int', 'int a, const sigset_t* b, sigset_t* c', 'auto'),
    'sin': ('double', 'double a', 'auto'),
    'sinf': ('float', 'float a', 'auto'),
    'snprintf': ('int', 'char* buf, size_t n, const char* fmt', 'VARIADIC'),
    'socket': ('int', 'int a, int b, int c', 'auto'),
    'sprintf': ('int', 'char* buf, const char* fmt', 'VARIADIC'),
    'sqrt': ('double', 'double a', 'auto'),
    'sqrtf': ('float', 'float a', 'auto'),
    'srand48': ('void', 'long a', 'auto'),
    'sscanf': ('int', 'const char* str, const char* fmt', 'VARIADIC'),
    'stat': ('int', 'const char* a, struct stat* b', 'auto'),
    'statfs': ('int', 'const char* a, struct statfs* b', 'auto'),
    'strcasecmp': ('int', 'const char* a, const char* b', 'auto'),
    'strcat': ('char*', 'char* a, const char* b', 'auto'),
    'strchr': ('char*', 'const char* a, int b', 'auto'),
    'strcmp': ('int', 'const char* a, const char* b', 'auto'),
    'strcpy': ('char*', 'char* a, const char* b', 'auto'),
    'strdup': ('char*', 'const char* a', 'auto'),
    'strerror': ('char*', 'int a', 'auto'),
    'strlen': ('size_t', 'const char* a', 'auto'),
    'strncasecmp': ('int', 'const char* a, const char* b, size_t c', 'auto'),
    'strncmp': ('int', 'const char* a, const char* b, size_t c', 'auto'),
    'strncpy': ('char*', 'char* a, const char* b, size_t c', 'auto'),
    'strrchr': ('char*', 'const char* a, int b', 'auto'),
    'strtol': ('long', 'const char* a, char** b, int c', 'auto'),
    'strtoul': ('unsigned long', 'const char* a, char** b, int c', 'auto'),
    'syscall': ('long', 'long n_', 'VARIADIC'),
    'sysconf': ('long', 'int a', 'auto'),
    'tan': ('double', 'double a', 'auto'),
    'time': ('time_t', 'time_t* a', 'auto'),
    'umask': ('mode_t', 'mode_t a', 'auto'),
    'uname': ('int', 'struct utsname* a', 'auto'),
    'unlink': ('int', 'const char* a', 'auto'),
    'usleep': ('int', 'useconds_t a', 'auto'),
    'utime': ('int', 'const char* a, const struct utimbuf* b', 'auto'),
    'vasprintf': ('int', 'char** a, const char* b, va_list c', 'auto'),
    'vfprintf': ('int', 'FILE* a, const char* b, va_list c', 'auto'),
    'vsnprintf': ('int', 'char* a, size_t b, const char* c, va_list d', 'auto'),
    'wcslen': ('size_t', 'const wchar_t* a', 'auto'),
    'write': ('ssize_t', 'int a, const void* b, size_t c', 'auto'),
    '_exit': ('void', 'int a', 'auto'),
}

HEADERS = [
    '#include <stdio.h>', '#include <stdlib.h>', '#include <string.h>',
    '#include <stdarg.h>', '#include <stddef.h>', '#include <unistd.h>',
    '#include <fcntl.h>', '#include <sys/stat.h>', '#include <sys/mman.h>',
    '#include <sys/time.h>', '#include <sys/socket.h>', '#include <sys/utsname.h>',
    '#include <sys/vfs.h>', '#include <netdb.h>', '#include <arpa/inet.h>',
    '#include <utime.h>', '#include <semaphore.h>', '#include <time.h>',
    '#include <dlfcn.h>', '#include <errno.h>', '#include <wchar.h>',
    '#include <pthread.h>', '#include <signal.h>', '#include <sys/select.h>',
    '',
    'extern FILE* stdin; extern FILE* stdout; extern FILE* stderr;',
]

VARARG_BODIES = {
    'printf': '    va_list ap; va_start(ap, fmt); int r = vfprintf(stdout, fmt, ap); va_end(ap); return r;',
    'fprintf': '    va_list ap; va_start(ap, fmt); int r = vfprintf(f, fmt, ap); va_end(ap); return r;',
    'sprintf': '    va_list ap; va_start(ap, fmt); int r = vsprintf(buf, fmt, ap); va_end(ap); return r;',
    'snprintf': '    va_list ap; va_start(ap, fmt); int r = vsnprintf(buf, n, fmt, ap); va_end(ap); return r;',
    'sscanf': '    va_list ap; va_start(ap, fmt); int r = vsscanf(str, fmt, ap); va_end(ap); return r;',
    'syscall': ('    va_list ap; va_start(ap, n_); long a1 = va_arg(ap, long), a2 = va_arg(ap, long),'
                ' a3 = va_arg(ap, long), a4 = va_arg(ap, long), a5 = va_arg(ap, long), a6 = va_arg(ap, long);'
                ' va_end(ap); return ((long (*)(long, long, long, long, long, long, long))resolve("syscall"))'
                '(n_, a1, a2, a3, a4, a5, a6);'),
}

MANUAL = {
    '__cxa_atexit': 'int __cxa_atexit(void (*a)(void*), void* b, void* c) { return ((int (*)(void (*)(void*), void*, void*))resolve("__cxa_atexit"))(a, b, c); }',
    '__cxa_finalize': 'void __cxa_finalize(void* a) { ((void (*)(void*))resolve("__cxa_finalize"))(a); }',
    '__assert2': 'void __assert2(const char* a, int b, const char* c, const char* d) { fprintf(stderr, "Assertion failed: %s, function %s, file %s, line %d\\n", d, c, a, b); abort(); }',
    'pthread_create': 'int pthread_create(pthread_t* a, const pthread_attr_t* b, void* (*c)(void*), void* d) { return ((int (*)(pthread_t*, const pthread_attr_t*, void* (*)(void*), void*))resolve("pthread_create"))(a, b, c, d); }',
    'pthread_key_create': 'int pthread_key_create(pthread_key_t* a, void (*b)(void*)) { return ((int (*)(pthread_key_t*, void (*)(void*)))resolve("pthread_key_create"))(a, b); }',
    'pthread_once': 'int pthread_once(pthread_once_t* a, void (*b)(void)) { return ((int (*)(pthread_once_t*, void (*)(void)))resolve("pthread_once"))(a, b); }',
    'qsort': 'void qsort(void* a, size_t b, size_t c, int (*d)(const void*, const void*)) { ((void (*)(void*, size_t, size_t, int (*)(const void*, const void*)))resolve("qsort"))(a, b, c, d); }',
}


def name_list(args):
    if args == 'void':
        return []
    out = []
    for part in args.split(','):
        part = part.strip()
        out.append(part.rsplit(' ', 1)[-1])
    return out


def type_only(args):
    if args == 'void':
        return 'void'
    out = []
    for part in args.split(','):
        part = part.strip()
        out.append(part.rsplit(' ', 1)[0])
    return ', '.join(out)


def gen():
    L = list(HEADERS)
    L.append('extern int __assert_fail(const char*, const char*, unsigned int, const char*);')
    L.append('')
    L.append('static void* resolve(const char* n) {')
    L.append('    void* p = dlsym(RTLD_NEXT, n);')
    L.append('    if (!p) p = dlsym(RTLD_DEFAULT, n);')
    L.append('    if (!p) { fprintf(stderr, "[bionic-shim] cannot resolve %s\\n", n); abort(); }')
    L.append('    return p;')
    L.append('}')
    L.append('')
    for n in sorted(SIG):
        ret, args, kind = SIG[n]
        if kind == 'SPECIAL':
            continue
        if kind == 'MANUAL':
            L.append(MANUAL[n])
            L.append('')
            continue
        names = name_list(args)
        types = type_only(args)
        if kind == 'VARIADIC':
            L.append(f'{ret} {n}({args}, ...) {{')
            L.append('    ' + VARARG_BODIES[n])
            L.append('}')
            L.append('')
            continue
        L.append(f'static {ret} (*_f_{n})({types});')
        call = ', '.join(names)
        if ret == 'void':
            L.append(f'{ret} {n}({args}) {{')
            L.append(f'    _f_{n} = ({ret} (*)({types}))resolve("{n}");')
            L.append(f'    _f_{n}({call});')
            L.append('}')
        else:
            L.append(f'{ret} {n}({args}) {{')
            L.append(f'    _f_{n} = ({ret} (*)({types}))resolve("{n}");')
            L.append(f'    return _f_{n}({call});')
            L.append('}')
        L.append('')
    L.append('int* __errno(void) { return &errno; }')
    L.append('')
    L.append('FILE* __sF[3] = { NULL, NULL, NULL };')
    L.append('__attribute__((constructor)) static void init_sF(void) {')
    L.append('    __sF[0] = stdin; __sF[1] = stdout; __sF[2] = stderr;')
    L.append('}')
    L.append('')
    L.append('unsigned long __stack_chk_guard = 0xdeadbeef;')
    L.append('void __stack_chk_fail(void) { fprintf(stderr, "stack smashing detected\\n"); abort(); }')
    L.append('')
    return '\n'.join(L)


if __name__ == '__main__':
    src = gen()
    open('shim.c', 'w').write(src)
    exported = set(SIG.keys()) - {'__sF'} | {'__errno', '__sF', '__assert2', '__stack_chk_guard', '__stack_chk_fail'}
    exported = sorted(exported)
    vmap = 'LIBC {\n  global:\n'
    for n in exported:
        vmap += f'    {n};\n'
    vmap += '  local:\n    *;\n};\n'
    open('version.map', 'w').write(vmap)
    print('shim.c + version.map written')
