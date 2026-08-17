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
#include "host_config.h"
#include "config.h"
#include "engine_bridge.h"
#include "touch_fifo.h"
#include "jni.h"
#include "wrap_overlay.h"
#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <signal.h>
#include <execinfo.h>
#include <pthread.h>
#include <sys/stat.h>

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

static int g_width;
static int g_height;

static long now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/* ---- watchdog: catch render-loop stall and dump all-thread stacks ----
 * The engine hangs (memory pressure / GPU OOM / spin) freeze the whole
 * process; SSH is usually dead by then too.  A dedicated thread inside the
 * host notices that renderstep stopped returning and runs gcore + gdb on
 * itself, so the dump lands on disk even while the device looks frozen.
 * It also watches its own VmRSS every second: a jump of >100MB in 1s is the
 * GPU-memory spike that precedes the OOM kill, and dumps maps/smaps first. */
static volatile long g_last_render_ms = 0;
static volatile int  g_watchdog_armed = 0;

static long read_vmrss_kb(void) {
    FILE* f = fopen("/proc/self/status", "r");
    if (!f) return -1;
    char line[128];
    long v = -1;
    while (fgets(line, sizeof(line), f)) {
        if (sscanf(line, "VmRSS: %ld", &v) == 1) break;
    }
    fclose(f);
    return v;
}

static void wd_dump(const char* tag) {
    /* Write maps/status directly from this thread (no fork — fork can fail
     * under memory pressure).  gdb attach is best-effort via system(). */
    char base[320];
    snprintf(base, sizeof(base), "/root/gof2hd/hang_capture/%s_%d", tag, (int)getpid());
    FILE* out;
    char line[512];

    /* maps -> base.maps (never overwrite: most important) */
    {
        FILE* src = fopen("/proc/self/maps", "r");
        char fn[360];
        snprintf(fn, sizeof(fn), "%s.maps", base);
        out = fopen(fn, "w");
        if (src && out) {
            while (fgets(line, sizeof(line), src)) fputs(line, out);
            fclose(src);
        }
        if (out) fclose(out);
    }
    /* smaps -> base.smaps (physical pages per mapping — what we diff) */
    {
        FILE* src = fopen("/proc/self/smaps", "r");
        char fn[360];
        snprintf(fn, sizeof(fn), "%s.smaps", base);
        out = fopen(fn, "w");
        if (src && out) {
            while (fgets(line, sizeof(line), src)) fputs(line, out);
            fclose(src);
        }
        if (out) fclose(out);
    }
    /* VBO lifecycle tracker from the GL bridge (driver investigation) */
    gof_vbo_tracker_dump();
    /* status -> base.status */
    {
        FILE* src = fopen("/proc/self/status", "r");
        char fn[360];
        snprintf(fn, sizeof(fn), "%s.status", base);
        out = fopen(fn, "w");
        if (src && out) {
            while (fgets(line, sizeof(line), src))
                if (!strncmp(line, "Vm", 2) || !strncmp(line, "Threads", 7) ||
                    !strncmp(line, "Rss", 3))
                    fputs(line, out);
            fclose(src);
        }
        if (out) fclose(out);
    }
    /* gdb all-thread backtrace, best-effort; clear LD_PRELOAD (host preloads
     * are 32-bit and a 64-bit gdb chokes on them).  Run ASYNC so this thread
     * is never blocked and always reaches the self-kill below. */
    {
        char cmd[512];
        snprintf(cmd, sizeof(cmd),
                 "LD_PRELOAD= gdb -p %d -batch -ex 'set pagination off' "
                 "-ex 'thread apply all bt' > %s.bt 2>&1 &",
                 (int)getpid(), base);
        system(cmd);
    }
    fprintf(stderr, "[watchdog] dump %s written\n", base);
    fflush(stderr);
}

