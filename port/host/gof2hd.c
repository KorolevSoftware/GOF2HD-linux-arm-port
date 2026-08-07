/*
 * gof2hd — Linux host for the Galaxy on Fire 2 HD ARMv7 engine.
 *
 * Replicates what the Android Java wrapper did:
 *   JNI_OnLoad -> setZIPPath(.obb) -> SetDirectories -> setAPKPath(base.apk)
 *   -> setEnvironmentVariables(context) -> setCountryCodeOfDevice
 *   -> initialize(w,h) -> resize(w,h) -> { renderstep(ms); sleep ~33ms; }
 *
 * Window/EGL and gamepad input are handled by SDL2 (device build: SDL2 with
 * the mali-fbdev video driver + the built-in joystick/gamecontroller
 * subsystem). The engine still resolves its GLES calls through our
 * libGLESv2.so shim -> libmali, so frames render into the same SDL/EGL
 * context on the device framebuffer.
 *
 * Usage: gof2hd <apk> <obb> <dataDir> [width] [height]
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
#include <sys/mman.h>
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

/* ---- crash handler ---- */
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

/* ---- engine entry points (resolved via dlsym) ---- */
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
static void (*p_backbutton_fn)(JNIEnv*, jclass);

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

/* ---- cursor drawn on fb0 (shared with SDL mali-fbdev surface) ---- */
static int g_pad_x = 320, g_pad_y = 240;   /* virtual finger (defined in pad section) */
static int g_pad_down = 0;
static int g_cursor_fb = -1;
static unsigned char* g_cursor_map = NULL;

static void draw_cursor_on_fb(void) {
    if (!getenv("GOF_SHOW_CURSOR")) return;
    if (g_cursor_fb < 0) {
        const char* p = getenv("GOF_FB");
        g_cursor_fb = open(p ? p : "/dev/fb0", O_RDWR);
        if (g_cursor_fb >= 0)
            g_cursor_map = mmap(NULL, g_width * g_height * 4,
                                PROT_READ | PROT_WRITE, MAP_SHARED, g_cursor_fb, 0);
    }
    if (!g_cursor_map) return;
    int cx = g_pad_x, cy = g_pad_y;
    if (cx < 0 || cy < 0 || cx >= g_width || cy >= g_height) return;
    for (int i = -12; i <= 12; i++) {
        for (int t = 0; t < 3; t++) {
            int x1 = cx + i, y1 = cy - t + 1;
            int x2 = cx - t + 1, y2 = cy + i;
            unsigned char* p1 = g_cursor_map + ((size_t)y1 * g_width + x1) * 4;
            unsigned char* p2 = g_cursor_map + ((size_t)y2 * g_width + x2) * 4;
            if (x1 >= 0 && x1 < g_width && y1 >= 0 && y1 < g_height) { p1[2]=255; p1[1]=80; p1[0]=80; }
            if (x2 >= 0 && x2 < g_width && y2 >= 0 && y2 < g_height) { p2[2]=255; p2[1]=80; p2[0]=80; }
        }
    }
}

/* ---- gamepad: SDL2 joystick subsystem sees our uinput device ---- */
static SDL_Window* g_win = NULL;
static SDL_GameController* g_pad = NULL;

static void pad_move(int dx, int dy) {
    g_pad_x += dx; g_pad_y += dy;
    if (g_pad_x < 0) g_pad_x = 0;
    if (g_pad_y < 0) g_pad_y = 0;
    if (g_pad_x >= g_width) g_pad_x = g_width - 1;
    if (g_pad_y >= g_height) g_pad_y = g_height - 1;
}

/* ---- built-in ANBERNIC gamepad ----
 * Empirical mapping from a live capture on the device:
 *   A=b0, B=b3, X=b2, Y=b1, D-pad=hat0, left stick=a0/a1, right stick=a2/a3.
 * Left stick moves the on-screen cursor with speed proportional to
 * deflection (deadzone ~4000); D-pad steps the cursor by 10px; A is a
 * tap at the cursor, B is BackButtonPressed. */
