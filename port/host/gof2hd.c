/*
 * gof2hd — Linux host for the Galaxy on Fire 2 HD ARMv7 engine.
 *
 * Replicates what the Android Java wrapper did:
 *   JNI_OnLoad -> setZIPPath(.obb) -> SetDirectories -> setAPKPath(base.apk)
 *   -> setEnvironmentVariables(context) -> setCountryCodeOfDevice
 *   -> initialize(w,h) -> resize(w,h) -> { renderstep(ms); sleep ~33ms; }
 *
 * Usage: gof2hd <apk> <obb> <dataDir> [width] [height]
 */
#define _GNU_SOURCE
#include "jni.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <unistd.h>
#include <time.h>
#include <signal.h>
#include <execinfo.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>

/* Avoid glibc's __isoc23_sscanf (needs GLIBC_2.38, absent on device glibc 2.37) */
#include <stdarg.h>
static int legacy_sscanf(const char* s, const char* fmt, ...) {
    /* only "%d %d %d %d" is used for touch parsing; maps path is unused */
    if (!strcmp(fmt, "%d %d %d %d")) {
        va_list ap; va_start(ap, fmt);
        int* a = va_arg(ap, int*); int* b = va_arg(ap, int*);
        int* c = va_arg(ap, int*); int* d = va_arg(ap, int*);
        va_end(ap);
        const char* p = s; int r = 0;
        for (int i = 0; i < 4; i++) {
            char* end;
            long v = strtoul(p, &end, 10);
            if (end == p) break;
            if (i==0)*a=(int)v; else if(i==1)*b=(int)v; else if(i==2)*c=(int)v; else *d=(int)v;
            r++;
            p = end; while (*p==' ') p++;
        }
        return r;
    }
    return 0;
}
#define sscanf legacy_sscanf

static char g_maps[32768];
static int g_maps_len = 0;
static void snapshot_maps(void) {
    int fd = open("/proc/self/maps", O_RDONLY);
    if (fd < 0) return;
    g_maps_len = read(fd, g_maps, sizeof(g_maps) - 1);
    close(fd);
    if (g_maps_len > 0) g_maps[g_maps_len] = 0;
}

static void find_addr(unsigned long a, char* out, size_t n) {
    out[0] = 0;
    char* p = g_maps;
    char* end = g_maps + g_maps_len;
    while (p < end) {
        char* nl = strchr(p, '\n');
        if (!nl) nl = end;
        char saved = *nl; *nl = 0;
        unsigned long lo, hi;
        char path[256] = "";
        if (sscanf(p, "%lx-%lx %*s %*s %*s %*s %255s", &lo, &hi, path) >= 2 &&
            a >= lo && a < hi) {
            snprintf(out, n, "%s +0x%lx", path[0] ? path : "[anon]", a - lo);
            *nl = saved;
            return;
        }
        *nl = saved;
        p = nl + 1;
    }
}

static void crash_handler(int sig, siginfo_t* si, void* uc) {
    ucontext_t* u = (ucontext_t*)uc;
    char buf[512];
    int n = snprintf(buf, sizeof(buf),
        "\n=== crash signal %d (%s), fault %p r0=%x r1=%x r2=%x r3=%x\n"
        "  r4=%x r5=%x r6=%x r7=%x sp=%x lr=%x pc=%x\n",
        sig, strsignal(sig), si->si_addr,
        u->uc_mcontext.arm_r0, u->uc_mcontext.arm_r1, u->uc_mcontext.arm_r2,
        u->uc_mcontext.arm_r3, u->uc_mcontext.arm_r4, u->uc_mcontext.arm_r5,
        u->uc_mcontext.arm_r6, u->uc_mcontext.arm_r7,
        u->uc_mcontext.arm_sp, u->uc_mcontext.arm_lr, u->uc_mcontext.arm_pc);
    if (n > 0) write(2, buf, n);
    if (g_maps_len > 0) {
        write(2, "---- maps ----\n", 15);
        write(2, g_maps, g_maps_len);
    }
    _exit(128 + sig);
}

static void install_crash_handler(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = crash_handler;
    sa.sa_flags = SA_SIGINFO;
    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGABRT, &sa, NULL);
    sigaction(SIGBUS, &sa, NULL);
    sigaction(SIGILL, &sa, NULL);
}

