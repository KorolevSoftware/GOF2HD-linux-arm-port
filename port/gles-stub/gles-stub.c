/*
 * libGLESv2.so — software GLES2 implementation for the GOF2HD ARM engine.
 *
 * Runs entirely on the CPU (no GPU), renders into an RGBA framebuffer and
 * writes each completed frame to /tmp/gof2hd_fb (viewed by host viewer.c).
 *
 * The engine only needs a working subset: fixed attrib layout
 * (pos=0, tex=1, col=2), a single MVP matrix, textures, blending.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#define _POSIX_C_SOURCE 200809L
#include <time.h>

typedef unsigned int   GLenum;
typedef unsigned int   GLuint;
typedef unsigned int   GLbitfield;
typedef unsigned char  GLboolean;
typedef int            GLint;
typedef int            GLsizei;
typedef unsigned char  GLubyte;
typedef float          GLfloat;
typedef double         GLdouble;
typedef char           GLchar;
typedef long           GLsizeiptr;
typedef long           GLintptr;

#define GL_FALSE                0
#define GL_TRUE                 1
#define GL_ZERO                 0
#define GL_ONE                  1
#define GL_TRIANGLES            0x0004
#define GL_TRIANGLE_STRIP       0x0005
#define GL_TRIANGLE_FAN         0x0006
#define GL_LINES                0x0001
#define GL_LINE_STRIP           0x0003
#define GL_NEAREST              0x2600
#define GL_LINEAR               0x2601
#define GL_NEAREST_MIPMAP_NEAREST 0x2700
#define GL_LINEAR_MIPMAP_NEAREST 0x2701
#define GL_NEAREST_MIPMAP_LINEAR  0x2702
#define GL_LINEAR_MIPMAP_LINEAR   0x2703
#define GL_TEXTURE_MAG_FILTER   0x2800
#define GL_TEXTURE_MIN_FILTER   0x2801
#define GL_TEXTURE_WRAP_S       0x2802
#define GL_TEXTURE_WRAP_T       0x2803
#define GL_REPEAT               0x2901
#define GL_CLAMP_TO_EDGE        0x812F
#define GL_TEXTURE0             0x84C0
#define GL_TEXTURE_2D           0x0DE1
#define GL_ARRAY_BUFFER         0x8892
#define GL_ELEMENT_ARRAY_BUFFER 0x8893
#define GL_STATIC_DRAW          0x88E4
#define GL_DYNAMIC_DRAW         0x88E8
#define GL_FLOAT                0x1406
#define GL_UNSIGNED_BYTE        0x1401
#define GL_UNSIGNED_SHORT       0x1403
#define GL_RGBA                 0x1908
#define GL_RGB                  0x1907
#define GL_LUMINANCE            0x1909
#define GL_LUMINANCE_ALPHA      0x190A
#define GL_ALPHA                0x1906
#define GL_UNSIGNED_INT         0x1405
#define GL_BYTE                 0x1400
#define GL_SHORT                0x1402
#define GL_VERTEX_SHADER        0x8B31
#define GL_FRAGMENT_SHADER      0x8B30
#define GL_COMPILE_STATUS       0x8B81
#define GL_LINK_STATUS          0x8B82
#define GL_INFO_LOG_LENGTH      0x8B84
#define GL_VENDOR               0x1F00
#define GL_RENDERER             0x1F01
#define GL_VERSION              0x1F02
#define GL_EXTENSIONS           0x1F03
#define GL_BLEND                0x0BE2
#define GL_DEPTH_TEST           0x0B71
#define GL_CULL_FACE            0x0B44
#define GL_SCISSOR_TEST         0x0C11
#define GL_MAX_TEXTURE_SIZE     0x0D33
#define GL_COLOR_BUFFER_BIT     0x4000
#define GL_DEPTH_BUFFER_BIT     0x0100
#define GL_SRC_ALPHA            0x0302
#define GL_ONE_MINUS_SRC_ALPHA  0x0303
#define GL_SRC_COLOR            0x0300
#define GL_ONE_MINUS_SRC_COLOR  0x0301
#define GL_DST_ALPHA            0x0304
#define GL_ONE_MINUS_DST_ALPHA  0x0305
#define GL_DST_COLOR            0x0306
#define GL_ONE_MINUS_DST_COLOR  0x0307
#define GL_BACK                 0x0405
#define GL_FRONT                0x0404
#define GL_CCW                  0x0901
#define GL_CW                   0x0900
#define GL_FRAMEBUFFER          0x8D40
#define GL_COLOR_ATTACHMENT0    0x8CE0
#define GL_RENDERBUFFER         0x8D41
#define GL_DEPTH_COMPONENT16    0x81A5
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#define GL_LINE_LOOP            0x0002
#define GL_FRAMEBUFFER_BINDING  0x8CA6
#define GL_VIEWPORT             0x0BA2
#define GL_CURRENT_PROGRAM      0x8B8D
#define GL_BLEND_SRC_RGB        0x80C9
#define GL_BLEND_DST_RGB        0x80CA
#define GL_BLEND_SRC_ALPHA      0x80CB
#define GL_BLEND_DST_ALPHA      0x80CC
#define GL_SCISSOR_BOX          0x0C10
#define GL_TEXTURE_BINDING_2D   0x8069
#define GL_ARRAY_BUFFER_BINDING 0x8894
#define GL_ELEMENT_ARRAY_BUFFER_BINDING 0x8895
#define GL_ACTIVE_TEXTURE       0x84E0
#define GL_DEPTH_COMPONENT      0x1902
#define GL_NONE                 0
#define GL_BGRA_EXT             0x80E1

#define MAX_TEX       512
#define MAX_BUF       512
#define MAX_SHADER    256
#define MAX_PROG      256
#define MAX_FBO       64
#define MAX_RBO       64
#define MAX_FB_W      2048
#define MAX_FB_H      2048

typedef struct { uint8_t r, g, b, a; } Px;

static Px*      g_fb = NULL;
static float*   g_depth = NULL;
static int      g_fb_w = 640, g_fb_h = 480;
static int      g_vp_x = 0, g_vp_y = 0, g_vp_w = 640, g_vp_h = 480;
static int      g_fb_dirty = 0;
static unsigned g_next_id = 1;

typedef struct {
    uint32_t id, w, h, valid;
    Px*      data;
    int      mag, minf, ws, wt;
} Tex;
static Tex g_tex[MAX_TEX];

typedef struct {
    uint32_t id, size;
    uint8_t* data;
} Buf;
static Buf g_buf[MAX_BUF];

typedef struct {
    uint32_t id, type, compiled;
    char     src[8192];
} Sh;
static Sh g_sh[MAX_SHADER];

typedef struct {
    uint32_t id, linked;
    uint32_t vs, fs;
} Prog;
static Prog g_prog[MAX_PROG];

typedef struct {
    uint32_t id, valid;
    uint32_t tex_id, rbo_id;
} Fbo;
static Fbo g_fbo[MAX_FBO];

typedef struct {
    uint32_t id, valid;
    uint32_t w, h, fmt;
} Rbo;
static Rbo g_rbo[MAX_RBO];

typedef struct {
    int      enabled;
    uint32_t buf_id;
    int      size, type, normalized, stride;
    intptr_t ptr;
} Attrib;
static Attrib g_attrib[16];
static int     g_max_attrib_idx = 0;

static uint32_t g_array_buf = 0, g_elem_buf = 0;
static uint32_t g_active_tex = GL_TEXTURE0;
static uint32_t g_bound_tex = 0;          /* per unit; keep simple: one */
static uint32_t g_bound_tex_per_unit[8] = {0};
static uint32_t g_cur_program = 0;
static uint32_t g_cur_fbo = 0;
static float    g_mvp[16];
static int      g_mvp_valid = 0;