static void add_dev_mapping(void) {
    int n = SDL_NumJoysticks();
    for (int i = 0; i < n; i++) {
        const char* nm = SDL_JoystickNameForIndex(i);
        if (!nm) continue;
        if (!strstr(nm, "ANBERNIC")) continue;
        SDL_JoystickGUID g = SDL_JoystickGetDeviceGUID(i);
        char guid[64];
        SDL_JoystickGetGUIDString(g, guid, sizeof(guid));
        char map[512];
        snprintf(map, sizeof(map),
            "%s,%s,"
            "a:b0,b:b3,x:b2,y:b1,"
            "leftshoulder:b7,rightshoulder:b6,lefttrigger:b5,righttrigger:b4,"
            "back:b12,start:b9,"
            "dpup:h0.1,dpright:h0.2,dpdown:h0.4,dpleft:h0.8,"
            "leftx:a0,lefty:a1,rightx:a2,righty:a3,"
            "platform:Linux",
            guid, nm);
        if (SDL_GameControllerAddMapping(map) > 0)
            fprintf(stderr, "[pad] mapped %s (%s)\n", nm, guid);
    }
}

static SDL_GameController* find_pad(void) {
    int n = SDL_NumJoysticks();
    for (int i = 0; i < n; i++) {
        const char* nm = SDL_JoystickNameForIndex(i);
        if (nm && strstr(nm, "ANBERNIC")) {
            SDL_GameController* c = SDL_GameControllerOpen(i);
            if (c) fprintf(stderr, "[pad] opened %s (idx %d)\n", nm, i);
            return c;
        }
    }
    if (n > 0) {
        SDL_GameController* c = SDL_GameControllerOpen(0);
        if (c) fprintf(stderr, "[pad] opened fallback controller 0\n");
        return c;
    }
    return NULL;
}

static void handle_btn(JNIEnv* env, jclass cls, int ctrl_btn, int down) {
    switch (ctrl_btn) {
    case SDL_CONTROLLER_BUTTON_DPAD_UP:    if (down) pad_move(0, -10); break;
    case SDL_CONTROLLER_BUTTON_DPAD_DOWN:  if (down) pad_move(0, +10); break;
    case SDL_CONTROLLER_BUTTON_DPAD_LEFT:  if (down) pad_move(-10, 0); break;
    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: if (down) pad_move(+10, 0); break;
    case SDL_CONTROLLER_BUTTON_A:
        if (down && !g_pad_down) {
            g_pad_down = 1;
            p_handleTouchEvent(env, cls, 722, 0, g_pad_x, g_pad_y);
            fprintf(stderr, "[pad] touch down %d,%d\n", g_pad_x, g_pad_y);
        } else if (!down && g_pad_down) {
            g_pad_down = 0;
            p_handleTouchEvent(env, cls, 722, 1, g_pad_x, g_pad_y);
            fprintf(stderr, "[pad] touch up %d,%d\n", g_pad_x, g_pad_y);
        }
        break;
    case SDL_CONTROLLER_BUTTON_B:
        if (down && p_backbutton_fn) {
            fprintf(stderr, "[pad] back\n");
            p_backbutton_fn(env, cls);
        }
        break;
    }
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

static void pump_sdl_input(JNIEnv* env, jclass cls) {
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        switch (ev.type) {
        case SDL_CONTROLLERBUTTONDOWN:
            handle_btn(env, cls, ev.cbutton.button, 1);
            break;
        case SDL_CONTROLLERBUTTONUP:
            handle_btn(env, cls, ev.cbutton.button, 0);
            break;
        case SDL_CONTROLLERAXISMOTION: {
            const int v = ev.caxis.value;
            const int dz = 4000;
            if (abs(v) <= dz) break;
            int sp = (abs(v) - dz) * 10 / (32767 - dz);
            if (sp < 1) sp = 1;
            if (v < 0) sp = -sp;
            if (ev.caxis.axis == SDL_CONTROLLER_AXIS_LEFTX)
                pad_move(sp, 0);
            else if (ev.caxis.axis == SDL_CONTROLLER_AXIS_LEFTY)
                pad_move(0, sp);
            break;
        }
        case SDL_KEYDOWN:
            if (ev.key.repeat) break;
            switch (ev.key.keysym.sym) {
            case SDLK_UP:    pad_move(0, -10); break;
            case SDLK_DOWN:  pad_move(0, +10); break;
            case SDLK_LEFT:  pad_move(-10, 0); break;
            case SDLK_RIGHT: pad_move(+10, 0); break;
            case SDLK_RETURN: case SDLK_SPACE:
                p_handleTouchEvent(env, cls, 722, 0, g_pad_x, g_pad_y);
                p_handleTouchEvent(env, cls, 722, 1, g_pad_x, g_pad_y);
                fprintf(stderr, "[pad] key tap %d,%d\n", g_pad_x, g_pad_y);
                break;
            case SDLK_BACKSPACE: case SDLK_ESCAPE:
                if (p_backbutton_fn) { fprintf(stderr, "[pad] back\n"); p_backbutton_fn(env, cls); }
                break;
            }
            break;
        }
    }
}