/* SIGUSR1: dump backtrace to help locate a hang */
static void dump_backtrace(int sig, siginfo_t* si, void* uc) {
    (void)sig; (void)si; (void)uc;
    fprintf(stderr, "\n=== SIGUSR1 backtrace (hang diagnostic) ===\n");
    void* bt[32];
    int n = backtrace(bt, 32);
    char** syms = backtrace_symbols(bt, n);
    for (int i = 0; i < n; i++) fprintf(stderr, "  %s\n", syms[i]);
    free(syms);
    fflush(stderr);
}
static void install_dump_handler(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = dump_backtrace;
    sa.sa_flags = SA_SIGINFO;
    sigaction(SIGUSR1, &sa, NULL);
}

/* engine entry points (resolved via dlsym) */
static jint (*p_JNI_OnLoad)(JavaVM_* vm);
static void (*p_setZIPPath)(JNIEnv*, jclass, jstring);
static void (*p_SetDirectories)(JNIEnv*, jclass, jstring, jstring);
static void (*p_setAPKPath)(JNIEnv*, jclass, jstring);
static void (*p_setEnvironmentVariables)(JNIEnv*, jclass, jobject);
static void (*p_setCountryCodeOfDevice)(JNIEnv*, jclass, jint);
static void (*p_initialize)(JNIEnv*, jclass, jint, jint);
static void (*p_resize)(JNIEnv*, jclass, jint, jint);
static void (*p_renderstep)(JNIEnv*, jclass, jlong);
static jint (*p_getExitFlag)(JNIEnv*, jclass);
static jint (*p_getLogoShown)(JNIEnv*, jclass);
static jint (*p_isInMainMenu)(JNIEnv*, jclass);
static jint (*p_getScreenshotFlag)(JNIEnv*, jclass);
static void (*p_resetScreenshotFlag)(JNIEnv*, jclass);
static void (*p_handleTouchEvent)(JNIEnv*, jclass, jint, jint, jint, jint);
static void (*p_handleAccelerometer)(JNIEnv*, jclass, jfloat, jfloat, jfloat);
static void (*p_setOrigamiSuperClub)(JNIEnv*, jclass, jstring);

static int g_width = 640;
static int g_height = 480;

static jclass tojni_class(void) {
    static fake_jclass c = { "net/fishlabs/gof2hdallandroid2012/ToJNI" };
    return (jclass)&c;
}

static void* resolve_required(void* h, const char* name) {
    void* p = dlsym(h, name);
    if (!p) { fprintf(stderr, "[host] missing symbol %s: %s\n", name, dlerror()); exit(1); }
    return p;
}

static long now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/* ---- EGL presenter (replaces the Android Java surface/swap) ----
 * On Android the Java GLSurfaceView does eglCreateWindowSurface +
 * eglSwapBuffers around the native renderstep.  Here we create the
 * default fbdev window surface ourselves and swap after each frame. */
typedef void* EGLDisplay;
typedef void* EGLConfig;
typedef void* EGLSurface;
typedef void* EGLContext;
typedef int32_t EGLBoolean;
typedef int32_t EGLint;
typedef int32_t GLint;
typedef int32_t GLsizei;
typedef unsigned int GLenum;

static EGLDisplay (*q_eglGetDisplay)(EGLDisplay);
static EGLBoolean (*q_eglInitialize)(EGLDisplay, EGLint*, EGLint*);
static EGLBoolean (*q_eglChooseConfig)(EGLDisplay, const EGLint*, EGLConfig*, EGLint, EGLint*);
static EGLBoolean (*q_eglGetConfigAttrib)(EGLDisplay, EGLConfig, EGLint, EGLint*);
static EGLSurface (*q_eglCreateWindowSurface)(EGLDisplay, EGLConfig, void*, const EGLint*);
static EGLContext (*q_eglCreateContext)(EGLDisplay, EGLConfig, EGLContext, const EGLint*);
static EGLBoolean (*q_eglMakeCurrent)(EGLDisplay, EGLSurface, EGLSurface, EGLContext);
static EGLBoolean (*q_eglSwapBuffers)(EGLDisplay, EGLSurface);
static EGLint (*q_eglGetError)(void);

