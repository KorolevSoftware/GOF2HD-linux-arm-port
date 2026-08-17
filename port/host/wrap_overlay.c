/*
 * wrap_overlay.c — 2D GL overlay + wrapper input state for the GOF2HD host.
 *
 * The old approach drew into raw fb0 memory after SwapWindow, so the
 * fbdev driver's frame presentation wiped it asynchronously and the
 * cursor flickered.  Instead we render the reticle through the same
 * GLES2 context, after the engine's renderstep and before SwapWindow,
 * so it is part of the presented frame.
 *
 * This module also owns the wrapper input state (WrawState): the input
 * mode (cursor/gyro), the cursor (virtual finger) position, the held
 * button states and the per-frame input vector.  gof2hd.c is the input
 * backend — it applies deadzone/normalization, combines the stick and
 * the D-pad (the D-pad duplicates the stick so consoles without analog
 * sticks work too) and feeds the resulting [-1,1] vector here.
 *
 * The GL bridge libGLESv2.so exports pcs("aapcs") (softfp) entry points
 * while the host is hardfp (aapcs-vfp).  The host is linked against
 * libGLESv2.so directly, and every call site must use the softfp
 * calling convention; therefore each external declaration carries
 * __attribute__((pcs("aapcs"))), and floats (glUniformMatrix4fv /
 * glUniform4f) travel in r0-r3 exactly as the bridge entry points expect.
 *
 * The shader program is intentionally minimal: one vec2 position
 * attribute, one ortho 2D projection uniform (built once from the game
 * resolution passed to overlay_init) and one plain color uniform.  No
 * textures, no blending, no matrices in the shader itself.
 */
#include "wrap_overlay.h"
#include "host_config.h"

#include <SDL2/SDL_opengles2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* GLES2 entry points as exported by the bridge (softfp).  The linker
 * resolves these to libGLESv2.so. */
#define GLF(name, ret, ...) __attribute__((pcs("aapcs"))) ret name(__VA_ARGS__)
GLF(glGetIntegerv,            void,   GLenum, GLint*);
GLF(glViewport,               void,   GLint, GLint, GLsizei, GLsizei);
GLF(glCreateShader,           GLuint, GLenum);
GLF(glShaderSource,           void,   GLuint, GLsizei, const GLchar* const*, const GLint*);
GLF(glCompileShader,          void,   GLuint);
GLF(glGetShaderiv,            void,   GLuint, GLenum, GLint*);
GLF(glCreateProgram,          GLuint, void);
GLF(glAttachShader,           void,   GLuint, GLuint);
GLF(glLinkProgram,            void,   GLuint);
GLF(glGetProgramiv,           void,   GLuint, GLenum, GLint*);
GLF(glDeleteShader,           void,   GLuint);
GLF(glGetUniformLocation,     GLint,  GLuint, const GLchar*);
GLF(glGetAttribLocation,      GLint,  GLuint, const GLchar*);
GLF(glUseProgram,             void,   GLuint);
GLF(glGenBuffers,             void,   GLsizei, GLuint*);
GLF(glBindBuffer,             void,   GLenum, GLuint);
GLF(glBufferData,             void,   GLenum, GLsizeiptr, const void*, GLenum);
GLF(glVertexAttribPointer,    void,   GLuint, GLint, GLenum, GLboolean, GLsizei, const void*);
GLF(glEnableVertexAttribArray, void,  GLuint);
GLF(glDisableVertexAttribArray, void, GLuint);
GLF(glUniformMatrix4fv,       void,   GLint, GLsizei, GLboolean, const GLfloat*);
GLF(glUniform4f,              void,   GLint, GLfloat, GLfloat, GLfloat, GLfloat);
GLF(glDrawArrays,             void,   GLenum, GLint, GLsizei);
GLF(glBindFramebuffer,        void,   GLenum, GLuint);
GLF(glEnable,                 void,   GLenum);
GLF(glDisable,                void,   GLenum);
GLF(glDepthMask,              void,   GLboolean);
#undef GLF

/* ---- wrapper input state ----
 * Single owner of the per-frame input data used by both the overlay
 * renderer and the engine-driving code in gof2hd.c. */
typedef struct WrawState {
    int   mode;                 /* WRAW_MODE_CURSOR / WRAW_MODE_GYRO */
    int   cx, cy;               /* cursor (virtual finger), clamped to w/h */
    int   w, h;                 /* window size (set by overlay_init) */
    int   btn[WRAW_BTN_COUNT];  /* held state (0/1) per WrawButton */
    float vec[2];               /* ready input vector, normalized [-1,1] */
    float rem[2];               /* fractional accumulator for smooth cursor */
} WrawState;