static void* watchdog_fn(void* arg) {
    (void)arg;
    mkdir("/root/gof2hd/hang_capture", 0777);
    long prev_vmrss = -1;
    long last_baseline = 0;
    for (;;) {
        usleep(200000);
        if (!g_watchdog_armed) continue;

        long v = read_vmrss_kb();
        long now = now_ms();

        /* keep a rolling "stable-state" smaps (overwrite every 60s) so we can
         * diff physical pages before vs at the hang */
        if (now - last_baseline > 60000) {
            last_baseline = now;
            FILE* src = fopen("/proc/self/smaps", "r");
            FILE* dst = fopen("/root/gof2hd/hang_capture/baseline.smaps", "w");
            if (src && dst) {
                char line[512];
                while (fgets(line, sizeof(line), src)) fputs(line, dst);
                fclose(src);
            }
            if (dst) fclose(dst);
        }

        if (prev_vmrss > 0 && v > 0 && v - prev_vmrss > 60000) {
            fprintf(stderr, "[watchdog] VmRSS spike +%ldMB -> %ldMB, dumping\n",
                    (v - prev_vmrss) / 1024, v / 1024);
            fflush(stderr);
            wd_dump("spike");
            prev_vmrss = v;
            usleep(1000000);
            continue;
        }
        if (v > 0) prev_vmrss = v;

        long last = g_last_render_ms;
        if (!last) continue;
        if (now_ms() - last > 6000) {
            g_watchdog_armed = 0;
            fprintf(stderr, "[watchdog] render loop stalled (%ld ms ago), dumping...\n",
                    (long)(now_ms() - last));
            fflush(stderr);
            wd_dump("stall");
            fprintf(stderr, "[watchdog] dump done; terminating self so the device recovers\n");
            fflush(stderr);
            usleep(3000000);
            kill(getpid(), SIGKILL);
            return NULL;
        }
    }
    return NULL;
}

static void watchdog_start(void) {
    pthread_t t;
    pthread_create(&t, NULL, watchdog_fn, NULL);
}

/* ---- D-pad hold flags (backend for the wrapper input vector) ----
 * The on-screen cursor / accelerometer state itself lives in wrap_overlay
 * (WrawState); gof2hd.c only feeds it: normalized stick + D-pad flags. */
static int g_dpad[4];              /* up / down / left / right hold flags */

/* ---- gamepad: SDL2 joystick subsystem sees our uinput device ---- */
static SDL_Window* g_win = NULL;
static SDL_GameController* g_pad = NULL;

static float gyro_axis(int v) {
    const int dz = GOF_AXIS_DEADZONE;
    if (abs(v) <= dz) return 0.0f;
    float n = (float)(v > 0 ? v - dz : v + dz) / (32767 - dz);
    if (n > 1.0f) n = 1.0f; else if (n < -1.0f) n = -1.0f;
    return n;
}

/* ---- built-in ANBERNIC gamepad ----
 * Empirical mapping from a live capture on the device:
 *   A=b0, B=b1, X=b3, Y=b2, L1=b4, R1=b5, L2=b10, R2=b11, D-pad=hat0,
 *   left stick=a0/a1, right stick=a2/a3.
 * The D-pad duplicates the left stick (consoles without analog sticks):
 * both feed the same normalized input vector in wrap_overlay — in cursor
 * mode it moves the on-screen cursor, in gyro mode it drives the
 * accelerometer.  START toggles the mode. */
