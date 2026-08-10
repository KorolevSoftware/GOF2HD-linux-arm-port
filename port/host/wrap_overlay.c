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

/* ---- GL objects ---- */
static GLuint   g_prog, g_vbo;
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

void overlay_input_button(WrawButton btn, int down) {
    if (btn < 0 || btn >= WRAW_BTN_COUNT) return;
    if (btn == WRAW_BTN_START && down) {
        overlay_set_mode((WrawMode)!g_wraw.mode);
        fprintf(stderr, "[ove] gyro mode %s\n",
                g_wraw.mode ? "ON (input vector -> accelerometer)" : "OFF");
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
    const float k = 8.0f;
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
}

void overlay_get_gyro(float* ax, float* ay, float* az) {
    if (g_wraw.mode == WRAW_MODE_GYRO) {
        /* Engine: SetAccelValue(-a, b, c) == (X, Y, Z).
         *   steer uses accel[1] (Y) = b -> -vector X
         *   pitch uses accel[2] (Z) = c; neutral Z ~ +1.0 (Android sends
         *   z/10 ~ 0.98 level), so 1.0, not 0 (0 reads as a dive). */
        *ax = 0.0f;
        *ay = -g_wraw.vec[0];
        *az = -g_wraw.vec[1];
    } else {
        *ax = *ay = 0.0f;
        *az = 0.0f;
    }
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

    glGenBuffers(1, &g_vbo);

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
    glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(g_a_pos);
    glVertexAttribPointer(g_a_pos, 2, GL_FLOAT, 0, 0, NULL);
    glUniformMatrix4fv(g_uproj, 1, 0, g_proj);
    glUniform4f(g_ucol, 1.0f, 0.3137f, 0.3137f, 1.0f);
    glDrawArrays(GL_LINES, 0, 12);

    glDisableVertexAttribArray(g_a_pos);
    glBindBuffer(GL_ARRAY_BUFFER, saved_buf);
    glUseProgram(saved_prog);
    glViewport(saved_vp[0], saved_vp[1], saved_vp[2], saved_vp[3]);
}