static WrawState g_wraw;

/* ---- engine delivery ----
 * gof2hd.c registers the send callback (JNI side).  This module forms the
 * complete per-frame events here: accel every frame, tap/swipe (pid 722)
 * and fire (pid 723) on A/R2 edges, directional swipes on L1/R1, and back
 * on B edge. */
static WrawSendFn g_send;
static int g_fw, g_fh;              /* engine resolution (fire zone base) */
static int g_prev_b, g_prev_y, g_prev_l2, g_prev_r2;
static int g_prev_cx, g_prev_cy;    /* last touch point sent to the engine */

/* A swipe is deliberately spread across frames.  The engine receives the
 * normal Android-like touch sequence (down, move, move, up), which gives its
 * gesture recognizer a real displacement and a release edge to consume. */
enum { SWIPE_IDLE, SWIPE_DOWN, SWIPE_MOVE_MID, SWIPE_MOVE_END, SWIPE_UP };
static int g_swipe_pending;
static int g_swipe_phase;
static int g_swipe_x0, g_swipe_y0, g_swipe_x1, g_swipe_y1;
enum { SWIPE_LEFT = -1, SWIPE_RIGHT = 1 };
static int g_swipe_direction;

/* ---- GL objects ---- */
static GLuint   g_prog;
static GLint    g_uproj, g_ucol, g_a_pos;
static GLfloat  g_proj[16];

int overlay_enabled(void) {
    return getenv("GOF_SHOW_CURSOR") != NULL;
}

/* ---- input state ---- */

void overlay_set_mode(WrawMode mode) {
    g_wraw.mode = mode == WRAW_MODE_GYRO ? WRAW_MODE_GYRO : WRAW_MODE_CURSOR;
}

WrawMode overlay_get_mode(void) {
    return (WrawMode)g_wraw.mode;
}

void overlay_get_cursor(int* x, int* y) {
    *x = g_wraw.cx;
    *y = g_wraw.cy;
}

int overlay_get_btn(WrawButton btn) {
    if (btn < 0 || btn >= WRAW_BTN_COUNT) return 0;
    return g_wraw.btn[btn];
}

static void queue_swipe(int direction) {
    /* Keep the current gesture intact.  A bumper press during the gesture
     * is ignored rather than interleaving two pointer sequences. */
    if (g_swipe_pending || g_swipe_phase != SWIPE_IDLE)
        return;
    g_swipe_pending = 1;
    g_swipe_direction = direction;
}

void overlay_input_button(WrawButton btn, int down) {
    if (btn < 0 || btn >= WRAW_BTN_COUNT) return;
    if (btn == WRAW_BTN_START && down) {
        overlay_set_mode((WrawMode)!g_wraw.mode);
        fprintf(stderr, "[ove] gyro mode %s\n",
                g_wraw.mode ? "ON (input vector -> accelerometer)" : "OFF");
    }
    if (down && !g_wraw.btn[btn]) {
        if (btn == WRAW_BTN_L1)
            queue_swipe(SWIPE_LEFT);
        else if (btn == WRAW_BTN_R1)
            queue_swipe(SWIPE_RIGHT);
    }
    g_wraw.btn[btn] = down ? 1 : 0;
}

void overlay_input_vector(float nx, float ny) {
    if (nx > 1.0f) nx = 1.0f; else if (nx < -1.0f) nx = -1.0f;
    if (ny > 1.0f) ny = 1.0f; else if (ny < -1.0f) ny = -1.0f;
    g_wraw.vec[0] = nx;
    g_wraw.vec[1] = ny;
    if (g_wraw.mode != WRAW_MODE_CURSOR) return;  /* gyro: cursor stays put */
    if (!g_wraw.w || !g_wraw.h) return;

    /* speed proportional to deflection, fractional accumulator for a
     * smooth crawl at small deflections (~8 px/frame at full stick) */
    const float k = 16.0f;
    g_wraw.rem[0] += nx * k;
    g_wraw.rem[1] += ny * k;
    int dx = (int)g_wraw.rem[0]; g_wraw.rem[0] -= dx;
    int dy = (int)g_wraw.rem[1]; g_wraw.rem[1] -= dy;
    if (!dx && !dy) return;

    g_wraw.cx += dx;
    g_wraw.cy += dy;
    if (g_wraw.cx < 0) g_wraw.cx = 0;
    if (g_wraw.cy < 0) g_wraw.cy = 0;
    if (g_wraw.cx >= g_wraw.w) g_wraw.cx = g_wraw.w - 1;
    if (g_wraw.cy >= g_wraw.h) g_wraw.cy = g_wraw.h - 1;
    fprintf(stderr, "[pad] cursor %d,%d\n", g_wraw.cx, g_wraw.cy);
}