static void mvp_identity(void) {
    float m[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    memcpy(g_mvp, m, 64);
    g_mvp_valid = 1;
}
static float    g_clear_r, g_clear_g, g_clear_b, g_clear_a;
static int      g_blend_en, g_depth_en, g_cull_en, g_scissor_en;
static int      g_blend_src = GL_ONE, g_blend_dst = GL_ZERO;
static Px      g_clear_col;
static int      g_scissor[4];
static float    g_line_width = 1.0f;
static int      g_fb_file_open = 0;

/* ---------------- frame output ---------------- */

static void fb_write_frame(void) {
    if (!g_fb) return;
    static int warned = 0;
    const char* fb_path = getenv("GOF_FB");
    if (fb_path) {
        /* write RGBA pixels directly to a framebuffer device (e.g. /dev/fb0) */
        int fd = open(fb_path, O_WRONLY);
        if (fd < 0) {
            if (!warned) { fprintf(stderr, "[gles] cannot open %s: %s\n", fb_path, strerror(errno)); warned = 1; }
            return;
        }
        /* FB is often BGRX; convert */
        static uint8_t* conv = NULL;
        static int conv_sz = 0;
        int n = g_fb_w * g_fb_h * 4;
        if (conv_sz < n) { free(conv); conv = malloc(n); conv_sz = n; }
        for (int i = 0; i < g_fb_w * g_fb_h; i++) {
            conv[i*4+0] = g_fb[i].b;
            conv[i*4+1] = g_fb[i].g;
            conv[i*4+2] = g_fb[i].r;
            conv[i*4+3] = 0;
        }
        lseek(fd, 0, SEEK_SET);
        write(fd, conv, n);
        close(fd);
        return;
    }
    int fd = open("/tmp/gof2hd_fb", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        if (!warned) { fprintf(stderr, "[gles] cannot write /tmp/gof2hd_fb\n"); warned = 1; }
        return;
    }
    uint8_t hdr[8];
    uint32_t w = (uint32_t)g_fb_w, h = (uint32_t)g_fb_h;
    hdr[0] = w & 255; hdr[1] = (w >> 8) & 255; hdr[2] = (w >> 16) & 255; hdr[3] = (w >> 24) & 255;
    hdr[4] = h & 255; hdr[5] = (h >> 8) & 255; hdr[6] = (h >> 16) & 255; hdr[7] = (h >> 24) & 255;
    write(fd, hdr, 8);
    write(fd, g_fb, g_fb_w * g_fb_h * 4);
    close(fd);
}

static void fb_resize(int w, int h) {
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    if (w > MAX_FB_W) w = MAX_FB_W;
    if (h > MAX_FB_H) h = MAX_FB_H;
    if (w == g_fb_w && h == g_fb_h && g_fb) return;
    free(g_fb); free(g_depth);
    g_fb = calloc(w * h, 4);
    g_depth = malloc(w * h * sizeof(float));
    g_fb_w = w; g_fb_h = h;
    for (int i = 0; i < w * h; i++) g_depth[i] = 1e30f;
}

/* ---------------- lookup helpers ---------------- */

static Tex* find_tex(uint32_t id) {
    for (int i = 0; i < MAX_TEX; i++) if (g_tex[i].id == id && id) return &g_tex[i];
    return NULL;
}
static Buf* find_buf(uint32_t id) {
    for (int i = 0; i < MAX_BUF; i++) if (g_buf[i].id == id && id) return &g_buf[i];
    return NULL;
}
static Sh* find_sh(uint32_t id) {
    for (int i = 0; i < MAX_SHADER; i++) if (g_sh[i].id == id && id) return &g_sh[i];
    return NULL;
}
static Prog* find_prog(uint32_t id) {
    for (int i = 0; i < MAX_PROG; i++) if (g_prog[i].id == id && id) return &g_prog[i];
    return NULL;
}
static Fbo* find_fbo(uint32_t id) {
    for (int i = 0; i < MAX_FBO; i++) if (g_fbo[i].id == id && id) return &g_fbo[i];
    return NULL;
}

/* ---------------- public API ---------------- */

GLenum glGetError(void) { return 0; }

const GLubyte* glGetString(GLenum name) {
    switch (name) {
        case GL_VENDOR:   return (const GLubyte*)"fishlabs-port";
        case GL_RENDERER: return (const GLubyte*)"gles-soft";
        case GL_VERSION:  return (const GLubyte*)"OpenGL ES 2.0 software";
        case GL_EXTENSIONS: return (const GLubyte*)"";
        default: return (const GLubyte*)"";
    }
}

void glGetIntegerv(GLenum pname, GLint* v) {
    switch (pname) {
        case GL_MAX_TEXTURE_SIZE: *v = 2048; break;
        case GL_FRAMEBUFFER_BINDING: *v = (GLint)g_cur_fbo; break;
        case GL_VIEWPORT: v[0]=g_vp_x; v[1]=g_vp_y; v[2]=g_vp_w; v[3]=g_vp_h; break;
        case GL_BLEND_SRC_RGB: v[0]=g_blend_src; break;
        case GL_BLEND_DST_RGB: v[0]=g_blend_dst; break;
        case GL_ACTIVE_TEXTURE: v[0]=(GLint)g_active_tex; break;
        case GL_ARRAY_BUFFER_BINDING: v[0]=(GLint)g_array_buf; break;
        case GL_ELEMENT_ARRAY_BUFFER_BINDING: v[0]=(GLint)g_elem_buf; break;
        case GL_TEXTURE_BINDING_2D: v[0]=(GLint)g_bound_tex; break;
        default: *v = 0;
    }
}

GLuint glCreateShader(GLenum type) {
    Sh* s = NULL;
    for (int i = 0; i < MAX_SHADER; i++) if (!g_sh[i].id) { s = &g_sh[i]; break; }
    if (!s) return 0;
    s->id = g_next_id++;
    s->type = type;
    s->compiled = 0;
    s->src[0] = 0;
    return s->id;
}

void glShaderSource(GLuint sh, GLsizei count, const GLchar* const* src, const GLint* len) {
    Sh* s = find_sh(sh);
    if (!s) return;
    s->src[0] = 0;
    for (int i = 0; i < count; i++) {
        if (len && len[i] >= 0) { strncat(s->src, src[i], len[i]); }
        else strncat(s->src, src[i], sizeof(s->src) - strlen(s->src) - 1);
    }
}

void glCompileShader(GLuint sh) {
    Sh* s = find_sh(sh);
    if (s) s->compiled = 1;
}

void glGetShaderiv(GLuint sh, GLenum pname, GLint* v) {
    Sh* s = find_sh(sh);
    if (pname == GL_COMPILE_STATUS) *v = s ? s->compiled : 0;
    else if (pname == GL_INFO_LOG_LENGTH) *v = 1;
    else *v = 0;
}

void glGetShaderInfoLog(GLuint sh, GLsizei n, GLsizei* len, GLchar* log) {
    (void)sh; if (len) *len = 0; if (n > 0 && log) log[0] = 0;
}

void glDeleteShader(GLuint sh) {
    Sh* s = find_sh(sh);
    if (s) memset(s, 0, sizeof(*s));
}

GLuint glCreateProgram(void) {
    Prog* p = NULL;
    for (int i = 0; i < MAX_PROG; i++) if (!g_prog[i].id) { p = &g_prog[i]; break; }
    if (!p) return 0;
    p->id = g_next_id++;
    p->linked = 0;
    return p->id;
}

void glAttachShader(GLuint prog, GLuint sh) {
    Prog* p = find_prog(prog);
    Sh* s = find_sh(sh);
    if (!p || !s) return;
    if (s->type == GL_VERTEX_SHADER) p->vs = sh;
    else p->fs = sh;
}

void glLinkProgram(GLuint prog) {
    Prog* p = find_prog(prog);
    if (p) p->linked = 1;
}

void glGetProgramiv(GLuint prog, GLenum pname, GLint* v) {
    Prog* p = find_prog(prog);
    if (pname == GL_LINK_STATUS) *v = p ? p->linked : 0;
    else if (pname == GL_INFO_LOG_LENGTH) *v = 1;
    else *v = 0;
}

void glGetProgramInfoLog(GLuint prog, GLsizei n, GLsizei* len, GLchar* log) {
    (void)prog; if (len) *len = 0; if (n > 0 && log) log[0] = 0;
}

void glDeleteProgram(GLuint prog) {
    Prog* p = find_prog(prog);
    if (p) memset(p, 0, sizeof(*p));
    if (g_cur_program == prog) g_cur_program = 0;
}

GLint glGetAttribLocation(GLuint prog, const GLchar* name) {
    if (getenv("GOF_GL_TRACE")) {
        static int n = 0;
        if (n++ < 20)
            fprintf(stderr, "[gles] glGetAttribLocation(%u, %s)\n", prog, name ? name : "(null)");
    }
    (void)prog;
    const char* n = name ? name : "";
    if (strstr(n, "position") || strstr(n, "Position") || strstr(n, "pos") ||
        strstr(n, "POS") || strstr(n, "vertex") || strstr(n, "Vertex"))
        return 0;
    if (strstr(n, "tex") || strstr(n, "Tex") || strstr(n, "uv") || strstr(n, "UV") ||
        strstr(n, "coord") || strstr(n, "Coord"))
        return 1;
    if (strstr(n, "color") || strstr(n, "Color") || strstr(n, "col") || strstr(n, "Col"))
        return 2;
    return 3;
}

GLint glGetUniformLocation(GLuint prog, const GLchar* name) {
    if (getenv("GOF_GL_TRACE")) {
        static int n = 0;
        if (n++ < 20)
            fprintf(stderr, "[gles] glGetUniformLocation(%u, %s)\n", prog, name ? name : "(null)");
    }
    (void)prog;
    const char* n = name ? name : "";
    if (strstr(n, "mvp") || strstr(n, "MVP") || strstr(n, "model") || strstr(n, "Model") ||
        strstr(n, "matrix") || strstr(n, "Matrix") || strstr(n, "proj") || strstr(n, "Proj") ||
        strstr(n, "view") || strstr(n, "View") || strstr(n, "ModelView") || strstr(n, "MV"))
        return 0;
    if (strstr(n, "color") || strstr(n, "Color") || strstr(n, "col"))
        return 1;
    if (strstr(n, "tex") || strstr(n, "Tex") || strstr(n, "sampler"))
        return 2;
    return 3;
}

void glUseProgram(GLuint prog) { g_cur_program = prog; }

void glGenTextures(GLsizei n, GLuint* ids) {
    for (int i = 0; i < n; i++) {
        Tex* t = NULL;
        for (int j = 0; j < MAX_TEX; j++) if (!g_tex[j].id) { t = &g_tex[j]; break; }
        if (!t) { ids[i] = 0; continue; }
        t->id = g_next_id++;
        t->valid = 0;
        t->mag = GL_LINEAR; t->minf = GL_LINEAR;
        t->ws = GL_REPEAT; t->wt = GL_REPEAT;
        ids[i] = t->id;
    }
}

void glDeleteTextures(GLsizei n, const GLuint* ids) {
    for (int i = 0; i < n; i++) {
        Tex* t = find_tex(ids[i]);
        if (t) { free(t->data); memset(t, 0, sizeof(*t)); }
    }
}

void glBindTexture(GLenum target, GLuint id) {
    if (target != GL_TEXTURE_2D) return;
    int unit = (g_active_tex - GL_TEXTURE0) & 7;
    g_bound_tex_per_unit[unit] = id;
    if (unit == 0) g_bound_tex = id;
}

void glActiveTexture(GLenum tex) { g_active_tex = tex; }

void glTexParameteri(GLenum target, GLenum pname, GLint v) {
    (void)target;
    Tex* t = find_tex(g_bound_tex);
    if (!t) return;
    if (pname == GL_TEXTURE_MAG_FILTER) t->mag = v;
    else if (pname == GL_TEXTURE_MIN_FILTER) t->minf = v;
    else if (pname == GL_TEXTURE_WRAP_S) t->ws = v;
    else if (pname == GL_TEXTURE_WRAP_T) t->wt = v;
}
void glTexParameterf(GLenum target, GLenum pname, GLfloat v) {
    glTexParameteri(target, pname, (GLint)v);
}

static Px tex_sample(Tex* t, float u, float v) {
    Px black = {0,0,0,255};
    if (!t || !t->valid || !t->data) return black;
    if (t->ws != GL_REPEAT) { if (u < 0) u = 0; if (u > 1) u = 1; }
    if (t->wt != GL_REPEAT) { if (v < 0) v = 0; if (v > 1) v = 1; }
    if (t->ws == GL_REPEAT) u -= floorf(u);
    if (t->wt == GL_REPEAT) v -= floorf(v);
    if (t->mag == GL_NEAREST) {
        int x = (int)(u * t->w); if (x >= (int)t->w) x = t->w - 1;
        int y = (int)(v * t->h); if (y >= (int)t->h) y = t->h - 1;
        if (x < 0) x = 0; if (y < 0) y = 0;
        return t->data[y * t->w + x];
    }
    float fx = u * t->w - 0.5f, fy = v * t->h - 0.5f;
    int x0 = (int)floorf(fx), y0 = (int)floorf(fy);
    float tx = fx - x0, ty = fy - y0;
    int x1 = x0 + 1, y1 = y0 + 1;
    if (x0 < 0) x0 = 0; if (x1 >= (int)t->w) x1 = t->w - 1;
    if (y0 < 0) y0 = 0; if (y1 >= (int)t->h) y1 = t->h - 1;
    Px p00 = t->data[y0*t->w+x0], p10 = t->data[y0*t->w+x1];
    Px p01 = t->data[y1*t->w+x0], p11 = t->data[y1*t->w+x1];
    Px out;
    for (int c = 0; c < 4; c++) {
        float* a = c==0?&out.r:c==1?&out.g:c==2?&out.b:&out.a;
        const uint8_t* v00 = c==0?&p00.r:c==1?&p00.g:c==2?&p00.b:&p00.a;
        const uint8_t* v10 = c==0?&p10.r:c==1?&p10.g:c==2?&p10.b:&p10.a;
        const uint8_t* v01 = c==0?&p01.r:c==1?&p01.g:c==2?&p01.b:&p01.a;
        const uint8_t* v11 = c==0?&p11.r:c==1?&p11.g:c==2?&p11.b:&p11.a;
        *a = (uint8_t)((v00[0]*(1-tx)+v10[0]*tx)*(1-ty) + (v01[0]*(1-tx)+v11[0]*tx)*ty);
    }
    return out;
}

void glTexImage2D(GLenum target, GLint level, GLint ifmt, GLsizei w, GLsizei h,
                  GLint border, GLenum format, GLenum type, const void* data) {
    (void)level; (void)ifmt; (void)border; (void)type;
    if (target != GL_TEXTURE_2D || w <= 0 || h <= 0) return;
    Tex* t = find_tex(g_bound_tex);
    if (!t) return;
    if (!data) { t->valid = 0; return; }
    Px* px = malloc((size_t)w * h * 4);
    const uint8_t* src = data;
    if (format == GL_BGRA_EXT) {
        for (int i = 0; i < w * h; i++) {
            px[i].b = src[4*i+0]; px[i].g = src[4*i+1];
            px[i].r = src[4*i+2]; px[i].a = src[4*i+3];
        }
    } else if (format == GL_RGBA) {
        for (int i = 0; i < w * h; i++) {
            px[i].r = src[4*i+0]; px[i].g = src[4*i+1];
            px[i].b = src[4*i+2]; px[i].a = src[4*i+3];
        }
    } else if (format == GL_RGB) {
        for (int i = 0; i < w * h; i++) {
            px[i].r = src[3*i+0]; px[i].g = src[3*i+1];
            px[i].b = src[3*i+2]; px[i].a = 255;
        }
    } else if (format == GL_LUMINANCE) {
        for (int i = 0; i < w * h; i++) {
            px[i].r = px[i].g = px[i].b = src[i]; px[i].a = 255;
        }
    } else if (format == GL_LUMINANCE_ALPHA) {
        for (int i = 0; i < w * h; i++) {
            px[i].r = px[i].g = px[i].b = src[2*i+0]; px[i].a = src[2*i+1];
        }
    } else if (format == GL_ALPHA) {
        for (int i = 0; i < w * h; i++) {
            px[i].r = px[i].g = px[i].b = 255; px[i].a = src[i];
        }
    } else {
        free(px);
        t->valid = 0;
        return;
    }
    free(t->data);
    t->data = px;
    t->w = w; t->h = h;
    t->valid = 1;
}

void glCompressedTexImage2D(GLenum target, GLint level, GLenum ifmt, GLsizei w,
                            GLsizei h, GLint border, GLsizei size, const void* data) {
    /* ETC1/ATC ignored: mark invalid so we fall back to plain color. */
    (void)level; (void)ifmt; (void)border; (void)size; (void)data;
    if (target != GL_TEXTURE_2D) return;
    Tex* t = find_tex(g_bound_tex);
    if (!t) return;
    free(t->data);
    t->data = NULL;
    t->w = w; t->h = h;
    t->valid = 0;
}

void glGenerateMipmap(GLenum target) { (void)target; }

void glGenBuffers(GLsizei n, GLuint* ids) {
    for (int i = 0; i < n; i++) {
        Buf* b = NULL;
        for (int j = 0; j < MAX_BUF; j++) if (!g_buf[j].id) { b = &g_buf[j]; break; }
        if (!b) { ids[i] = 0; continue; }
        b->id = g_next_id++;
        b->data = NULL; b->size = 0;
        ids[i] = b->id;
    }
}

void glDeleteBuffers(GLsizei n, const GLuint* ids) {
    for (int i = 0; i < n; i++) {
        Buf* b = find_buf(ids[i]);
        if (b) { free(b->data); memset(b, 0, sizeof(*b)); }
    }
}

void glBindBuffer(GLenum target, GLuint id) {
    if (target == GL_ARRAY_BUFFER) g_array_buf = id;
    else if (target == GL_ELEMENT_ARRAY_BUFFER) g_elem_buf = id;
}

void glBufferData(GLenum target, GLsizeiptr size, const void* data, GLenum usage) {
    (void)usage;
    uint32_t* bid = target == GL_ARRAY_BUFFER ? &g_array_buf : &g_elem_buf;
    Buf* b = find_buf(*bid);
    if (!b) return;
    free(b->data);
    b->size = (uint32_t)size;
    b->data = malloc(size ? size : 1);
    if (data && size) memcpy(b->data, data, size);
}

void glBufferSubData(GLenum target, GLintptr off, GLsizeiptr size, const void* data) {
    uint32_t* bid = target == GL_ARRAY_BUFFER ? &g_array_buf : &g_elem_buf;
    Buf* b = find_buf(*bid);
    if (!b || !b->data || off + size > (GLsizeiptr)b->size) return;
    memcpy(b->data + off, data, size);
}

void glGenFramebuffers(GLsizei n, GLuint* ids) {
    for (int i = 0; i < n; i++) {
        Fbo* f = NULL;
        for (int j = 0; j < MAX_FBO; j++) if (!g_fbo[j].id) { f = &g_fbo[j]; break; }
        if (!f) { ids[i] = 0; continue; }
        f->id = g_next_id++;
        f->valid = 1; f->tex_id = 0; f->rbo_id = 0;
        ids[i] = f->id;
    }
}
void glDeleteFramebuffers(GLsizei n, const GLuint* ids) {
    for (int i = 0; i < n; i++) {
        Fbo* f = find_fbo(ids[i]);
        if (f) memset(f, 0, sizeof(*f));
    }
}
void glGenRenderbuffers(GLsizei n, GLuint* ids) {
    for (int i = 0; i < n; i++) {
        Rbo* r = NULL;
        for (int j = 0; j < MAX_RBO; j++) if (!g_rbo[j].id) { r = &g_rbo[j]; break; }
        if (!r) { ids[i] = 0; continue; }
        r->id = g_next_id++;
        r->valid = 1;
        ids[i] = r->id;
    }
}
void glDeleteRenderbuffers(GLsizei n, const GLuint* ids) {
    for (int i = 0; i < n; i++)
        for (int j = 0; j < MAX_RBO; j++)
            if (g_rbo[j].id == ids[i]) memset(&g_rbo[j], 0, sizeof(g_rbo[j]));
}
void glBindFramebuffer(GLenum target, GLuint id) {
    (void)target;
    g_cur_fbo = id;
}
void glBindRenderbuffer(GLenum target, GLuint id) { (void)target; }
void glFramebufferTexture2D(GLenum target, GLenum attachment, GLenum textarget,
                            GLuint tex, GLint level) {
    (void)target; (void)attachment; (void)textarget; (void)level;
    Fbo* f = find_fbo(g_cur_fbo);
    if (f) f->tex_id = tex;
}
void glFramebufferRenderbuffer(GLenum target, GLenum attachment, GLenum rb, GLuint id) {
    (void)target; (void)attachment; (void)rb;
    Fbo* f = find_fbo(g_cur_fbo);
    if (f) f->rbo_id = id;
}
void glRenderbufferStorage(GLenum target, GLenum ifmt, GLsizei w, GLsizei h) {
    (void)target; (void)ifmt; (void)w; (void)h;
}
GLenum glCheckFramebufferStatus(GLenum target) {
    (void)target; return GL_FRAMEBUFFER_COMPLETE;
}

void glViewport(GLint x, GLint y, GLsizei w, GLsizei h) {
    if (getenv("GOF_GL_TRACE")) {
        static int n = 0;
        if (n++ < 5) fprintf(stderr, "[gles] glViewport %d %d %d %d\n", x, y, w, h);
    }
    g_vp_x = x; g_vp_y = y; g_vp_w = w; g_vp_h = h;
    if (!g_cur_fbo) fb_resize(w, h);
}

void glScissor(GLint x, GLint y, GLsizei w, GLsizei h) {
    g_scissor[0]=x; g_scissor[1]=y; g_scissor[2]=w; g_scissor[3]=h;
}

void glClearColor(GLfloat r, GLfloat g, GLfloat b, GLfloat a) {
    g_clear_r = r; g_clear_g = g; g_clear_b = b; g_clear_a = a;
}

void glClear(GLbitfield mask) {
    if (getenv("GOF_GL_TRACE")) {
        static int n = 0;
        if (n++ < 20)
            fprintf(stderr, "[gles] glClear mask=0x%x fb_dirty=%d fbo=%u\n", mask, g_fb_dirty, g_cur_fbo);
    }
    if (mask & GL_COLOR_BUFFER_BIT) {
        if (g_cur_fbo) {
            /* offscreen: nothing to display, ignore */
        }
        g_fb_dirty = 0;
        if (g_fb) {
            Px c;
            c.r = (uint8_t)(g_clear_r * 255);
            c.g = (uint8_t)(g_clear_g * 255);
            c.b = (uint8_t)(g_clear_b * 255);
            c.a = (uint8_t)(g_clear_a * 255);
            for (int i = 0; i < g_fb_w * g_fb_h; i++) g_fb[i] = c;
        }
    }
    if ((mask & GL_DEPTH_BUFFER_BIT) && g_depth)
        for (int i = 0; i < g_fb_w * g_fb_h; i++) g_depth[i] = 1e30f;
}

void glColorMask(GLboolean r, GLboolean g, GLboolean b, GLboolean a) {
    (void)r; (void)g; (void)b; (void)a;
}

void glEnable(GLenum cap) {
    if (cap == GL_BLEND) g_blend_en = 1;
    else if (cap == GL_DEPTH_TEST) g_depth_en = 1;
    else if (cap == GL_CULL_FACE) g_cull_en = 1;
    else if (cap == GL_SCISSOR_TEST) g_scissor_en = 1;
}
void glDisable(GLenum cap) {
    if (cap == GL_BLEND) g_blend_en = 0;
    else if (cap == GL_DEPTH_TEST) g_depth_en = 0;
    else if (cap == GL_CULL_FACE) g_cull_en = 0;
    else if (cap == GL_SCISSOR_TEST) g_scissor_en = 0;
}
void glBlendFunc(GLenum src, GLenum dst) { g_blend_src = src; g_blend_dst = dst; }
void glDepthFunc(GLenum f) { (void)f; }
void glDepthMask(GLboolean m) { (void)m; }
void glCullFace(GLenum m) { (void)m; }
void glLineWidth(GLfloat w) { g_line_width = w; }
void glPixelStorei(GLenum pname, GLint v) { (void)pname; (void)v; }

void glEnableVertexAttribArray(GLuint i) {
    if (i < 16) { g_attrib[i].enabled = 1; if ((int)i > g_max_attrib_idx) g_max_attrib_idx = i; }
}
void glDisableVertexAttribArray(GLuint i) {
    if (i < 16) g_attrib[i].enabled = 0;
}
void glVertexAttribPointer(GLuint i, GLint size, GLenum type, GLboolean norm,
                           GLsizei stride, const void* ptr) {
    if (getenv("GOF_GL_TRACE")) {
        static int n = 0;
        if (n++ < 20)
            fprintf(stderr, "[gles] glVertexAttribPointer i=%u size=%d type=%u stride=%d buf=%u\n",
                    i, size, type, stride, g_array_buf);
    }
    if (i >= 16) return;
    g_attrib[i].size = size;
    g_attrib[i].type = type;
    g_attrib[i].normalized = norm;
    g_attrib[i].stride = stride;
    g_attrib[i].ptr = (intptr_t)ptr;
    g_attrib[i].buf_id = g_array_buf;
}

void glUniform1f(GLint loc, GLfloat v) { (void)loc; (void)v; }
void glUniform1i(GLint loc, GLint v) { (void)loc; (void)v; }
void glUniform2f(GLint loc, GLfloat a, GLfloat b) { (void)loc; (void)a; (void)b; }
void glUniform3f(GLint loc, GLfloat a, GLfloat b, GLfloat c) { (void)loc; (void)a; (void)b; (void)c; }
void glUniform3fv(GLint loc, GLsizei n, const GLfloat* v) { (void)loc; (void)n; (void)v; }
void glUniform4f(GLint loc, GLfloat a, GLfloat b, GLfloat c, GLfloat d) { (void)loc; (void)a; (void)b; (void)c; (void)d; }
void glUniform4fv(GLint loc, GLsizei n, const GLfloat* v) { (void)loc; (void)n; (void)v; }
void glUniformMatrix3fv(GLint loc, GLsizei n, GLboolean t, const GLfloat* v) { (void)loc; (void)n; (void)t; (void)v; }
void glUniformMatrix4fv(GLint loc, GLsizei n, GLboolean t, const GLfloat* v) {
    if (loc == 0 && n > 0 && v) {
        memcpy(g_mvp, v, 64);
        g_mvp_valid = 1;
    }
}

/* ---------------- software rasterizer ---------------- */

static void mvp_apply(const float* m, const float* in, float* out) {
    out[0] = m[0]*in[0] + m[4]*in[1] + m[8]*in[2]  + m[12]*in[3];
    out[1] = m[1]*in[0] + m[5]*in[1] + m[9]*in[2]  + m[13]*in[3];
    out[2] = m[2]*in[0] + m[6]*in[1] + m[10]*in[2] + m[14]*in[3];
    out[3] = m[3]*in[0] + m[7]*in[1] + m[11]*in[2] + m[15]*in[3];
}

static int read_attrib(const Attrib* a, int idx, float* out) {
    if (!a->enabled) return -1;
    const uint8_t* base;
    Buf* b = find_buf(a->buf_id);
    if (b && b->data) base = b->data;
    else if (a->buf_id) return -1; /* buffer bound but no data yet */
    else base = (const uint8_t*)0; /* client memory */
    if (!base && !a->ptr) return -1;
    const uint8_t* p = base + (intptr_t)a->ptr + (size_t)idx * (a->stride ? a->stride : (a->size * 4));
    for (int c = 0; c < a->size && c < 4; c++) {
        if (a->type == GL_FLOAT) out[c] = ((const float*)p)[c];
        else if (a->type == GL_BYTE) out[c] = (float)((const int8_t*)p)[c];
        else if (a->type == GL_UNSIGNED_BYTE)
            out[c] = a->normalized ? ((const uint8_t*)p)[c] / 255.0f : (float)((const uint8_t*)p)[c];
        else if (a->type == GL_SHORT) out[c] = (float)((const short*)p)[c];
        else if (a->type == GL_UNSIGNED_SHORT)
            out[c] = a->normalized ? ((const uint16_t*)p)[c] / 65535.0f : (float)((const uint16_t*)p)[c];
        else out[c] = 0;
    }
    return a->size;
}

static void blend_pixel(Px* dst, Px src, float sa) {
    int sr = src.r, sg = src.g, sb = src.b, sa8 = src.a;
    int dr = dst->r, dg = dst->g, db = dst->b, da = dst->a;
    int outr, outg, outb, outa;
    switch (g_blend_src) {
        case GL_SRC_ALPHA:
            outr = (sr * sa8) >> 8; outg = (sg * sa8) >> 8;
            outb = (sb * sa8) >> 8; outa = (sa8 * sa8) >> 8;
            break;
        case GL_ONE:
            outr = sr; outg = sg; outb = sb; outa = sa8;
            break;
        default:
            outr = sr; outg = sg; outb = sb; outa = sa8;
    }
    switch (g_blend_dst) {
        case GL_ONE_MINUS_SRC_ALPHA: {
            int ia = 255 - sa8;
            dst->r = (uint8_t)(((outr * ia) >> 8) + ((dr * (255 - (255 - ia))) >> 8));
            dst->g = (uint8_t)(((outg * ia) >> 8) + ((dg * (255 - (255 - ia))) >> 8));
            dst->b = (uint8_t)(((outb * ia) >> 8) + ((db * (255 - (255 - ia))) >> 8));
            dst->a = (uint8_t)(((outa * ia) >> 8) + ((da * (255 - (255 - ia))) >> 8));
            break;
        }
        case GL_ONE:
            dst->r = (uint8_t)(outr + dr > 255 ? 255 : outr + dr);
            dst->g = (uint8_t)(outg + dg > 255 ? 255 : outg + dg);
            dst->b = (uint8_t)(outb + db > 255 ? 255 : outb + db);
            dst->a = (uint8_t)(outa + da > 255 ? 255 : outa + da);
            break;
        case GL_ZERO:
            dst->r = (uint8_t)outr; dst->g = (uint8_t)outg;
            dst->b = (uint8_t)outb; dst->a = (uint8_t)outa;
            break;
        default:
            dst->r = (uint8_t)outr; dst->g = (uint8_t)outg;
            dst->b = (uint8_t)outb; dst->a = (uint8_t)outa;
    }
    (void)sa;
}

static void draw_tri(float ax, float ay, float az, float au, float av,
                     float bx, float by, float bz, float bu, float bv,
                     float cx, float cy, float cz, float cu, float cv,
                     Px col, int has_tex, Tex* tex) {
    if (!g_fb) return;
    float minx = fminf(fminf(ax, bx), cx), maxx = fmaxf(fmaxf(ax, bx), cx);
    float miny = fminf(fminf(ay, by), cy), maxy = fmaxf(fmaxf(ay, by), cy);
    /* guard against NaN/Inf coords: they'd make the pixel loop run forever */
    if (!(minx == minx) || !(miny == miny) || !(maxx == maxx) || !(maxy == maxy)) return;
    if (minx < -1e7f || maxx > 1e7f || miny < -1e7f || maxy > 1e7f) return;
    int vx = g_vp_x, vy = g_vp_y, vw = g_vp_w, vh = g_vp_h;
    if (g_scissor_en) { vx = g_scissor[0]; vy = g_scissor[1]; vw = g_scissor[2]; vh = g_scissor[3]; }
    if (minx >= vx + vw || maxx < vx || miny >= vy + vh || maxy < vy) return; /* offscreen */
    int sx = (int)floorf(minx), ex = (int)ceilf(maxx);
    int sy = (int)floorf(miny), ey = (int)ceilf(maxy);
    if (sx < vx) sx = vx; if (ex > vx + vw) ex = vx + vw;
    if (sy < vy) sy = vy; if (ey > vy + vh) ey = vy + vh;
    float det = (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
    if (det > -1e-9f && det < 1e-9f) return;
    for (int y = sy; y <= ey; y++) {
        for (int x = sx; x <= ex; x++) {
            if (x < 0 || y < 0 || x >= g_fb_w || y >= g_fb_h) continue;
            float px = x + 0.5f, py = y + 0.5f;
            float w0 = ((bx - ax) * (py - ay) - (by - ay) * (px - ax)) / det;
            float w1 = ((cx - bx) * (py - by) - (cy - by) * (px - bx)) / det;
            float w2 = 1.0f - w0 - w1;
            if (w0 < -0.001f || w1 < -0.001f || w2 < -0.001f) continue;
            Px src = col;
            if (has_tex && tex && tex->valid && tex->data) {
                float u = w0 * au + w1 * bu + w2 * cu;
                float v = w0 * av + w1 * bv + w2 * cv;
                src = tex_sample(tex, u, v);
            }
            Px* d = &g_fb[y * g_fb_w + x];
            if (g_blend_en) blend_pixel(d, src, 0);
            else *d = src;
        }
    }
}

static void emit_tri(int i0, int i1, int i2) {
    float pa[4], pb[4], pc[4], ua[2], ub[2], uc[2], ca[4], cb[4], cc[4];
    float wa[4], wb[4], wc[4];
    const Attrib* A = &g_attrib[0];
    const Attrib* T = &g_attrib[1];
    const Attrib* C = &g_attrib[2];
    int na = read_attrib(A, i0, pa), nb = read_attrib(A, i1, pb), nc = read_attrib(A, i2, pc);
    if (na < 2) return;
    int has_tex = 0;
    if (T && T->enabled) {
        float tu[2], tv[2], tw[2];
        if (read_attrib(T, i0, tu) >= 2 && read_attrib(T, i1, tv) >= 2 && read_attrib(T, i2, tw) >= 2) {
            ua[0]=tu[0]; ua[1]=tu[1]; ub[0]=tv[0]; ub[1]=tv[1]; uc[0]=tw[0]; uc[1]=tw[1];
            has_tex = 1;
        }
    }
    Px col = {255,255,255,255};
    if (C && C->enabled) {
        float tc0[4], tc1[4], tc2[4];
        if (read_attrib(C, i0, tc0) >= 3 && read_attrib(C, i1, tc1) >= 3 && read_attrib(C, i2, tc2) >= 3) {
            col.r=(uint8_t)(tc0[0]*255); col.g=(uint8_t)(tc0[1]*255);
            col.b=(uint8_t)(tc0[2]*255); col.a=(uint8_t)((tc0[3]>1?1:tc0[3])*255);
        }
    }
    float ia[4] = {0,0,0,1};
    mvp_apply(g_mvp, pa, wa); mvp_apply(g_mvp, pb, wb); mvp_apply(g_mvp, pc, wc);
    for (int k = 0; k < 3; k++) {
        if (wa[3] != 0) wa[k] /= wa[3];
        if (wb[3] != 0) wb[k] /= wb[3];
        if (wc[3] != 0) wc[k] /= wc[3];
    }
    /* NDC -> pixels */
    float ax2 = (wa[0] + 1) * 0.5f * g_fb_w;
    float ay2 = (1 - wa[1]) * 0.5f * g_fb_h;
    float bx2 = (wb[0] + 1) * 0.5f * g_fb_w;
    float by2 = (1 - wb[1]) * 0.5f * g_fb_h;
    float cx2 = (wc[0] + 1) * 0.5f * g_fb_w;
    float cy2 = (1 - wc[1]) * 0.5f * g_fb_h;
    Tex* tex = has_tex ? find_tex(g_bound_tex) : NULL;
    draw_tri(ax2, ay2, 0, ua[0], ua[1], bx2, by2, 0, ub[0], ub[1], cx2, cy2, 0, uc[0], uc[1],
             col, has_tex, tex);
}

static void maybe_write_frame(void) {
    static long last_write_ms = 0;
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    long now = ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
    if (now - last_write_ms > 200) {
        if (g_fb) fb_write_frame();
        last_write_ms = now;
    }
}

void glDrawArrays(GLenum mode, GLint first, GLsizei count) {
    if (getenv("GOF_GL_TRACE")) {
        static int n = 0;
        if (n++ < 10) fprintf(stderr, "[gles] glDrawArrays mode=%u first=%d count=%d\n", mode, first, count);
    }
    if (!g_fb) return;
    if (!g_mvp_valid) mvp_identity();
    if (count < 0 || count > 1000000) return;
    if (mode == GL_TRIANGLES) {
        for (GLsizei i = 0; i + 2 < count; i += 3)
            emit_tri(first + i, first + i + 1, first + i + 2);
    } else if (mode == GL_TRIANGLE_STRIP) {
        for (GLsizei i = 0; i + 2 < count; i++)
            emit_tri(first + i, first + i + 1, first + i + 2);
    } else if (mode == GL_TRIANGLE_FAN) {
        for (GLsizei i = 1; i + 1 < count; i++)
            emit_tri(first, first + i, first + i + 1);
    }
    g_fb_dirty = 1;
    maybe_write_frame();
}

void glDrawElements(GLenum mode, GLsizei count, GLenum type, const void* indices) {
    if (getenv("GOF_GL_TRACE")) {
        static int n = 0;
        if (n++ < 10) fprintf(stderr, "[gles] glDrawElements mode=%u count=%d type=%u fbo=%u\n", mode, count, type, g_cur_fbo);
    }
    if (!g_fb) return;
    if (!count || count < 0 || count > 1000000) return;
    Buf* eb = find_buf(g_elem_buf);
    const uint8_t* base = NULL;
    if (eb && eb->data) base = eb->data;
    else base = (const uint8_t*)indices;
    if (!base) return;
    if (type != GL_UNSIGNED_SHORT && type != GL_UNSIGNED_BYTE && type != GL_UNSIGNED_INT) return;
    int* idx = malloc((count + 1) * sizeof(int));
    if (!idx) return;
    if (type == GL_UNSIGNED_SHORT) {
        const uint16_t* p = (const uint16_t*)base;
        for (int i = 0; i < count; i++) idx[i] = p[i];
    } else if (type == GL_UNSIGNED_BYTE) {
        for (int i = 0; i < count; i++) idx[i] = ((const uint8_t*)base)[i];
    } else if (type == GL_UNSIGNED_INT) {
        const uint32_t* p = (const uint32_t*)base;
        for (int i = 0; i < count; i++) idx[i] = p[i];
    }
    if (mode == GL_TRIANGLES) {
        for (int i = 0; i + 2 < count; i += 3)
            emit_tri(idx[i], idx[i+1], idx[i+2]);
    } else if (mode == GL_TRIANGLE_STRIP) {
        for (int i = 0; i + 2 < count; i++)
            emit_tri(idx[i], idx[i+1], idx[i+2]);
    } else if (mode == GL_TRIANGLE_FAN) {
        for (int i = 1; i + 1 < count; i++)
            emit_tri(idx[0], idx[i], idx[i+1]);
    }
    free(idx);
    g_fb_dirty = 1;
    maybe_write_frame();
}