static void add_dev_mapping(void) {
    int n = SDL_NumJoysticks();
    for (int i = 0; i < n; i++) {
        const char* nm = SDL_JoystickNameForIndex(i);
        if (!nm) continue;
        if (!strstr(nm, GOF_GAMEPAD_NAME)) continue;
        SDL_JoystickGUID g = SDL_JoystickGetDeviceGUID(i);
        char guid[64];
        SDL_JoystickGetGUIDString(g, guid, sizeof(guid));
        char map[512];
        snprintf(map, sizeof(map),
            "%s,%s,"
            "a:b0,b:b1,x:b3,y:b2,"
            "leftshoulder:b4,rightshoulder:b5,"
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
        if (nm && strstr(nm, GOF_GAMEPAD_NAME)) {
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
    case SDL_CONTROLLER_BUTTON_Y:
        overlay_input_button(WRAW_BTN_Y, down);
        break;
    case SDL_CONTROLLER_BUTTON_START:
        overlay_input_button(WRAW_BTN_START, down);
        break;
    case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:
        overlay_input_button(WRAW_BTN_L1, down);
        break;
    case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:
        overlay_input_button(WRAW_BTN_R1, down);
        break;
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

}

static void handle_raw_button(int raw_button, int down) {
    if (raw_button == GOF_B_RAW_BUTTON)
        overlay_input_button(WRAW_BTN_B, down);
    else if (raw_button == GOF_Y_RAW_BUTTON)
        overlay_input_button(WRAW_BTN_Y, down);
    else if (raw_button == GOF_L2_RAW_BUTTON)
        overlay_input_button(WRAW_BTN_L2, down);
    else if (raw_button == GOF_R2_RAW_BUTTON)
        overlay_input_button(WRAW_BTN_R2, down);
}

static void pump_sdl_input(void) {
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        switch (ev.type) {
        case SDL_JOYBUTTONDOWN:
        case SDL_JOYBUTTONUP: {
            SDL_Joystick* j = g_pad
                ? SDL_GameControllerGetJoystick(g_pad)
                : NULL;
            /* This port has one built-in joystick.  Some older SDL builds
             * report the device index in which rather than the instance ID;
             * do not discard its button event on that mismatch. */
            if (j)
                fprintf(stderr, "[pad] raw b%d %s\n", ev.jbutton.button,
                        ev.type == SDL_JOYBUTTONDOWN ? "down" : "up");
            if (j)
                handle_raw_button(ev.jbutton.button,
                                  ev.type == SDL_JOYBUTTONDOWN);
            break;
        }
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
    if (g_width <= 0 || g_height <= 0) {
        fprintf(stderr, "[host] SDL returned invalid window size %dx%d\n",
                g_width, g_height);
        return -1;
    }
    /* GOF_DISPLAY=WxH overrides the size reported to the engine.  The engine
     * picks texture variants and its internal render resolution from this, so
     * a smaller value can cut GPU memory on low-RAM devices (default fb res). */
    {
        const char* env = getenv("GOF_DISPLAY");
        if (env) {
            int w = 0, h = 0;
            if (sscanf(env, "%dx%d", &w, &h) == 2 && w > 0 && h > 0) {
                g_width = w;
                g_height = h;
                fprintf(stderr, "[host] GOF_DISPLAY override: %dx%d\n", w, h);
            }
        }
    }
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

/* EXPERIMENT (GOF_GLFINISH=N): call glFinish() every N frames from the host.
 * Tests whether pending GPU work keeps freed resources physically pinned
 * (if yes, mali0/VmRSS growth slows).  glFinish is exported by our bridge. */
__attribute__((pcs("aapcs"))) void glFinish(void);
void gof_vbo_tracker_dump(void);   /* exported by the GL bridge */
static void maybe_glfinish(int frames) {
    static int fin_every = -1;
    if (fin_every < 0) {
        const char* e = getenv("GOF_GLFINISH");
        fin_every = e ? atoi(e) : 0;
        if (fin_every > 0) fprintf(stderr, "[host] GOF_GLFINISH=%d (periodic glFinish)\n", fin_every);
    }
    if (fin_every > 0 && (frames % fin_every) == 0)
        glFinish();
}

int main(int argc, char** argv) {
    HostConfig config = config_parse(argc, argv);
    if (!config.valid) {
        config_print_usage(&config);
        return 2;
    }
    if (config.verbose_jni) g_jni_verbose = 1;
    setbuf(stdout, NULL);
    setbuf(stderr, NULL);
    if (!config.disable_crash_handler) install_crash_handler();
    install_dump_handler();
    watchdog_start();

    EngineBridge engine;
    TouchFifo touch_fifo;

    if (engine_bridge_load(&engine) != 0)
        return 1;
    if (engine_bridge_configure(&engine, config.apk_path, config.obb_path,
                                config.data_dir) != 0)
        return 1;
    if (config.gyro_mode)
        overlay_set_mode(WRAW_MODE_GYRO);

    printf("[host] init SDL/EGL...\n");
    if (sdl_video_init() != 0)
        return 1;

    if (overlay_enabled() && !overlay_init(g_width, g_height)) {
        fprintf(stderr, "[host] overlay init failed, cursor disabled\n");
    }
    overlay_set_send(engine_bridge_send_input, g_width, g_height);
    if (engine_bridge_initialize(&engine, g_width, g_height) != 0)
        return 1;

    add_dev_mapping();
    g_pad = find_pad();
    if (g_pad)
        fprintf(stderr, "[pad] gamepad ready\n");
    else
        fprintf(stderr, "[pad] no gamepad found\n");

    printf("[host] render loop started\n");
    touch_fifo_open(&touch_fifo);
    int frames = 0;
    g_watchdog_armed = 1;
    while (1) {
        long t0 = now_ms();
        touch_fifo_pump(&touch_fifo, engine_bridge_send_touch, &engine);
        pump_sdl_input();
        pump_input_vector();
        overlay_pump();
        engine_bridge_render(&engine, t0);
        sdl_swap();
        frames++;
        maybe_glfinish(frames);
        g_last_render_ms = now_ms();
        if (frames % 120 == 0)
            printf("[host] %d frames; logo=%d menu=%d exit=%d\n",
                   frames, engine_bridge_logo_shown(&engine),
                   engine_bridge_in_main_menu(&engine),
                   engine_bridge_exit_flag(&engine));
        if (engine_bridge_exit_flag(&engine) == -1) break;
        long el = now_ms() - t0;
        if (el < GOF_FRAME_PERIOD_MS)
            usleep((GOF_FRAME_PERIOD_MS - el) * 1000);
    }
    touch_fifo_close(&touch_fifo);
    printf("[host] exit requested\n");
    return 0;
}