void overlay_get_gyro(float* ax, float* ay, float* az) {
    if (g_wraw.mode == WRAW_MODE_GYRO) {
        /* Engine: SetAccelValue(-a, b, c) == (X, Y, Z).
         *   steer uses accel[1] (Y) = b -> -vector X
         *   pitch uses accel[2] (Z) = c. */
        *ax = 0.0f;
        *ay = -g_wraw.vec[0];
        *az = -g_wraw.vec[1];
    } else {
        *ax = *ay = 0.0f;
        *az = 0.0f;
    }
}

void overlay_set_send(WrawSendFn fn, int width, int height) {
    g_send = fn;
    g_fw = width;
    g_fh = height;
}

static int clamp_int(int value, int low, int high) {
    if (value < low) return low;
    if (value > high) return high;
    return value;
}

static void prepare_swipe(void) {
    int distance;
    int max_distance;
    int cx, cy;

    if (!g_swipe_pending || !g_wraw.w || !g_wraw.h)
        return;

    overlay_get_cursor(&cx, &cy);
    max_distance = (g_wraw.w - 1) / 2;
    distance = g_wraw.w / 4;
    if (distance < 1) distance = 1;
    if (distance > max_distance) distance = max_distance;

    cy = clamp_int(cy, 0, g_wraw.h - 1);
    if (g_swipe_direction == SWIPE_RIGHT) {
        g_swipe_x0 = clamp_int(cx, 0, g_wraw.w - 1 - distance);
        g_swipe_x1 = g_swipe_x0 + distance;
    } else {
        g_swipe_x0 = clamp_int(cx, distance, g_wraw.w - 1);
        g_swipe_x1 = g_swipe_x0 - distance;
    }
    g_swipe_y0 = cy;
    g_swipe_y1 = cy;
    g_swipe_pending = 0;
    g_swipe_phase = SWIPE_DOWN;
}

static void pump_swipe(void) {
    WrawInputEvent ev;
    int x, y, action;

    prepare_swipe();
    if (g_swipe_phase == SWIPE_IDLE)
        return;

    switch (g_swipe_phase) {
    case SWIPE_DOWN:
        x = g_swipe_x0; y = g_swipe_y0; action = 0;
        g_swipe_phase = SWIPE_MOVE_MID;
        break;
    case SWIPE_MOVE_MID:
        x = (g_swipe_x0 + g_swipe_x1) / 2; y = g_swipe_y0; action = 2;
        g_swipe_phase = SWIPE_MOVE_END;
        break;
    case SWIPE_MOVE_END:
        x = g_swipe_x1; y = g_swipe_y1; action = 2;
        g_swipe_phase = SWIPE_UP;
        break;
    default:
        x = g_swipe_x1; y = g_swipe_y1; action = 1;
        g_swipe_phase = SWIPE_IDLE;
        break;
    }

    ev.kind = WRAW_EV_TOUCH;
    ev.u.touch.pid = 722;
    ev.u.touch.action = action;
    ev.u.touch.x = x;
    ev.u.touch.y = y;
    g_send(&ev);
    if (getenv("GOF_VERBOSE_JNI"))
        fprintf(stderr, "[pad] swipe %s act=%d %d,%d\n",
                g_swipe_direction == SWIPE_RIGHT ? "right" : "left",
                action, x, y);
}

/* Form and emit the per-frame input: accelerometer values each frame, and
 * touch/back events on button edges (only held states live in WrawState). */
