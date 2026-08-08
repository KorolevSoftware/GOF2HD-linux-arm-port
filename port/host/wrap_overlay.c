/*
 * wrap_overlay.c — 2D GL overlay for the GOF2HD host (cursor reticle).
 *
 * The old approach drew into raw fb0 memory after SwapWindow, so the
 * fbdev driver's frame presentation wiped it asynchronously and the
 * cursor flickered.  Instead we render the reticle through the same
 * GLES2 context, after the engine's renderstep and before SwapWindow,
 * so it is part of the presented frame.
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

static int      g_w, g_h;
static GLuint   g_prog, g_vbo;
static GLint    g_uproj, g_ucol, g_a_pos;
static GLfloat  g_proj[16];

int overlay_enabled(void) {
    return getenv("GOF_SHOW_CURSOR") != NULL;
}

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
    g_w = width;
    g_h = height;

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

void overlay_draw(int x, int y) {
    if (!g_prog || !g_w || !g_h) return;

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
    glViewport(0, 0, g_w, g_h);

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