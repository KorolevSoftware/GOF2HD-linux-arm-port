/*
 * gof2hd-sdl — Linux host for the Galaxy on Fire 2 HD ARMv7 engine,
 * built with SDL2 + Mali EGL linked directly (libs from the target device).
 *
 * Creates an SDL2 window + OpenGL ES context (via SDL -> libmali EGL),
 * then boots the engine.  The engine resolves its GLES calls to the same
 * libmali (libGLESv2.so -> libmali.so), so frames render on the device
 * framebuffer.
 */
#define _GNU_SOURCE
#include "jni.h"
#include <SDL2/SDL.h>
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

static SDL_Window* g_win = NULL;
static SDL_GLContext g_gl = NULL;
static int sdl_ok = 0;

static int init_sdl_egl(void) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "[host] SDL_Init: %s\n", SDL_GetError());
        return -1;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    g_win = SDL_CreateWindow("GOF2HD", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                             640, 480, SDL_WINDOW_OPENGL);
    if (!g_win) {
        fprintf(stderr, "[host] SDL_CreateWindow: %s\n", SDL_GetError());
        return -1;
    }
    g_gl = SDL_GL_CreateContext(g_win);
    if (!g_gl) {
        fprintf(stderr, "[host] SDL_GL_CreateContext: %s\n", SDL_GetError());
        return -1;
    }
    SDL_GL_MakeCurrent(g_win, g_gl);
    sdl_ok = 1;
    printf("[host] SDL+EGL window ready\n");
    return 0;
}

static void sdl_swap(void) {
    if (sdl_ok && g_win) SDL_GL_SwapWindow(g_win);
}

static void sdl_pump(void) {
    if (!sdl_ok) return;
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) { /* swallow for now */ }
}

/* ---- crash handler ---- */
static void crash_handler(int sig, siginfo_t* si, void* uc) {
    ucontext_t* u = (ucontext_t*)uc;
    fprintf(stderr, "\n=== crash signal %d (%s), fault addr %p ===\n",
            sig, strsignal(sig), si->si_addr);
    fprintf(stderr, "  r0=%08x r1=%08x r2=%08x r3=%08x\n",
            u->uc_mcontext.arm_r0, u->uc_mcontext.arm_r1,
            u->uc_mcontext.arm_r2, u->uc_mcontext.arm_r3);
    fprintf(stderr, "  sp=%08x lr=%08x pc=%08x\n",
            u->uc_mcontext.arm_sp, u->uc_mcontext.arm_lr, u->uc_mcontext.arm_pc);
    void* pc = (void*)u->uc_mcontext.arm_pc;
    Dl_info info;
    if (dladdr(pc, &info) && info.dli_fname)
        fprintf(stderr, "  in %s (%s) at offset +0x%lx\n",
                info.dli_fname, info.dli_sname ? info.dli_sname : "?",
                (unsigned long)((char*)pc - (char*)info.dli_fbase));
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

/* ---- engine entry points ---- */
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
static void (*p_handleTouchEvent)(JNIEnv*, jclass, jint, jint, jint, jint);
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

#define TOUCH_FIFO "/tmp/gof2hd_touch"
static int g_touch_fd = -1;
static void open_touch_fifo(void) {
    mkfifo(TOUCH_FIFO, 0644);
    g_touch_fd = open(TOUCH_FIFO, O_RDONLY | O_NONBLOCK);
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
        if (sscanf(line, "%d %d %d %d", &pid, &action, &x, &y) >= 4)
            p_handleTouchEvent(env, cls, pid, action, x, y);
    }
}

int main(int argc, char** argv) {
    if (argc < 4) {
        fprintf(stderr, "usage: %s <base.apk> <main.*.obb> <dataDir> [w] [h]\n", argv[0]);
        return 2;
    }
    const char* apk_path = argv[1];
    const char* obb_path = argv[2];
    const char* data_dir = argv[3];
    if (argc >= 5) g_width = atoi(argv[4]);
    if (argc >= 6) g_height = atoi(argv[5]);
    g_display_width = g_width;
    g_display_height = g_height;
    setbuf(stdout, NULL);
    setbuf(stderr, NULL);
    install_crash_handler();

    init_sdl_egl();

    if (!dlopen("libgcc_s.so.1", RTLD_NOW | RTLD_GLOBAL))
        fprintf(stderr, "[host] warn: libgcc_s: %s\n", dlerror());
    if (!dlopen("libstdc++.so.6", RTLD_NOW | RTLD_GLOBAL))
        fprintf(stderr, "[host] warn: libstdc++: %s\n", dlerror());
    void* h = dlopen("libgof2hdaa.so", RTLD_NOW | RTLD_GLOBAL);
    if (!h) { fprintf(stderr, "[host] cannot load libgof2hdaa.so: %s\n", dlerror()); return 1; }

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
    SYM(p_handleTouchEvent, "Java_net_fishlabs_gof2hdallandroid2012_ToJNI_handleTouchEvent");
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
    printf("[host] initialize(%d, %d)\n", g_width, g_height);
    p_initialize(env, cls, g_width, g_height);
    printf("[host] resize(%d, %d)\n", g_width, g_height);
    p_resize(env, cls, g_width, g_height);

    printf("[host] render loop started\n");
    open_touch_fifo();
    int frames = 0;
    while (1) {
        long t0 = now_ms();
        pump_touch(env, cls);
        sdl_pump();
        p_renderstep(env, cls, t0);
        sdl_swap();
        frames++;
        if (frames % 60 == 0)
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