void overlay_pump(void) {
    if (!g_send) return;
    WrawInputEvent ev;

    ev.kind = WRAW_EV_ACCEL;
    overlay_get_gyro(&ev.u.accel.x, &ev.u.accel.y, &ev.u.accel.z);
    g_send(&ev);

    pump_swipe();

    int cx, cy;
    overlay_get_cursor(&cx, &cy);

    int l2 = overlay_get_btn(WRAW_BTN_L2);
    if (l2 && !g_prev_l2) {
        ev.kind = WRAW_EV_TOUCH;
        ev.u.touch.pid = 722; ev.u.touch.action = 0;
        ev.u.touch.x = GOF_L2_CLICK_X; ev.u.touch.y = GOF_L2_CLICK_Y;
        g_send(&ev);
        ev.u.touch.action = 1;
        g_send(&ev);
        fprintf(stderr, "[pad] L2 click %d,%d\n",
                GOF_L2_CLICK_X, GOF_L2_CLICK_Y);
    }
    g_prev_l2 = l2;

    int r2 = overlay_get_btn(WRAW_BTN_R2);
    if (overlay_get_mode() == WRAW_MODE_CURSOR) {
        /* Cursor mode: R2 is the A-style virtual finger.  A remains mapped
         * but intentionally has no action until a later binding is chosen. */
        if (r2 && !g_prev_r2) {
            g_prev_cx = cx; g_prev_cy = cy;
            ev.kind = WRAW_EV_TOUCH;
            ev.u.touch.pid = 722; ev.u.touch.action = 0;
            ev.u.touch.x = cx; ev.u.touch.y = cy;
            g_send(&ev);
            fprintf(stderr, "[pad] R2 touch down %d,%d\n", cx, cy);
        } else if (!r2 && g_prev_r2) {
            ev.kind = WRAW_EV_TOUCH;
            ev.u.touch.pid = 722; ev.u.touch.action = 1;
            ev.u.touch.x = cx; ev.u.touch.y = cy;
            g_send(&ev);
            fprintf(stderr, "[pad] R2 touch up %d,%d\n", cx, cy);
        } else if (r2 && (cx != g_prev_cx || cy != g_prev_cy)) {
            g_prev_cx = cx; g_prev_cy = cy;
            ev.kind = WRAW_EV_TOUCH;
            ev.u.touch.pid = 722; ev.u.touch.action = 2;
            ev.u.touch.x = cx; ev.u.touch.y = cy;
            g_send(&ev);
            if (getenv("GOF_VERBOSE_JNI"))
                fprintf(stderr, "[pad] R2 touch move %d,%d\n", cx, cy);
        }
    } else if (r2 && !g_prev_r2) {
        ev.kind = WRAW_EV_TOUCH;
        ev.u.touch.pid = 723; ev.u.touch.action = 0;
        ev.u.touch.x = g_fw - g_fw / 8; ev.u.touch.y = g_fh - g_fh / 8;
        g_send(&ev);
        fprintf(stderr, "[pad] fire down\n");
    } else if (!r2 && g_prev_r2) {
        ev.kind = WRAW_EV_TOUCH;
        ev.u.touch.pid = 723; ev.u.touch.action = 1;
        ev.u.touch.x = g_fw - g_fw / 8; ev.u.touch.y = g_fh - g_fh / 8;
        g_send(&ev);
        fprintf(stderr, "[pad] fire up\n");
    }
    g_prev_r2 = r2;

    int b = overlay_get_btn(WRAW_BTN_B);
    if (b && !g_prev_b) {
        ev.kind = WRAW_EV_TOUCH;
        ev.u.touch.pid = 722; ev.u.touch.action = 0;
        ev.u.touch.x = GOF_B_CLICK_X; ev.u.touch.y = GOF_B_CLICK_Y;
        g_send(&ev);
        ev.u.touch.action = 1;
        g_send(&ev);
        fprintf(stderr, "[pad] B click %d,%d\n",
                GOF_B_CLICK_X, GOF_B_CLICK_Y);
    }
    g_prev_b = b;

    int y = overlay_get_btn(WRAW_BTN_Y);
    if (y && !g_prev_y) {
        ev.kind = WRAW_EV_TOUCH;
        ev.u.touch.pid = 722; ev.u.touch.action = 0;
        ev.u.touch.x = GOF_Y_CLICK_X; ev.u.touch.y = GOF_Y_CLICK_Y;
        g_send(&ev);
        fprintf(stderr, "[pad] Y down %d,%d\n",
                GOF_Y_CLICK_X, GOF_Y_CLICK_Y);
    } else if (!y && g_prev_y) {
        ev.kind = WRAW_EV_TOUCH;
        ev.u.touch.pid = 722; ev.u.touch.action = 1;
        ev.u.touch.x = GOF_Y_CLICK_X; ev.u.touch.y = GOF_Y_CLICK_Y;
        g_send(&ev);
        fprintf(stderr, "[pad] Y up %d,%d\n",
                GOF_Y_CLICK_X, GOF_Y_CLICK_Y);
    }
    g_prev_y = y;
}

/* ---- shader + draw ---- */

static const char VSH[] =
    "attribute vec2 aPos;\n"
    "uniform mat4 uProj;\n"
    "void main() { gl_Position = uProj * vec4(aPos, 0.0, 1.0); }\n";