static EGLDisplay g_egl_display;
static EGLSurface g_egl_surface;

static int setup_egl(void) {
    const char* path = getenv("GOF_EGL_LIB");
    if (!path) path = "/usr/lib32/libEGL.so.1";
    void* h = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!h) {
        h = dlopen("libEGL.so.1", RTLD_NOW | RTLD_LOCAL);
        if (!h) { fprintf(stderr, "[host] EGL: %s\n", dlerror()); return -1; }
    }
    q_eglGetDisplay = dlsym(h, "eglGetDisplay");
    q_eglInitialize = dlsym(h, "eglInitialize");
    q_eglChooseConfig = dlsym(h, "eglChooseConfig");
    q_eglGetConfigAttrib = dlsym(h, "eglGetConfigAttrib");
    q_eglCreateWindowSurface = dlsym(h, "eglCreateWindowSurface");
    q_eglCreateContext = dlsym(h, "eglCreateContext");
    q_eglMakeCurrent = dlsym(h, "eglMakeCurrent");
    q_eglSwapBuffers = dlsym(h, "eglSwapBuffers");
    q_eglGetError = dlsym(h, "eglGetError");
    if (!q_eglGetDisplay || !q_eglInitialize || !q_eglChooseConfig ||
        !q_eglCreateWindowSurface || !q_eglCreateContext || !q_eglMakeCurrent ||
        !q_eglSwapBuffers) {
        fprintf(stderr, "[host] EGL: missing symbols in %s\n", path);
        return -1;
    }
    g_egl_display = q_eglGetDisplay(0);
    if (!g_egl_display) { fprintf(stderr, "[host] EGL: eglGetDisplay failed\n"); return -1; }
    EGLint maj = 0, min = 0;
    if (!q_eglInitialize(g_egl_display, &maj, &min)) {
        fprintf(stderr, "[host] EGL: eglInitialize failed\n"); return -1;
    }
    EGLConfig cfgs[8];
    EGLint n = 0;
    {
        static const EGLint attribs[] = {
            0x3033 /*EGL_SURFACE_TYPE*/, 0x4  /*EGL_WINDOW_BIT*/,
            0x3040 /*EGL_RENDERABLE_TYPE*/, 0x4 /*EGL_OPENGL_ES2_BIT*/,
            0x3024 /*EGL_RED_SIZE*/, 8,
            0x3023 /*EGL_GREEN_SIZE*/, 8,
            0x3022 /*EGL_BLUE_SIZE*/, 8,
            0x3021 /*EGL_ALPHA_SIZE*/, 8,
            0x3025 /*EGL_DEPTH_SIZE*/, 24,
            0x3026 /*EGL_STENCIL_SIZE*/, 8,
            0x3038 /*EGL_NONE*/
        };
        if (!q_eglChooseConfig(g_egl_display, attribs, cfgs, 8, &n) || n < 1) {
            fprintf(stderr, "[host] EGL: no ES2 configs (err=%d)\n", q_eglGetError()); return -1;
        }
    }
    EGLConfig cfg = cfgs[0];
    for (EGLint i = 0; i < n && i < 4; i++) {
        EGLint depth = 0;
        q_eglGetConfigAttrib(g_egl_display, cfgs[i], 0x3025 /*EGL_DEPTH_SIZE*/, &depth);
        if (depth >= 16) { cfg = cfgs[i]; break; }
    }
    g_egl_surface = q_eglCreateWindowSurface(g_egl_display, cfg, NULL, 0);
    if (!g_egl_surface) {
        fprintf(stderr, "[host] EGL: window surface failed (err=%d)\n", q_eglGetError());
        return -1;
    }
    {
        static const EGLint ctx_attrs[] = {
            0x3098 /*EGL_CONTEXT_CLIENT_VERSION*/, 2,
            0x3038 /*EGL_NONE*/
        };
        EGLContext ctx = q_eglCreateContext(g_egl_display, cfg, 0, ctx_attrs);
        if (!ctx) {
            fprintf(stderr, "[host] EGL: ES2 context failed (err=%d)\n", q_eglGetError());
            return -1;
        }
        if (!q_eglMakeCurrent(g_egl_display, g_egl_surface, g_egl_surface, ctx)) {
            fprintf(stderr, "[host] EGL: makeCurrent failed (err=%d)\n", q_eglGetError());
            return -1;
        }
    }
    fprintf(stderr, "[host] EGL: display/surface/context ready (EGL %d.%d)\n", maj, min);
    {
        static EGLint (*q_eglQuerySurface)(EGLDisplay, EGLSurface, EGLint, EGLint*);
        static EGLint (*q_eglGetCurrentSurface)(EGLint);
        if (!q_eglQuerySurface) q_eglQuerySurface = dlsym(h, "eglQuerySurface");
        if (!q_eglGetCurrentSurface) q_eglGetCurrentSurface = dlsym(h, "eglGetCurrentSurface");
        if (q_eglQuerySurface && q_eglGetCurrentSurface) {
            EGLint w = 0, hh = 0;
            EGLSurface cs = q_eglGetCurrentSurface(0x3059 /*EGL_DRAW*/);
            q_eglQuerySurface(g_egl_display, g_egl_surface, 0x3057 /*EGL_WIDTH*/, &w);
            q_eglQuerySurface(g_egl_display, g_egl_surface, 0x3056 /*EGL_HEIGHT*/, &hh);
            fprintf(stderr, "[host] EGL surface=%p cur=%p %dx%d\n", g_egl_surface, cs, w, hh);
        }
    }
    return 0;
}