/* ---- SDL2 window + GL context ---- */
static int sdl_video_init(void) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0) {
        fprintf(stderr, "[host] SDL_Init: %s\n", SDL_GetError());
        return -1;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
    g_win = SDL_CreateWindow("GOF2HD", 0, 0, g_width, g_height, SDL_WINDOW_OPENGL);
    if (!g_win) {
        fprintf(stderr, "[host] SDL_CreateWindow: %s\n", SDL_GetError());
        return -1;
    }
    SDL_GLContext gl = SDL_GL_CreateContext(g_win);
    if (!gl) {
        fprintf(stderr, "[host] SDL_GL_CreateContext: %s\n", SDL_GetError());
        return -1;
    }
    SDL_GL_MakeCurrent(g_win, gl);
    SDL_GL_SetSwapInterval(0);
    printf("[host] SDL window ready (video driver: %s)\n",
           SDL_GetCurrentVideoDriver());
    int nj = SDL_NumJoysticks();
    fprintf(stderr, "[pad] %d joystick(s):\n", nj);
    for (int i = 0; i < nj; i++) {
        const char* nm = SDL_JoystickNameForIndex(i);
        SDL_JoystickGUID g = SDL_JoystickGetDeviceGUID(i);
        char guid[64];
        SDL_JoystickGetGUIDString(g, guid, sizeof(guid));
        SDL_Joystick* j = SDL_JoystickOpen(i);
        int nb = j ? SDL_JoystickNumButtons(j) : -1;
        int na = j ? SDL_JoystickNumAxes(j) : -1;
        int nh = j ? SDL_JoystickNumHats(j) : -1;
        if (j) SDL_JoystickClose(j);
        fprintf(stderr, "[pad]   %d: name='%s' guid=%s buttons=%d axes=%d hats=%d\n",
                i, nm ? nm : "?", guid, nb, na, nh);
    }
    return 0;
}

static void sdl_swap(void) {
    SDL_GL_SwapWindow(g_win);
    draw_cursor_on_fb();
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
    install_dump_handler();

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
    p_backbutton_fn = (typeof(p_backbutton_fn))
        dlsym(h, "Java_net_fishlabs_gof2hdallandroid2012_ToJNI_BackButtonPressed");
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
        printf("[host] setOrigamiSuperClub(...)\n");
        p_setOrigamiSuperClub(env, cls, (jstring)mk_jstring(origami));
    }

    printf("[host] init SDL/EGL...\n");
    if (sdl_video_init() != 0 && getenv("GOF_EGL_REQUIRED"))
        return 1;

    printf("[host] initialize(%d, %d)\n", g_width, g_height);
    p_initialize(env, cls, g_width, g_height);
    printf("[host] resize(%d, %d)\n", g_width, g_height);
    p_resize(env, cls, g_width, g_height);

    add_dev_mapping();
    g_pad = find_pad();
    if (g_pad)
        fprintf(stderr, "[pad] gamepad ready\n");
    else
        fprintf(stderr, "[pad] no gamepad found\n");

    printf("[host] render loop started\n");
    open_touch_fifo();
    int frames = 0;
    while (1) {
        long t0 = now_ms();
        pump_touch(env, cls);
        pump_sdl_input(env, cls);
        p_renderstep(env, cls, t0);
        sdl_swap();
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