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
 * Usage: gof2hd <apk> <obb> <dataDir>
 */
#define _GNU_SOURCE
#include "jni.h"
#include "wrap_overlay.h"
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

/* ---- crash handler ---- */
/* Print the load addresses of our .so's so pc/lr offsets can be computed
 * against the module base (ASLR changes the base each run). */
static void dump_module_maps(void) {
    FILE* f = fopen("/proc/self/maps", "r");
    if (!f) return;
    char line[512];
    static char out[512];
    while (fgets(line, sizeof(line), f)) {
        if (strstr(line, "libgof2hdaa.so") || strstr(line, "/libc.so") ||
            strstr(line, "/libm.so") || strstr(line, "/libGLESv2.so") ||
            strstr(line, "/gof2hd") || strstr(line, "libfmodev") ||
            strstr(line, "libfmodex") || strstr(line, "libOpenSLES") ||
            strstr(line, "libSDL") || strstr(line, "pthread_bionic") ||
            strstr(line, "cpuinfo_fake")) {
            char* nl = strchr(line, '\n');
            if (nl) *nl = 0;
            int n = snprintf(out, sizeof(out), "  map: %s\n", line);
            if (n > 0) write(2, out, n);
        }
    }
    fclose(f);
}

static void crash_handler(int sig, siginfo_t* si, void* uc) {
    ucontext_t* u = (ucontext_t*)uc;
    char buf[512];
    int n = snprintf(buf, sizeof(buf),
        "\n=== crash signal %d (%s), fault %p r0=%x r1=%x r2=%x r3=%x\n"
        "  r4=%x r5=%x r6=%x r7=%x r8=%x r9=%x r10=%x r11=%x fp=%x sp=%x lr=%x pc=%x\n",
        sig, strsignal(sig), si->si_addr,
        u->uc_mcontext.arm_r0, u->uc_mcontext.arm_r1, u->uc_mcontext.arm_r2,
        u->uc_mcontext.arm_r3, u->uc_mcontext.arm_r4, u->uc_mcontext.arm_r5,
        u->uc_mcontext.arm_r6, u->uc_mcontext.arm_r7, u->uc_mcontext.arm_r8,
        u->uc_mcontext.arm_r9, u->uc_mcontext.arm_r10, u->uc_mcontext.arm_fp,
        u->uc_mcontext.arm_fp, u->uc_mcontext.arm_sp, u->uc_mcontext.arm_lr,
        u->uc_mcontext.arm_pc);
    if (n > 0) write(2, buf, n);
    /* stack walk: dump words that look like code addresses (libs 0xf0-0xfa) */
    {
        unsigned sp = u->uc_mcontext.arm_sp;
        char sb[96];
        n = snprintf(sb, sizeof(sb), "  [stack walk sp=0x%08x]\n", sp);
        if (n > 0) write(2, sb, n);
        for (int i = 0; i < 256; i += 4) {
            unsigned long addr = (unsigned long)(unsigned long)sp + (unsigned)i;
            if (addr < 0x30000000ul) break;
            volatile unsigned* w = (volatile unsigned*)addr;
            unsigned v = *w;
            if (v >= 0xf0000000u && v <= 0xfaffffffu) {
                n = snprintf(sb, sizeof(sb), "  sp+0x%04x: 0x%08x\n", i, v);
                if (n > 0) write(2, sb, n);
            }
        }
    }
    dump_module_maps();
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
/*
 * Engine is softfp (bionic armeabi-v7a): floats live in r0-r3.  Our host is
 * hardfp (aapcs-vfp), so a plain function pointer would pass the jfloat args
 * in s0-s2 and the engine would read garbage from r0-r3.  Mark the pointer
 * pcs("aapcs") so the floats are placed in integer registers at the call.
 */
static void (__attribute__((pcs("aapcs"))) *p_handleAccelerometer)(JNIEnv*, jclass, jfloat, jfloat, jfloat);
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

/* ---- D-pad hold flags (backend for the wrapper input vector) ----
 * The on-screen cursor / accelerometer state itself lives in wrap_overlay
 * (WrawState); gof2hd.c only feeds it: normalized stick + D-pad flags. */
static int g_dpad[4];              /* up / down / left / right hold flags */

/* ---- gamepad: SDL2 joystick subsystem sees our uinput device ---- */
static SDL_Window* g_win = NULL;
static SDL_GameController* g_pad = NULL;

static float gyro_axis(int v) {
    const int dz = 4000;
    if (abs(v) <= dz) return 0.0f;
    float n = (float)(v > 0 ? v - dz : v + dz) / (32767 - dz);
    if (n > 1.0f) n = 1.0f; else if (n < -1.0f) n = -1.0f;
    return n;
}

/* ---- built-in ANBERNIC gamepad ----
 * Empirical mapping from a live capture on the device:
 *   A=b0, B=b3, X=b2, Y=b1, D-pad=hat0, left stick=a0/a1, right stick=a2/a3.
 * The D-pad duplicates the left stick (consoles without analog sticks):
 * both feed the same normalized input vector in wrap_overlay — in cursor
 * mode it moves the on-screen cursor, in gyro mode it drives the
 * accelerometer.  START toggles the mode. */
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
            "rightshoulder:b6,lefttrigger:b5,righttrigger:b11,"
            "back:b12,start:b7,"
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

static void handle_btn(int ctrl_btn, int down) {
    switch (ctrl_btn) {
    case SDL_CONTROLLER_BUTTON_DPAD_UP:    g_dpad[0] = down; break;
    case SDL_CONTROLLER_BUTTON_DPAD_DOWN:  g_dpad[1] = down; break;
    case SDL_CONTROLLER_BUTTON_DPAD_LEFT:  g_dpad[2] = down; break;
    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: g_dpad[3] = down; break;
    case SDL_CONTROLLER_BUTTON_A:
        overlay_input_button(WRAW_BTN_A, down);
        break;
    case SDL_CONTROLLER_BUTTON_B:
        overlay_input_button(WRAW_BTN_B, down);
        break;
    case SDL_CONTROLLER_BUTTON_START:
        overlay_input_button(WRAW_BTN_START, down);
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

/* Feed the wrapper the combined input vector once per frame: normalized
 * left stick (deadzone ~4000) + D-pad hold flags (D-pad doubles as a
 * second stick — useful on consoles without analog sticks). */
static void pump_input_vector(void) {
    float nx = 0.0f, ny = 0.0f;
    if (g_pad) {
        nx = gyro_axis(SDL_GameControllerGetAxis(g_pad, SDL_CONTROLLER_AXIS_LEFTX));
        ny = gyro_axis(SDL_GameControllerGetAxis(g_pad, SDL_CONTROLLER_AXIS_LEFTY));
    }
    /* D-pad signs follow the stick convention: up/left = -1 (matching
     * how the stick polarity was verified on the device). */
    if (g_dpad[0]) ny = -1.0f;
    if (g_dpad[1]) ny =  1.0f;
    if (g_dpad[2]) nx = -1.0f;
    if (g_dpad[3]) nx =  1.0f;
    overlay_input_vector(nx, ny);

    /* Fire: R2 (raw joystick button 11 on this console — captured: L1=4,
     * R2=11; mapping righttrigger:b11).  Polled every frame like the
     * stick, since the mapping binds a button to the trigger axis and no
     * gamecontroller button events exist for it.  Held state lives in the
     * wrapper (WRAW_BTN_R2, touch pid 723). */
    if (g_pad) {
        SDL_Joystick* j = SDL_GameControllerGetJoystick(g_pad);
        if (j) overlay_input_button(WRAW_BTN_R2, SDL_JoystickGetButton(j, 11));
    }
}

/* Delivery of the formed input events to the engine (JNI side).  The full
 * per-frame formation (cursor/gyro state, button edges, tap/swipe/fire
 * touches) happens in wrap_overlay (overlay_pump); this callback only
 * translates WrawInputEvent into the engine's JNI entry points. */
static jclass g_cls;

static void engine_input_send(const WrawInputEvent* ev) {
    switch (ev->kind) {
    case WRAW_EV_ACCEL:
        if (p_handleAccelerometer)
            p_handleAccelerometer(&g_env, g_cls,
                                  ev->u.accel.x, ev->u.accel.y, ev->u.accel.z);
        break;
    case WRAW_EV_TOUCH:
        p_handleTouchEvent(&g_env, g_cls, ev->u.touch.pid, ev->u.touch.action,
                           ev->u.touch.x, ev->u.touch.y);
        break;
    case WRAW_EV_BACK:
        if (p_backbutton_fn) p_backbutton_fn(&g_env, g_cls);
        break;
    }
}

static void pump_sdl_input(void) {
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        switch (ev.type) {
        case SDL_CONTROLLERBUTTONDOWN:
            handle_btn(ev.cbutton.button, 1);
            break;
        case SDL_CONTROLLERBUTTONUP:
            handle_btn(ev.cbutton.button, 0);
            break;
        case SDL_KEYDOWN:
            if (ev.key.repeat) break;
            switch (ev.key.keysym.sym) {
            case SDLK_UP:    g_dpad[0] = 1; break;
            case SDLK_DOWN:  g_dpad[1] = 1; break;
            case SDLK_LEFT:  g_dpad[2] = 1; break;
            case SDLK_RIGHT: g_dpad[3] = 1; break;
            case SDLK_RETURN: case SDLK_SPACE:
                overlay_input_button(WRAW_BTN_A, 1);
                overlay_input_button(WRAW_BTN_A, 0);
                break;
            case SDLK_BACKSPACE: case SDLK_ESCAPE:
                overlay_input_button(WRAW_BTN_B, 1);
                overlay_input_button(WRAW_BTN_B, 0);
                break;
            }
            break;
        case SDL_KEYUP:
            switch (ev.key.keysym.sym) {
            case SDLK_UP:    g_dpad[0] = 0; break;
            case SDLK_DOWN:  g_dpad[1] = 0; break;
            case SDLK_LEFT:  g_dpad[2] = 0; break;
            case SDLK_RIGHT: g_dpad[3] = 0; break;
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
    g_win = SDL_CreateWindow("GOF2HD", 0, 0, 0, 0,
                             SDL_WINDOW_OPENGL | SDL_WINDOW_FULLSCREEN_DESKTOP);
    if (!g_win) {
        fprintf(stderr, "[host] SDL_CreateWindow: %s\n", SDL_GetError());
        return -1;
    }
    SDL_GetWindowSize(g_win, &g_width, &g_height);
    g_display_width = g_width;
    g_display_height = g_height;
    SDL_GLContext gl = SDL_GL_CreateContext(g_win);
    if (!gl) {
        fprintf(stderr, "[host] SDL_GL_CreateContext: %s\n", SDL_GetError());
        return -1;
    }
    SDL_GL_MakeCurrent(g_win, gl);
    SDL_GL_SetSwapInterval(0);
    printf("[host] SDL window ready (video driver: %s, %dx%d fullscreen)\n",
           SDL_GetCurrentVideoDriver(), g_width, g_height);
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
    if (overlay_enabled())
        overlay_draw();
    SDL_GL_SwapWindow(g_win);
}

/*
 * Override FMOD's internal pool allocator with glibc malloc.  The Android
 * FMOD build uses a tiny internal memory pool; on Linux that pool gets
 * exhausted/corrupted (the game crashes in FMOD_EventSystem_Create with the
 * allocator returning NULL).  FMOD_Memory_Initialize is public API; the
 * callbacks take no float args so softfp/hardfp ABI does not matter.
 */
typedef void* (*fmod_alloc_fn)(unsigned int, unsigned int, const char*);
typedef void* (*fmod_realloc_fn)(void*, unsigned int, unsigned int, const char*);
typedef void  (*fmod_free_fn)(void*, unsigned int, const char*);
typedef int   (*fmod_meminit_fn)(void*, int, fmod_alloc_fn, fmod_realloc_fn,
                                 fmod_free_fn, int);

static void* g_fmod_alloc(unsigned int size, unsigned int type, const char* src)
{ (void)type; (void)src; return malloc(size); }
static void* g_fmod_realloc(void* p, unsigned int size, unsigned int type, const char* src)
{ (void)type; (void)src; return realloc(p, size); }
static void g_fmod_free(void* p, unsigned int type, const char* src)
{ (void)type; (void)src; free(p); }

static void fmod_memory_override(void) {
    void* h = dlopen("libfmodex.so", RTLD_NOW | RTLD_GLOBAL);
    if (!h) { fprintf(stderr, "[host] fmod_memory_override: no libfmodex\n"); return; }
    fmod_meminit_fn init = (fmod_meminit_fn)dlsym(h, "FMOD_Memory_Initialize");
    if (!init) { fprintf(stderr, "[host] fmod_memory_override: no FMOD_Memory_Initialize\n"); return; }
    int r = init(NULL, 0, g_fmod_alloc, g_fmod_realloc, g_fmod_free, 0);
    fprintf(stderr, "[host] FMOD_Memory_Initialize -> %d (glibc malloc)\n", r);
}

int main(int argc, char** argv) {
    if (argc < 4) {
        fprintf(stderr,
            "usage: %s <base.apk> <main.*.obb> <dataDir>\n", argv[0]);
        return 2;
    }
    const char* apk_path = argv[1];
    const char* obb_path = argv[2];
    const char* data_dir = argv[3];
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
    fmod_memory_override();
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
    if (getenv("GOF_GYRO")) overlay_set_mode(WRAW_MODE_GYRO);
    if (p_setOrigamiSuperClub) {
        static char origami[] = "0123456789ABCDEF0123456789ABCDEF";
        printf("[host] setOrigamiSuperClub(...)\n");
        p_setOrigamiSuperClub(env, cls, (jstring)mk_jstring(origami));
    }

    printf("[host] init SDL/EGL...\n");
    if (sdl_video_init() != 0)
        return 1;

    if (overlay_enabled() && !overlay_init(g_width, g_height)) {
        fprintf(stderr, "[host] overlay init failed, cursor disabled\n");
    }
    g_cls = cls;
    overlay_set_send(engine_input_send, g_width, g_height);

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
        pump_sdl_input();
        pump_input_vector();
        overlay_pump();
        p_renderstep(env, cls, t0);
        sdl_swap();
        frames++;
        if (frames % 120 == 0)
            printf("[host] %d frames; logo=%d menu=%d exit=%d\n",
                   frames, p_getLogoShown(env, cls),
                   p_isInMainMenu(env, cls), p_getExitFlag(env, cls));
        if (p_getExitFlag(env, cls) == -1) break;
        long el = now_ms() - t0;
        if (el < 33) usleep((33 - el) * 1000);
    }
    printf("[host] exit requested\n");
    return 0;
}