static void swap_egl(void) {
    static void (*q_glReadPixels)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*);
    static void (*q_glClearColor)(float, float, float, float);
    static void (*q_glClear)(GLint);
    if (!q_glReadPixels) {
        void* g = dlopen("libGLESv2.so.2", RTLD_NOW | RTLD_LOCAL);
        if (g) {
            q_glReadPixels = dlsym(g, "glReadPixels");
            q_glClearColor = dlsym(g, "glClearColor");
            q_glClear = dlsym(g, "glClear");
        }
    }
    if (getenv("GOF_GL_DIAG")) {
        static long t0;
        static int shown;
        if (!shown) { shown = 1; t0 = now_ms(); }
        if (now_ms() - t0 < 9000 && (now_ms() / 1500) % 3 == 0) {
            static void (*q_glGetIntegerv)(GLenum, GLint*);
            static GLenum (*q_glGetError)(void);
            static void* g;
            if (!q_glGetIntegerv) {
                g = dlopen("libGLESv2.so.2", RTLD_NOW | RTLD_LOCAL);
                if (g) {
                    q_glGetIntegerv = dlsym(g, "glGetIntegerv");
                    q_glGetError = dlsym(g, "glGetError");
                }
            }
            if (q_glGetIntegerv) {
                GLint v[4] = { -1, -1, -1, -1 };
                q_glGetIntegerv(0x8B8D /*GL_CURRENT_PROGRAM*/, &v[0]);
                q_glGetIntegerv(0x8CA6 /*GL_FRAMEBUFFER_BINDING*/, &v[1]);
                q_glGetIntegerv(0x0BA2 /*GL_VIEWPORT*/, &v[2]);
                q_glGetIntegerv(0x0D33, &v[3]);
                fprintf(stderr, "[host] diag prog=%d fbo=%d vp=%d glerr=%d\n",
                        v[0], v[1], v[2], q_glGetError ? q_glGetError() : -1);
            }
        }
    }
    if (g_egl_surface && q_eglSwapBuffers)
        q_eglSwapBuffers(g_egl_display, g_egl_surface);
}

/* ---- mouse -> touch bridge (host side viewer writes /tmp/gof2hd_touch) ---- */
#define TOUCH_FIFO "/tmp/gof2hd_touch"

static int g_touch_fd = -1;

static void open_touch_fifo(void) {
    mkfifo(TOUCH_FIFO, 0644);
    g_touch_fd = open(TOUCH_FIFO, O_RDONLY | O_NONBLOCK);
    if (g_touch_fd < 0)
        fprintf(stderr, "[host] touch fifo open failed: %s\n", strerror(errno));
}