static const char FSH[] =
    "precision mediump float;\n"
    "uniform vec4 uColor;\n"
    "void main() { gl_FragColor = uColor; }\n";

static GLuint compile_shader(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    if (!s) return 0;
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) return 0;
    return s;
}

int overlay_init(int width, int height) {
    if (width <= 0 || height <= 0) return 0;
    g_wraw.w = width;
    g_wraw.h = height;
    g_wraw.cx = width / 2;
    g_wraw.cy = height / 2;
    fprintf(stderr, "[pad] cursor %d,%d\n", g_wraw.cx, g_wraw.cy);

    GLuint vs = compile_shader(GL_VERTEX_SHADER, VSH);
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, FSH);
    if (!vs || !fs) return 0;

    g_prog = glCreateProgram();
    glAttachShader(g_prog, vs);
    glAttachShader(g_prog, fs);
    glLinkProgram(g_prog);
    GLint ok = 0;
    glGetProgramiv(g_prog, GL_LINK_STATUS, &ok);
    if (!ok) { fprintf(stderr, "[ove] program link failed\n"); return 0; }
    glDeleteShader(vs);
    glDeleteShader(fs);

    g_uproj = glGetUniformLocation(g_prog, "uProj");
    g_ucol  = glGetUniformLocation(g_prog, "uColor");
    g_a_pos = glGetAttribLocation(g_prog, "aPos");
    if (g_uproj < 0 || g_ucol < 0 || g_a_pos < 0) {
        fprintf(stderr, "[ove] uniform/attrib location lookup failed\n");
        return 0;
    }

    /* ortho 2D projection: screen top-left origin, x in [0,w], y in [0,h].
     * Column-major as GL wants: x_ndc = 2x/w - 1, y_ndc = 1 - 2y/h. */
    memset(g_proj, 0, sizeof(g_proj));
    g_proj[0]  = 2.0f / (float)width;
    g_proj[5]  = -2.0f / (float)height;
    g_proj[10] = 1.0f;
    g_proj[12] = -1.0f;
    g_proj[13] = 1.0f;
    g_proj[15] = 1.0f;

    fprintf(stderr, "[ove] overlay ready %dx%d\n", width, height);
    return 1;
}

void overlay_draw(void) {
    if (!g_prog || !g_wraw.w || !g_wraw.h) return;
    if (g_wraw.mode == WRAW_MODE_GYRO) return;  /* no reticle in gyro mode */
    int x = g_wraw.cx, y = g_wraw.cy;

    /* two crossing bars, 3 px thick, half-length 12 px -> 6 segments */
    GLfloat verts[12 * 2];
    int v = 0;
    for (int t = -1; t <= 1; t++) {
        verts[v++] = (GLfloat)(x - 12); verts[v++] = (GLfloat)(y + t);
        verts[v++] = (GLfloat)(x + 12); verts[v++] = (GLfloat)(y + t);
        verts[v++] = (GLfloat)(x + t);  verts[v++] = (GLfloat)(y - 12);
        verts[v++] = (GLfloat)(x + t);  verts[v++] = (GLfloat)(y + 12);
    }

    GLint saved_prog = 0, saved_buf = 0, saved_vp[4] = {0};
    glGetIntegerv(GL_CURRENT_PROGRAM, &saved_prog);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &saved_buf);
    glGetIntegerv(GL_VIEWPORT, saved_vp);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glDisable(GL_SCISSOR_TEST);
    glDepthMask(0);
    glViewport(0, 0, g_wraw.w, g_wraw.h);

    glUseProgram(g_prog);
    /* Client-side vertex data: no VBO, no per-frame glBufferData.  A VBO +
     * glBufferData every frame re-allocates GPU storage each time; on some
     * mali drivers the old storage is not returned promptly, which makes the
     * GPU memory grow by a small allocation per frame over a long session. */
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glEnableVertexAttribArray(g_a_pos);
    glVertexAttribPointer(g_a_pos, 2, GL_FLOAT, 0, 0, verts);
    glUniformMatrix4fv(g_uproj, 1, 0, g_proj);
    glUniform4f(g_ucol, 1.0f, 0.3137f, 0.3137f, 1.0f);
    glDrawArrays(GL_LINES, 0, 12);

    glDisableVertexAttribArray(g_a_pos);
    glBindBuffer(GL_ARRAY_BUFFER, saved_buf);
    glUseProgram(saved_prog);
    glViewport(saved_vp[0], saved_vp[1], saved_vp[2], saved_vp[3]);
}