static void pump_touch(JNIEnv* env, jclass cls) {
    if (g_touch_fd < 0) return;
    static char buf[4096];
    ssize_t n = read(g_touch_fd, buf, sizeof(buf) - 1);
    if (n <= 0) return;
    buf[n] = 0;
    char* save = NULL;
    for (char* line = strtok_r(buf, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
        int pid = 722, action = 0, x = 0, y = 0;
        if (sscanf(line, "%d %d %d %d", &pid, &action, &x, &y) >= 4) {
            if (getenv("GOF_VERBOSE_JNI"))
                fprintf(stderr, "[host] touch pid=%d act=%d x=%d y=%d\n", pid, action, x, y);
            p_handleTouchEvent(env, cls, pid, action, x, y);
        }
    }
}

int main(int argc, char** argv) {
    if (argc < 4) {
        fprintf(stderr,
            "usage: %s <base.apk> <main.*.obb> <dataDir> [width] [height]\n", argv[0]);
        return 2;
    }
    const char* apk_path = argv[1];
    const char* obb_path = argv[2];
    const char* data_dir = argv[3];
    if (argc >= 5) g_width = atoi(argv[4]);
    if (argc >= 6) g_height = atoi(argv[5]);
    g_display_width = g_width;
    g_display_height = g_height;
    if (getenv("GOF_VERBOSE_JNI")) g_jni_verbose = 1;
    setbuf(stdout, NULL);
    setbuf(stderr, NULL);
    if (!getenv("GOF_GDB")) install_crash_handler();

    /* load engine libs (LD_LIBRARY_PATH must point to our shim dir) */
    /* ensure libgcc_s + libstdc++ are loaded: provide __aeabi_/_Unwind_/__cxa_ helpers */
    if (!dlopen("libgcc_s.so.1", RTLD_NOW | RTLD_GLOBAL))
        fprintf(stderr, "[host] warn: libgcc_s: %s\n", dlerror());
    if (!dlopen("libstdc++.so.6", RTLD_NOW | RTLD_GLOBAL))
        fprintf(stderr, "[host] warn: libstdc++: %s\n", dlerror());
    if (!dlopen("libfmodex.so", RTLD_NOW | RTLD_GLOBAL))
        fprintf(stderr, "[host] warn: libfmodex: %s\n", dlerror());
    if (!dlopen("libfmodevent.so", RTLD_NOW | RTLD_GLOBAL))
        fprintf(stderr, "[host] warn: libfmodevent: %s\n", dlerror());
    void* h = dlopen("libgof2hdaa.so", RTLD_NOW | RTLD_GLOBAL);
    if (!h) { fprintf(stderr, "[host] cannot load libgof2hdaa.so: %s\n", dlerror()); return 1; }
    {
        Dl_info di;
        if (dladdr((void*)resolve_required(h, "JNI_OnLoad"), &di))
            fprintf(stderr, "[host] libgof2hdaa.so base=%p\n", di.dli_fbase);
    }

#define SYM(var, n) var = (typeof(var))resolve_required(h, n)
    SYM(p_JNI_OnLoad, "JNI_OnLoad");
    SYM(p_setZIPPath, "Java_net_fishlabs_gof2hdallandroid2012_ToJNI_setZIPPath");
    SYM(p_SetDirectories, "Java_net_fishlabs_gof2hdallandroid2012_ToJNI_SetDirectories");
    SYM(p_setAPKPath, "Java_net_fishlabs_gof2hdallandroid2012_ToJNI_setAPKPath");
    SYM(p_setEnvironmentVariables, "Java_net_fishlabs_gof2hdallandroid2012_ToJNI_setEnvironmentVariables");
    SYM(p_setCountryCodeOfDevice, "Java_net_fishlabs_gof2hdallandroid2012_ToJNI_setCountryCodeOfDevice");
    SYM(p_initialize, "Java_net_fishlabs_gof2hdallandroid2012_ToJNI_initialize");
    SYM(p_resize, "Java_net_fishlabs_gof2hdallandroid2012_ToJNI_resize");
    SYM(p_renderstep, "Java_net_fishlabs_gof2hdallandroid2012_ToJNI_renderstep");
    SYM(p_getExitFlag, "Java_net_fishlabs_gof2hdallandroid2012_ToJNI_getExitFlag");
    SYM(p_getLogoShown, "Java_net_fishlabs_gof2hdallandroid2012_ToJNI_getLogoShown");
    SYM(p_isInMainMenu, "Java_net_fishlabs_gof2hdallandroid2012_ToJNI_isInMainMenu");
    SYM(p_getScreenshotFlag, "Java_net_fishlabs_gof2hdallandroid2012_ToJNI_getScreenshotFlag");
    SYM(p_resetScreenshotFlag, "Java_net_fishlabs_gof2hdallandroid2012_ToJNI_resetScreenshotFlag");
    SYM(p_handleTouchEvent, "Java_net_fishlabs_gof2hdallandroid2012_ToJNI_handleTouchEvent");
    SYM(p_handleAccelerometer, "Java_net_fishlabs_gof2hdallandroid2012_ToJNI_handleAccelerometer");
    /* optional; Java wrapper calls this with the origami super club key string */
    p_setOrigamiSuperClub = (typeof(p_setOrigamiSuperClub))
        dlsym(h, "Java_net_fishlabs_gof2hdallandroid2012_GOF2HD2012_SetOrigamiSuperClub");
#undef SYM

    JNIEnv* env = &g_env;
    jclass cls = tojni_class();
    jstring apk = (jstring)mk_jstring(apk_path);
    jstring obb = (jstring)mk_jstring(obb_path);
    jstring data = (jstring)mk_jstring(data_dir);

    printf("[host] JNI_OnLoad...\n");
    if (p_JNI_OnLoad(&g_vm) != 0) fprintf(stderr, "[host] warn: JNI_OnLoad != 0\n");

    printf("[host] setZIPPath(%s)\n", obb_path);
    p_setZIPPath(env, cls, obb);
    printf("[host] SetDirectories(%s, obbdir)\n", data_dir);
    p_SetDirectories(env, cls, data, obb);
    printf("[host] setAPKPath(%s)\n", apk_path);
    p_setAPKPath(env, cls, apk);
    printf("[host] setEnvironmentVariables(context)\n");
    static int fake_context;
    p_setEnvironmentVariables(env, cls, (jobject)&fake_context);
    printf("[host] setCountryCodeOfDevice(0)\n");
    p_setCountryCodeOfDevice(env, cls, 0);

    if (p_setOrigamiSuperClub) {
        static char origami[] = "0123456789ABCDEF0123456789ABCDEF";
        jstring oj = (jstring)mk_jstring(origami);
        printf("[host] setOrigamiSuperClub(...)\n");
        p_setOrigamiSuperClub(env, cls, oj);
    }

    printf("[host] init EGL from %s (red=%s)\n", getenv("GOF_EGL_LIB") ? getenv("GOF_EGL_LIB") : "/usr/lib32/libEGL.so.1",
           getenv("GOF_RED_TEST") ? "yes" : "no");
    if (setup_egl() != 0 && getenv("GOF_EGL_REQUIRED"))
        return 1;
    printf("[host] initialize(%d, %d)\n", g_width, g_height);
    p_initialize(env, cls, g_width, g_height);
    printf("[host] resize(%d, %d)\n", g_width, g_height);
    p_resize(env, cls, g_width, g_height);

    printf("[host] render loop started\n");
    snapshot_maps();
    open_touch_fifo();
    int frames = 0;
    while (1) {
        long t0 = now_ms();
        pump_touch(env, cls);
        p_renderstep(env, cls, t0);
        swap_egl();
        frames++;
        if (frames % 120 == 0)
            printf("[host] %d frames; logo=%d menu=%d exit=%d\n",
                   frames, p_getLogoShown(env, cls),
                   p_isInMainMenu(env, cls), p_getExitFlag(env, cls));
        if (getenv("GOF_AUTO_TOUCH") && frames % 8 == 0) {
            int cx = g_width / 2, cy = g_height / 2;
            p_handleTouchEvent(env, cls, 722, 0, cx, cy);
            p_handleTouchEvent(env, cls, 722, 1, cx, cy);
        }
        if (p_getExitFlag(env, cls) == -1) break;
        long el = now_ms() - t0;
        if (el < 33) usleep((33 - el) * 1000);
    }
    printf("[host] exit requested\n");
    return 0;
}
