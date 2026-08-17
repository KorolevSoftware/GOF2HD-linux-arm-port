/*
 * gles-bridge.c — softfp -> hardfp GLES2 bridge to libmali.
 *
 * The GOF2HD ARM engine is softfp (bionic armeabi-v7a): it passes float
 * arguments in r0-r3 / on the stack.  The device's libmali.so is hardfp
 * (aapcs-vfp): it expects floats in s0-s15.
 *
 * This .so is built hardfp and exports every GLES2 function with
 * __attribute__((pcs("aapcs"))) so the softfp engine can call in, then
 * forwards to the hardfp libmali symbol through a pcs("aapcs-vfp")
 * function pointer.
 *
 * Only GLES2 entry points are exported: that is exactly the set the
 * engine imports (verified with readelf on libgof2hdaa.so).  EGL is
 * not used by the engine at all (it never creates a context); SDL2 gets
 * EGL from a copy of libmali.so shipped as libEGL.so.1 in run-native.
 *
 * Build (hardfp): arm-none-linux-gnueabihf-gcc -mfloat-abi=hard
 *   -shared -fPIC -O2 gles-bridge.c -ldl -o libGLESv2.so
 */
#include <dlfcn.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>

typedef unsigned int   GLenum;
typedef unsigned int   GLuint;
typedef unsigned int   GLbitfield;
typedef unsigned char  GLboolean;
typedef unsigned char  GLubyte;
typedef char           GLchar;
typedef int            GLint;
typedef int            GLsizei;
typedef long           GLsizeiptr;
typedef long           GLintptr;
typedef void           GLvoid;
typedef float          GLfloat;

#define __HF __attribute__((pcs("aapcs-vfp")))
#define __SF __attribute__((pcs("aapcs")))

static int gt(void) { static int v = -1; if (v < 0) v = getenv("GOF_TRACE") != NULL; return v; }

/* ---- live GL resource counters (GPU-memory leak diagnosis) ---- */
static long glu_now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}
static int glu_log(void) { static int v = -1; if (v < 0) v = getenv("GOF_GLU_LOG") != NULL; return v; }
static unsigned long g_tx_gen, g_tx_del, g_tx_img, g_tx_comp, g_tx_bytes;
static unsigned long g_tx_2d_b, g_tx_cube_b;   /* cumulative bytes by target type */
static unsigned long g_fbo_gen, g_fbo_del, g_rb_gen, g_rb_del;
static unsigned long g_buf_alloc, g_buf_del, g_buf_bytes, g_buf_data_calls, g_buf_gen, g_buf_subdata;

/* live vertex-buffer tracking: id -> current byte size (from glBufferData) */
#define VBUF_MAX 32768
static struct { GLuint id; unsigned long size; } g_vbuf[VBUF_MAX];
static unsigned long g_vbuf_count, g_vbuf_bytes;
#define VTGT_MAX 16
static GLenum  g_bound_tgt[VTGT_MAX];
static GLuint  g_bound_buf[VTGT_MAX];

/* live texture tracking: id -> estimated GPU bytes (comp size or w*h*4) */
#define TEX_MAX 4096
static struct { GLuint id; unsigned long size; } g_tex[ TEX_MAX ];
static unsigned long g_tex_count, g_tex_bytes;
static GLuint g_bound_tex[16];  /* target-index -> texture id (0x0DE1=1, 0x0DE0=0, cube=2..7) */

static GLuint tex_index(GLenum target) {
    if (target >= 0x8515 && target <= 0x851A) return 2;  /* cube faces -> cube slot */
    switch (target) {
        case 0x0DE1: return 1;          /* GL_TEXTURE_2D */
        case 0x0DE0: return 0;          /* GL_TEXTURE_1D */
        case 0x8513: return 2;          /* GL_TEXTURE_CUBE_MAP */
        default: return 3;              /* cube faces + others */
    }
}
static void tex_set(GLuint id, unsigned long size) {
    for (unsigned i = 0; i < g_tex_count; i++) {
        if (g_tex[i].id == id) {
            g_tex_bytes -= g_tex[i].size;
            g_tex[i].size = size;
            g_tex_bytes += size;
            return;
        }
    }
    if (g_tex_count < TEX_MAX) {
        g_tex[g_tex_count].id = id;
        g_tex[g_tex_count].size = size;
        g_tex_count++;
        g_tex_bytes += size;
    }
}
static void tex_del(const GLuint* ids, GLsizei n) {
    for (GLsizei k = 0; k < n; k++) {
        GLuint id = ids[k];
        for (unsigned i = 0; i < g_tex_count; i++) {
            if (g_tex[i].id == id) {
                g_tex_bytes -= g_tex[i].size;
                g_tex[i] = g_tex[g_tex_count - 1];
                g_tex_count--;
                break;
            }
        }
    }
}

static GLuint vbuf_bound(GLenum target) {
    for (int i = 0; i < VTGT_MAX; i++)
        if (g_bound_tgt[i] == target) return g_bound_buf[i];
    return 0;
}
static void vbuf_bind(GLenum target, GLuint id) {
    for (int i = 0; i < VTGT_MAX; i++) {
        if (g_bound_tgt[i] == target) { g_bound_buf[i] = id; return; }
        if (g_bound_tgt[i] == 0) { g_bound_tgt[i] = target; g_bound_buf[i] = id; return; }
    }
}

static void vbuf_resize(GLuint id, unsigned long size) {
    for (unsigned i = 0; i < g_vbuf_count; i++) {
        if (g_vbuf[i].id == id) {
            g_vbuf_bytes -= g_vbuf[i].size;
            g_vbuf[i].size = size;
            g_vbuf_bytes += size;
            return;
        }
    }
    if (g_vbuf_count < VBUF_MAX) {
        g_vbuf[g_vbuf_count].id = id;
        g_vbuf[g_vbuf_count].size = size;
        g_vbuf_count++;
        g_vbuf_bytes += size;
    }
}
static unsigned long vbuf_size(GLuint id) {
    for (unsigned i = 0; i < g_vbuf_count; i++)
        if (g_vbuf[i].id == id) return g_vbuf[i].size;
    return 0;
}

/* live renderbuffer tracking: id -> byte size from glRenderbufferStorage */
#define RB_MAX 64
static struct { GLuint id; unsigned long size; } g_rb[ RB_MAX ];
static unsigned long g_rb_count, g_rb_bytes;
static GLuint g_bound_rb;

static void rb_bind(GLuint id) { g_bound_rb = id; }
static void rb_storage(GLenum internalformat, GLsizei w, GLsizei h) {
    unsigned long sz = (unsigned long)w * (unsigned long)h * 4;
    if (!g_bound_rb) return;
    for (unsigned i = 0; i < g_rb_count; i++) {
        if (g_rb[i].id == g_bound_rb) {
            g_rb_bytes -= g_rb[i].size;
            g_rb[i].size = sz;
            g_rb_bytes += sz;
            return;
        }
    }
    if (g_rb_count < RB_MAX) {
        g_rb[g_rb_count].id = g_bound_rb;
        g_rb[g_rb_count].size = sz;
        g_rb_count++;
        g_rb_bytes += sz;
    }
}
static void rb_del(const GLuint* ids, GLsizei n) {
    for (GLsizei k = 0; k < n; k++) {
        GLuint id = ids[k];
        for (unsigned i = 0; i < g_rb_count; i++) {
            if (g_rb[i].id == id) {
                g_rb_bytes -= g_rb[i].size;
                g_rb[i] = g_rb[g_rb_count - 1];
                g_rb_count--;
                break;
            }
        }
    }
}
static void vbuf_del(GLenum target, const GLuint* ids, GLsizei n) {
    for (GLsizei k = 0; k < n; k++) {
        GLuint id = ids[k];
        for (unsigned i = 0; i < g_vbuf_count; i++) {
            if (g_vbuf[i].id == id) {
                g_vbuf_bytes -= g_vbuf[i].size;
                g_vbuf[i] = g_vbuf[g_vbuf_count - 1];
                g_vbuf_count--;
                break;
            }
        }
    }
    (void)target;
}

/* ---- VBO lifecycle tracker (driver investigation) ----
 * Ring buffer of every glBufferData-sized buffer: id, size, creation ts,
 * deletion ts, generation.  Dumped at the hang so we can correlate the
 * number of /dev/mali0 mappings with created vs live vs deleted VBOs —
 * answering whether deleted VBO allocations stay in the driver. */
#define VLOG_MAX 8192
static struct {
    GLuint id;
    unsigned long size;
    long data_ts;
    long del_ts;
    unsigned long gen;
    int live;
} g_vlog[VLOG_MAX];
static unsigned long g_vlog_pos, g_vlog_gen;

static void vlog_data(GLuint id, unsigned long size) {
    for (unsigned i = 0; i < VLOG_MAX; i++) {
        if (g_vlog[i].gen && g_vlog[i].id == id && g_vlog[i].live) {
            g_vlog[i].size = size;
            g_vlog[i].data_ts = glu_now_ms();
            return;
        }
    }
    g_vlog[g_vlog_pos].id = id;
    g_vlog[g_vlog_pos].size = size;
    g_vlog[g_vlog_pos].data_ts = glu_now_ms();
    g_vlog[g_vlog_pos].del_ts = 0;
    g_vlog[g_vlog_pos].gen = ++g_vlog_gen;
    g_vlog[g_vlog_pos].live = 1;
    g_vlog_pos = (g_vlog_pos + 1) % VLOG_MAX;
}

static void vlog_del(const GLuint* ids, GLsizei n) {
    for (GLsizei k = 0; k < n; k++) {
        GLuint id = ids[k];
        for (unsigned i = 0; i < VLOG_MAX; i++) {
            if (g_vlog[i].gen && g_vlog[i].id == id && g_vlog[i].live) {
                g_vlog[i].del_ts = glu_now_ms();
                g_vlog[i].live = 0;
                break;
            }
        }
    }
}

void gof_vbo_tracker_dump(void) {
    FILE* f = fopen("/root/gof2hd/hang_capture/vbo_tracker.txt", "w");
    if (!f) return;
    long now = glu_now_ms();
    unsigned long live = 0, deleted = 0;
    for (unsigned i = 0; i < VLOG_MAX; i++)
        if (g_vlog[i].gen) { if (g_vlog[i].live) live++; else deleted++; }
    fprintf(f, "# vbo_tracker ts=%ldms created=%lu live=%lu deleted=%lu (ring %u)\n",
            now, g_vlog_gen, live, deleted, (unsigned)VLOG_MAX);
    for (unsigned i = 0; i < VLOG_MAX; i++) {
        if (!g_vlog[i].gen) continue;
        fprintf(f, "gen=%lu id=%u size=%lu data_ts=%ld del_ts=%ld live=%d\n",
                g_vlog[i].gen, g_vlog[i].id, g_vlog[i].size,
                g_vlog[i].data_ts, g_vlog[i].del_ts, g_vlog[i].live);
    }
    fclose(f);
}
static void tx_report(void) {
    static long last = -1;
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    long now = ts.tv_sec;
    if (last < 0) last = now;
    if (now - last >= 10) {
        last = now;
        dprintf(2, "[gltex] liveTex=%lu gen=%lu del=%lu tex2d=%lu comp=%lu ~texMB=%lu fbo=%ld rb=%ld rbBytesMB=%lu liveBuf=%ld gen=%lu del=%lu ~bufMB=%lu vboGen=%lu subdata=%lu liveVBO=%lu vboBytesMB=%lu liveTexBytesMB=%lu cubeMB=%lu twodMB=%lu\n",
                g_tx_gen - g_tx_del, g_tx_gen, g_tx_del, g_tx_img, g_tx_comp,
                g_tx_bytes / (1024 * 1024),
                (long)(g_fbo_gen - g_fbo_del), (long)(g_rb_gen - g_rb_del),
                g_rb_bytes / (1024 * 1024),
                (long)(g_buf_alloc - g_buf_del), g_buf_alloc, g_buf_del,
                g_buf_bytes / (1024 * 1024), g_buf_gen, g_buf_subdata,
                g_vbuf_count, g_vbuf_bytes / (1024 * 1024),
                g_tex_bytes / (1024 * 1024),
                g_tx_cube_b / (1024 * 1024), g_tx_2d_b / (1024 * 1024));
    }
}
static const char* gtext(unsigned int f) {
    switch (f) {
        case 0x6402: return "GL_ALPHA";      case 0x1908: return "GL_RGBA";
        case 0x8058: return "GL_RGBA8";      case 0x8D64: return "ETC1_RGB8";
        case 0x9274: return "ASTC_4x4";      case 0x9270: return "ASTC_4x4(ETC2?)";
        case 0x8C92: return "GL_ATC_RGB";    case 0x8C93: return "GL_ATC_RGBA";
        case 0x8DBC: return "ETC2/ATC?";     default:    return "?";
    }
}

static void* libmali(void) {
    static void* h = 0;
    if (!h) h = dlopen("libmali.so", RTLD_NOW | RTLD_LOCAL);
    return h;
}

static void* sym(void* h, const char* n) {
    if (!h) return 0;
    return dlsym(h, n);
}

/* ---------------- hardfp libmali pointers ---------------- */
static __HF void (*glActiveTexture_hf)(GLenum a);
static __HF void (*glAttachShader_hf)(GLuint a, GLuint b);
static __HF void (*glBindBuffer_hf)(GLenum a, GLuint b);
static __HF void (*glBindFramebuffer_hf)(GLenum a, GLuint b);
static __HF void (*glBindRenderbuffer_hf)(GLenum a, GLuint b);
static __HF void (*glBindTexture_hf)(GLenum a, GLuint b);
static __HF void (*glBlendFunc_hf)(GLenum a, GLenum b);
static __HF void (*glBufferData_hf)(GLenum a, GLsizeiptr b, const void* c, GLenum d);
static __HF void (*glBufferSubData_hf)(GLenum a, GLintptr b, GLsizeiptr c, const void* d);
static __HF GLenum (*glCheckFramebufferStatus_hf)(GLenum a);
static __HF void (*glClear_hf)(GLbitfield a);
static __HF void (*glClearColor_hf)(GLfloat a, GLfloat b, GLfloat c, GLfloat d);
static __HF void (*glColorMask_hf)(GLboolean a, GLboolean b, GLboolean c, GLboolean d);
static __HF void (*glCompileShader_hf)(GLuint a);
static __HF void (*glCompressedTexImage2D_hf)(GLenum a, GLint b, GLenum c, GLsizei d, GLsizei e, GLint f, GLsizei g, const void* h);
static __HF GLuint (*glCreateProgram_hf)(void);
static __HF GLuint (*glCreateShader_hf)(GLenum a);
static __HF void (*glCullFace_hf)(GLenum a);
static __HF void (*glDeleteBuffers_hf)(GLsizei a, const GLuint* b);
static __HF void (*glDeleteFramebuffers_hf)(GLsizei a, const GLuint* b);
static __HF void (*glDeleteProgram_hf)(GLuint a);
static __HF void (*glDeleteRenderbuffers_hf)(GLsizei a, const GLuint* b);
static __HF void (*glDeleteShader_hf)(GLuint a);
static __HF void (*glDeleteTextures_hf)(GLsizei a, const GLuint* b);
static __HF void (*glDepthFunc_hf)(GLenum a);
static __HF void (*glDepthMask_hf)(GLboolean a);
static __HF void (*glDisable_hf)(GLenum a);
static __HF void (*glDisableVertexAttribArray_hf)(GLuint a);
static __HF void (*glDrawArrays_hf)(GLenum a, GLint b, GLsizei c);
static __HF void (*glDrawElements_hf)(GLenum a, GLsizei b, GLenum c, const void* d);
static __HF void (*glFinish_hf)(void);
static __HF void (*glFlush_hf)(void);
static __HF void (*glEnable_hf)(GLenum a);
static __HF void (*glEnableVertexAttribArray_hf)(GLuint a);
static __HF void (*glFramebufferRenderbuffer_hf)(GLenum a, GLenum b, GLenum c, GLuint d);
static __HF void (*glFramebufferTexture2D_hf)(GLenum a, GLenum b, GLenum c, GLuint d, GLint e);
static __HF void (*glGenBuffers_hf)(GLsizei a, GLuint* b);
static __HF void (*glGenerateMipmap_hf)(GLenum a);
static __HF void (*glGenFramebuffers_hf)(GLsizei a, GLuint* b);
static __HF void (*glGenRenderbuffers_hf)(GLsizei a, GLuint* b);
static __HF void (*glGenTextures_hf)(GLsizei a, GLuint* b);
static __HF GLint (*glGetAttribLocation_hf)(GLuint a, const GLchar* b);
static __HF GLenum (*glGetError_hf)(void);
static __HF void (*glGetIntegerv_hf)(GLenum a, GLint* b);
static __HF void (*glGetProgramInfoLog_hf)(GLuint a, GLsizei b, GLsizei* c, GLchar* d);
static __HF void (*glGetProgramiv_hf)(GLuint a, GLenum b, GLint* c);
static __HF void (*glGetShaderInfoLog_hf)(GLuint a, GLsizei b, GLsizei* c, GLchar* d);
static __HF void (*glGetShaderiv_hf)(GLuint a, GLenum b, GLint* c);
static __HF const GLubyte* (*glGetString_hf)(GLenum a);
static __HF GLint (*glGetUniformLocation_hf)(GLuint a, const GLchar* b);
static __HF void (*glLineWidth_hf)(GLfloat a);
static __HF void (*glLinkProgram_hf)(GLuint a);
static __HF void (*glPixelStorei_hf)(GLenum a, GLint b);
static __HF void (*glRenderbufferStorage_hf)(GLenum a, GLenum b, GLsizei c, GLsizei d);
static __HF void (*glScissor_hf)(GLint a, GLint b, GLsizei c, GLsizei d);
static __HF void (*glShaderSource_hf)(GLuint a, GLsizei b, const GLchar* const* c, const GLint* d);
static __HF void (*glTexImage2D_hf)(GLenum a, GLint b, GLint c, GLsizei d, GLsizei e, GLint f, GLenum g, GLenum h, const void* i);
static __HF void (*glTexParameterf_hf)(GLenum a, GLenum b, GLfloat c);
static __HF void (*glTexParameteri_hf)(GLenum a, GLenum b, GLint c);
static __HF void (*glUniform1f_hf)(GLint a, GLfloat b);
static __HF void (*glUniform1i_hf)(GLint a, GLint b);
static __HF void (*glUniform2f_hf)(GLint a, GLfloat b, GLfloat c);
static __HF void (*glUniform3f_hf)(GLint a, GLfloat b, GLfloat c, GLfloat d);
static __HF void (*glUniform3fv_hf)(GLint a, GLsizei b, const GLfloat* c);
static __HF void (*glUniform4f_hf)(GLint a, GLfloat b, GLfloat c, GLfloat d, GLfloat e);
static __HF void (*glUniform4fv_hf)(GLint a, GLsizei b, const GLfloat* c);
static __HF void (*glUniformMatrix3fv_hf)(GLint a, GLsizei b, GLboolean c, const GLfloat* d);
static __HF void (*glUniformMatrix4fv_hf)(GLint a, GLsizei b, GLboolean c, const GLfloat* d);
static __HF void (*glUseProgram_hf)(GLuint a);
static __HF void (*glVertexAttribPointer_hf)(GLuint a, GLint b, GLenum c, GLboolean d, GLsizei e, const void* f);
static __HF void (*glViewport_hf)(GLint a, GLint b, GLsizei c, GLsizei d);

static void resolve_all(void) {
    void* h = libmali();
    *(void**)&glActiveTexture_hf = sym(h, "glActiveTexture");
    *(void**)&glAttachShader_hf = sym(h, "glAttachShader");
    *(void**)&glBindBuffer_hf = sym(h, "glBindBuffer");
    *(void**)&glBindFramebuffer_hf = sym(h, "glBindFramebuffer");
    *(void**)&glBindRenderbuffer_hf = sym(h, "glBindRenderbuffer");
    *(void**)&glBindTexture_hf = sym(h, "glBindTexture");
    *(void**)&glBlendFunc_hf = sym(h, "glBlendFunc");
    *(void**)&glBufferData_hf = sym(h, "glBufferData");
    *(void**)&glBufferSubData_hf = sym(h, "glBufferSubData");
    *(void**)&glCheckFramebufferStatus_hf = sym(h, "glCheckFramebufferStatus");
    *(void**)&glClear_hf = sym(h, "glClear");
    *(void**)&glClearColor_hf = sym(h, "glClearColor");
    *(void**)&glColorMask_hf = sym(h, "glColorMask");
    *(void**)&glCompileShader_hf = sym(h, "glCompileShader");
    *(void**)&glCompressedTexImage2D_hf = sym(h, "glCompressedTexImage2D");
    *(void**)&glCreateProgram_hf = sym(h, "glCreateProgram");
    *(void**)&glCreateShader_hf = sym(h, "glCreateShader");
    *(void**)&glCullFace_hf = sym(h, "glCullFace");
    *(void**)&glDeleteBuffers_hf = sym(h, "glDeleteBuffers");
    *(void**)&glDeleteFramebuffers_hf = sym(h, "glDeleteFramebuffers");
    *(void**)&glDeleteProgram_hf = sym(h, "glDeleteProgram");
    *(void**)&glDeleteRenderbuffers_hf = sym(h, "glDeleteRenderbuffers");
    *(void**)&glDeleteShader_hf = sym(h, "glDeleteShader");
    *(void**)&glDeleteTextures_hf = sym(h, "glDeleteTextures");
    *(void**)&glDepthFunc_hf = sym(h, "glDepthFunc");
    *(void**)&glDepthMask_hf = sym(h, "glDepthMask");
    *(void**)&glDisable_hf = sym(h, "glDisable");
    *(void**)&glDisableVertexAttribArray_hf = sym(h, "glDisableVertexAttribArray");
    *(void**)&glDrawArrays_hf = sym(h, "glDrawArrays");
    *(void**)&glDrawElements_hf = sym(h, "glDrawElements");
    *(void**)&glFinish_hf = sym(h, "glFinish");
    *(void**)&glFlush_hf = sym(h, "glFlush");
    *(void**)&glEnable_hf = sym(h, "glEnable");
    *(void**)&glEnableVertexAttribArray_hf = sym(h, "glEnableVertexAttribArray");
    *(void**)&glFramebufferRenderbuffer_hf = sym(h, "glFramebufferRenderbuffer");
    *(void**)&glFramebufferTexture2D_hf = sym(h, "glFramebufferTexture2D");
    *(void**)&glGenBuffers_hf = sym(h, "glGenBuffers");
    *(void**)&glGenerateMipmap_hf = sym(h, "glGenerateMipmap");
    *(void**)&glGenFramebuffers_hf = sym(h, "glGenFramebuffers");
    *(void**)&glGenRenderbuffers_hf = sym(h, "glGenRenderbuffers");
    *(void**)&glGenTextures_hf = sym(h, "glGenTextures");
    *(void**)&glGetAttribLocation_hf = sym(h, "glGetAttribLocation");
    *(void**)&glGetError_hf = sym(h, "glGetError");
    *(void**)&glGetIntegerv_hf = sym(h, "glGetIntegerv");
    *(void**)&glGetProgramInfoLog_hf = sym(h, "glGetProgramInfoLog");
    *(void**)&glGetProgramiv_hf = sym(h, "glGetProgramiv");
    *(void**)&glGetShaderInfoLog_hf = sym(h, "glGetShaderInfoLog");
    *(void**)&glGetShaderiv_hf = sym(h, "glGetShaderiv");
    *(void**)&glGetString_hf = sym(h, "glGetString");
    *(void**)&glGetUniformLocation_hf = sym(h, "glGetUniformLocation");
    *(void**)&glLineWidth_hf = sym(h, "glLineWidth");
    *(void**)&glLinkProgram_hf = sym(h, "glLinkProgram");
    *(void**)&glPixelStorei_hf = sym(h, "glPixelStorei");
    *(void**)&glRenderbufferStorage_hf = sym(h, "glRenderbufferStorage");
    *(void**)&glScissor_hf = sym(h, "glScissor");
    *(void**)&glShaderSource_hf = sym(h, "glShaderSource");
    *(void**)&glTexImage2D_hf = sym(h, "glTexImage2D");
    *(void**)&glTexParameterf_hf = sym(h, "glTexParameterf");
    *(void**)&glTexParameteri_hf = sym(h, "glTexParameteri");
    *(void**)&glUniform1f_hf = sym(h, "glUniform1f");
    *(void**)&glUniform1i_hf = sym(h, "glUniform1i");
    *(void**)&glUniform2f_hf = sym(h, "glUniform2f");
    *(void**)&glUniform3f_hf = sym(h, "glUniform3f");
    *(void**)&glUniform3fv_hf = sym(h, "glUniform3fv");
    *(void**)&glUniform4f_hf = sym(h, "glUniform4f");
    *(void**)&glUniform4fv_hf = sym(h, "glUniform4fv");
    *(void**)&glUniformMatrix3fv_hf = sym(h, "glUniformMatrix3fv");
    *(void**)&glUniformMatrix4fv_hf = sym(h, "glUniformMatrix4fv");
    *(void**)&glUseProgram_hf = sym(h, "glUseProgram");
    *(void**)&glVertexAttribPointer_hf = sym(h, "glVertexAttribPointer");
    *(void**)&glViewport_hf = sym(h, "glViewport");
}

__attribute__((constructor)) static void bridge_init(void) { resolve_all(); }

/* ---------------- softfp entry points (pcs aapcs) ---------------- */

__SF void glActiveTexture(GLenum a) { if (gt()) dprintf(2, "[gl] activeTex %x\n", a); if (glActiveTexture_hf) glActiveTexture_hf(a); }
__SF void glAttachShader(GLuint a, GLuint b) { if (glAttachShader_hf) glAttachShader_hf(a, b); }
__SF void glBindBuffer(GLenum a, GLuint b) { vbuf_bind(a, b); if (gt()) dprintf(2, "[gl] bindBuffer %x %u\n", a, b); if (glBindBuffer_hf) glBindBuffer_hf(a, b); }
__SF void glBindFramebuffer(GLenum a, GLuint b) { if (gt()) dprintf(2, "[gl] bindFbo %u\n", b); if (glBindFramebuffer_hf) glBindFramebuffer_hf(a, b); }
__SF void glBindRenderbuffer(GLenum a, GLuint b) { rb_bind(b); if (glBindRenderbuffer_hf) glBindRenderbuffer_hf(a, b); }
/* EXPERIMENT C1 (GOF_DUMMY_TEX): substitute a 1x1 dummy texture for every 2D
 * texture bind and skip real uploads, keeping geometry/VBOs + full render loop.
 * If mali0 stops growing -> texture residency participates in the accumulation;
 * if it still OOMs -> VBOs / render queue alone are enough. */
static int dummy_tex_enabled(void) { static int v = -1; if (v < 0) v = getenv("GOF_DUMMY_TEX") != NULL; return v; }
static GLuint g_dummy_tex;
static void ensure_dummy(void) {
    if (g_dummy_tex) return;
    if (!glGenTextures_hf || !glBindTexture_hf || !glTexImage2D_hf) return;
    glGenTextures_hf(1, &g_dummy_tex);
    glBindTexture_hf(0x0DE1, g_dummy_tex);
    static const unsigned char px[4] = { 128, 128, 128, 255 };
    glTexImage2D_hf(0x0DE1, 0, 0x1908, 1, 1, 0, 0x1908, 0x1401, px);
}

__SF void glBindTexture(GLenum a, GLuint b) {
    g_bound_tex[tex_index(a)] = b;
    if (gt() && b) dprintf(2, "[gl] bindTex %u\n", b);
    if (dummy_tex_enabled() && a == 0x0DE1) {
        ensure_dummy();
        if (glBindTexture_hf) glBindTexture_hf(a, g_dummy_tex);
        return;
    }
    if (glBindTexture_hf) glBindTexture_hf(a, b);
}
__SF void glBlendFunc(GLenum a, GLenum b) { if (glBlendFunc_hf) glBlendFunc_hf(a, b); }
__SF void glBufferData(GLenum a, GLsizeiptr b, const void* c, GLenum d) {
    g_buf_data_calls++;
    /* EXPERIMENT: same-size re-spec with data -> glBufferSubData (no GPU re-alloc).
     * Old storage would otherwise be replaced; the driver may hold it until the
     * GPU is done with it.  In-place update avoids the churn. */
    GLuint bound = vbuf_bound(a);
    if (c) {
        if (bound) {
            unsigned long old = vbuf_size(bound);
            if (old > 0 && (unsigned long)b == old) {
                g_buf_subdata++;
                vlog_data(bound, (unsigned long)b);
                tx_report();
                if (glBufferSubData_hf) glBufferSubData_hf(a, 0, b, c);
                return;
            }
            vbuf_resize(bound, (unsigned long)b);
            vlog_data(bound, (unsigned long)b);
        }
        g_buf_alloc++; g_buf_bytes += (unsigned long)b;
        if (b > 1024 * 1024 && glu_log()) dprintf(2, "[glu] %lu bufData %luKB total_buf=%luMB\n", glu_now_ms(),
                                     (unsigned long)b / 1024, g_buf_bytes / (1024 * 1024));
    } else {
        if (bound) {
            vbuf_resize(bound, (unsigned long)b);
            vlog_data(bound, (unsigned long)b);
        }
    }
    if (glBufferData_hf) glBufferData_hf(a, b, c, d);
}
__SF GLenum glCheckFramebufferStatus(GLenum a) { return glCheckFramebufferStatus_hf ? glCheckFramebufferStatus_hf(a) : 0; }
__SF void glClear(GLbitfield a) { if (gt()) dprintf(2, "[gl] clear 0x%x\n", a); if (glClear_hf) glClear_hf(a); }
__SF void glClearColor(GLfloat a, GLfloat b, GLfloat c, GLfloat d) { if (gt()) dprintf(2, "[gl] clearColor %.2f %.2f %.2f %.2f\n", a, b, c, d); if (glClearColor_hf) glClearColor_hf(a, b, c, d); }
__SF void glColorMask(GLboolean a, GLboolean b, GLboolean c, GLboolean d) { if (glColorMask_hf) glColorMask_hf(a, b, c, d); }
__SF void glCompileShader(GLuint a) {
    if (glCompileShader_hf) glCompileShader_hf(a);
    if (gt() && glGetShaderiv_hf && glGetShaderInfoLog_hf) {
        GLint ok = 0, len = 0;
        glGetShaderiv_hf(a, 0x8B81, &ok);
        glGetShaderiv_hf(a, 0x8B88, &len);
        char buf[512];
        glGetShaderInfoLog_hf(a, sizeof(buf) - 1, 0, buf);
        buf[sizeof(buf) - 1] = 0;
        dprintf(2, "[gl] compileShader(%u) ok=%d len=%d log: %s\n", a, ok, len, buf);
    }
}
__SF void glCompressedTexImage2D(GLenum a, GLint b, GLenum c, GLsizei d, GLsizei e, GLint f, GLsizei g, const void* h) {
    g_tx_comp++; g_tx_bytes += (unsigned long)g;
    if (a >= 0x8515 && a <= 0x851A) g_tx_cube_b += (unsigned long)g; else g_tx_2d_b += (unsigned long)g;
    tx_report();
    if (b == 0) { GLuint t = g_bound_tex[tex_index(a)]; if (t) tex_set(t, (unsigned long)g); }
    if (glu_log()) dprintf(2, "[glu] %lu comp %dx%d lvl=%d fmt=0x%x +%luKB total=%luMB\n", glu_now_ms(), d, e, b, c,
            (unsigned long)g / 1024, g_tx_bytes / (1024 * 1024));
    if (gt()) dprintf(2, "[gl] glCompressedTexImage2D(target=%u lvl=%d format=%s %dx%d size=%d\n", a, b, gtext(c), d, e, g);
    if (dummy_tex_enabled()) return;   /* C1: no real texture upload */
    if (glCompressedTexImage2D_hf) glCompressedTexImage2D_hf(a, b, c, d, e, f, g, h);
}__SF GLuint glCreateProgram(void) { GLuint r = glCreateProgram_hf ? glCreateProgram_hf() : 0; if (gt()) dprintf(2, "[gl] createProgram -> %u\n", r); return r; }
__SF GLuint glCreateShader(GLenum a) { GLuint r = glCreateShader_hf ? glCreateShader_hf(a) : 0; if (gt()) dprintf(2, "[gl] createShader %x -> %u\n", a, r); return r; }
__SF void glCullFace(GLenum a) { if (glCullFace_hf) glCullFace_hf(a); }
__SF void glBufferSubData(GLenum a, GLintptr b, GLsizeiptr c, const void* d) { g_buf_subdata++; if (glBufferSubData_hf) glBufferSubData_hf(a, b, c, d); }
__SF void glDeleteBuffers(GLsizei a, const GLuint* b) { g_buf_del += (unsigned long)a; vbuf_del(0, b, a); vlog_del(b, a); tx_report(); if (glDeleteBuffers_hf) glDeleteBuffers_hf(a, b); }
__SF void glDeleteFramebuffers(GLsizei a, const GLuint* b) { g_fbo_del += (unsigned long)a; tx_report(); if (glDeleteFramebuffers_hf) glDeleteFramebuffers_hf(a, b); }
__SF void glDeleteProgram(GLuint a) { if (glDeleteProgram_hf) glDeleteProgram_hf(a); }
__SF void glDeleteRenderbuffers(GLsizei a, const GLuint* b) { g_rb_del += (unsigned long)a; rb_del(b, a); tx_report(); if (glDeleteRenderbuffers_hf) glDeleteRenderbuffers_hf(a, b); }
__SF void glDeleteShader(GLuint a) { if (glDeleteShader_hf) glDeleteShader_hf(a); }
/* EXPERIMENT (GOF_TEX_POOL): texture reuse pool.
 * On glDeleteTextures KEEP the physical GPU texture alive and put the ID in a
 * free pool instead of destroying it; glGenTextures returns pooled IDs first.
 * Re-uploading via glTexImage2D on a pooled texture reuses the driver mapping
 * (instead of creating a new one every cycle), so the mapping count stays
 * bounded by the pool rather than by total uploads.  Closer to how a native
 * PC engine manages resources; no content hashing needed. */
__SF void glDeleteTextures(GLsizei a, const GLuint* b) { g_tx_del += (unsigned long)a; tex_del(b, a); tx_report(); if (glDeleteTextures_hf) glDeleteTextures_hf(a, b); }
__SF void glDepthFunc(GLenum a) { if (glDepthFunc_hf) glDepthFunc_hf(a); }
__SF void glDepthMask(GLboolean a) { if (glDepthMask_hf) glDepthMask_hf(a); }
__SF void glDisable(GLenum a) { if (gt()) dprintf(2, "[gl] disable %x\n", a); if (glDisable_hf) glDisable_hf(a); }
__SF void glDisableVertexAttribArray(GLuint a) { if (glDisableVertexAttribArray_hf) glDisableVertexAttribArray_hf(a); }
__SF void glDrawArrays(GLenum a, GLint b, GLsizei c) { if (gt()) dprintf(2, "[gl] draw %x n=%d\n", a, c); if (glDrawArrays_hf) glDrawArrays_hf(a, b, c); }
__SF void glDrawElements(GLenum a, GLsizei b, GLenum c, const void* d) { if (gt()) dprintf(2, "[gl] drawElements %x n=%d\n", a, b); if (glDrawElements_hf) glDrawElements_hf(a, b, c, d); }
__SF void glFinish(void) { if (glFinish_hf) glFinish_hf(); }
__SF void glFlush(void) { if (glFlush_hf) glFlush_hf(); }
__SF void glEnable(GLenum a) { if (gt()) dprintf(2, "[gl] enable %x\n", a); if (glEnable_hf) glEnable_hf(a); }
__SF void glEnableVertexAttribArray(GLuint a) { if (gt()) dprintf(2, "[gl] enableAttrib %u\n", a); if (glEnableVertexAttribArray_hf) glEnableVertexAttribArray_hf(a); }
__SF void glFramebufferRenderbuffer(GLenum a, GLenum b, GLenum c, GLuint d) { if (glFramebufferRenderbuffer_hf) glFramebufferRenderbuffer_hf(a, b, c, d); }
__SF void glFramebufferTexture2D(GLenum a, GLenum b, GLenum c, GLuint d, GLint e) { if (glFramebufferTexture2D_hf) glFramebufferTexture2D_hf(a, b, c, d, e); }
__SF void glGenBuffers(GLsizei a, GLuint* b) { g_buf_gen += (unsigned long)a; tx_report(); if (glGenBuffers_hf) glGenBuffers_hf(a, b); }
__SF void glGenerateMipmap(GLenum a) { if (glGenerateMipmap_hf) glGenerateMipmap_hf(a); }
__SF void glGenFramebuffers(GLsizei a, GLuint* b) { g_fbo_gen += (unsigned long)a; tx_report(); if (glGenFramebuffers_hf) glGenFramebuffers_hf(a, b); }
__SF void glGenRenderbuffers(GLsizei a, GLuint* b) { g_rb_gen += (unsigned long)a; tx_report(); if (glGenRenderbuffers_hf) glGenRenderbuffers_hf(a, b); }
__SF void glGenTextures(GLsizei a, GLuint* b) { g_tx_gen += (unsigned long)a; tx_report(); if (glGenTextures_hf) glGenTextures_hf(a, b); if (gt() && b) { dprintf(2, "[gl] genTextures n=%d -> ", a); for (GLsizei i = 0; i < a; i++) dprintf(2, "%u ", b[i]); dprintf(2, "\n"); } }
__SF GLint glGetAttribLocation(GLuint a, const GLchar* b) { GLint r = glGetAttribLocation_hf ? glGetAttribLocation_hf(a, b) : -1; if (gt()) dprintf(2, "[gl] getAttribLoc %u '%s' -> %d\n", a, b, r); return r; }
__SF GLenum glGetError(void) { GLenum e = glGetError_hf ? glGetError_hf() : 0; if (gt() && e) dprintf(2, "[gl] !!! glGetError=0x%x\n", e); return e; }
__SF void glGetIntegerv(GLenum a, GLint* b) { if (gt() && (a == 0x0D33 || a == 0x0D32)) dprintf(2, "[gl] getiv %x = %d\n", a, b ? b[0] : -1); if (glGetIntegerv_hf) glGetIntegerv_hf(a, b); }
__SF void glGetProgramInfoLog(GLuint a, GLsizei b, GLsizei* c, GLchar* d) {
    if (glGetProgramInfoLog_hf) glGetProgramInfoLog_hf(a, b, c, d);
    if (gt() && d && b > 1) dprintf(2, "[gl] progLog(%u): %.300s\n", a, d);
}
__SF void glGetProgramiv(GLuint a, GLenum b, GLint* c) {
    if (glGetProgramiv_hf) glGetProgramiv_hf(a, b, c);
    if (gt() && c && (b == 0x8B82 /*LINK_STATUS*/ || b == 0x8B84 /*VALIDATE*/ || b == 0x8B87 /*ACTIVE_UNIFORMS*/))
        dprintf(2, "[gl] progiv(%u,0x%x)=%d\n", a, b, c[0]);
}
__SF void glGetShaderInfoLog(GLuint a, GLsizei b, GLsizei* c, GLchar* d) {
    if (glGetShaderInfoLog_hf) glGetShaderInfoLog_hf(a, b, c, d);
    if (gt() && d && b > 1) dprintf(2, "[gl] shaderLog(%u): %.300s\n", a, d);
}
__SF void glGetShaderiv(GLuint a, GLenum b, GLint* c) {
    if (glGetShaderiv_hf) glGetShaderiv_hf(a, b, c);
    if (gt() && c && (b == 0x8B81 /*COMPILE_STATUS*/ || b == 0x8B88 /*INFO_LOG_LENGTH*/))
        dprintf(2, "[gl] shaderiv(%u,0x%x)=%d\n", a, b, c[0]);
}
__SF const GLubyte* glGetString(GLenum a) { const GLubyte* r = glGetString_hf ? glGetString_hf(a) : 0; if (gt()) dprintf(2, "[gl] getString %x -> '%s'\n", a, r ? (const char*)r : "(null)"); return r; }
__SF GLint glGetUniformLocation(GLuint a, const GLchar* b) { return glGetUniformLocation_hf ? glGetUniformLocation_hf(a, b) : -1; }
__SF void glLineWidth(GLfloat a) { if (glLineWidth_hf) glLineWidth_hf(a); }
__SF void glLinkProgram(GLuint a) {
    if (glLinkProgram_hf) glLinkProgram_hf(a);
    if (gt() && glGetProgramiv_hf && glGetProgramInfoLog_hf) {
        GLint ok = 0, len = 0;
        glGetProgramiv_hf(a, 0x8B82, &ok);
        glGetProgramiv_hf(a, 0x8B87, &len);
        char buf[512];
        glGetProgramInfoLog_hf(a, sizeof(buf) - 1, 0, buf);
        buf[sizeof(buf) - 1] = 0;
        dprintf(2, "[gl] linkProgram(%u) ok=%d log: %s\n", a, ok, buf);
    }
}
__SF void glPixelStorei(GLenum a, GLint b) { if (glPixelStorei_hf) glPixelStorei_hf(a, b); }
__SF void glRenderbufferStorage(GLenum a, GLenum b, GLsizei c, GLsizei d) { rb_storage(b, c, d); tx_report(); if (glRenderbufferStorage_hf) glRenderbufferStorage_hf(a, b, c, d); }
__SF void glScissor(GLint a, GLint b, GLsizei c, GLsizei d) { if (glScissor_hf) glScissor_hf(a, b, c, d); }
__SF void glShaderSource(GLuint a, GLsizei b, const GLchar* const* c, const GLint* d) { if (gt()) dprintf(2, "[gl] shaderSource %u\n", a); if (glShaderSource_hf) glShaderSource_hf(a, b, c, d); }

__SF void glTexImage2D(GLenum a, GLint b, GLint c, GLsizei d, GLsizei e, GLint f, GLenum g, GLenum h, const void* i) {
    g_tx_img++; g_tx_bytes += (unsigned long)d * (unsigned long)e * 4;
    if (a >= 0x8515 && a <= 0x851A) g_tx_cube_b += (unsigned long)d * (unsigned long)e * 4; else g_tx_2d_b += (unsigned long)d * (unsigned long)e * 4;
    tx_report();
    if (b == 0) { GLuint t = g_bound_tex[tex_index(a)]; if (t) tex_set(t, (unsigned long)d * (unsigned long)e * 4); }
    if (glu_log()) dprintf(2, "[glu] %lu tex2d %dx%d lvl=%d ifmt=0x%x fmt=0x%x +%luKB total=%luMB\n", glu_now_ms(), d, e, b, c, g,
            ((unsigned long)d * (unsigned long)e * 4) / 1024, g_tx_bytes / (1024 * 1024));
    if ((unsigned long)d * (unsigned long)e > 4096 * 4096) dprintf(2, "[gltex] BIG tex2d %dx%d\n", d, e);
    if (gt()) dprintf(2, "[gl] glTexImage2D lvl=%d %dx%d ifmt=%s pixfmt=%s target=%d\n", b, d, e, gtext(c), gtext(g), a);
    if (dummy_tex_enabled()) return;   /* C1: no real texture upload */
    if (glTexImage2D_hf) glTexImage2D_hf(a, b, c, d, e, f, g, h, i);
}
__SF void glTexParameterf(GLenum a, GLenum b, GLfloat c) { if (glTexParameterf_hf) glTexParameterf_hf(a, b, c); }
__SF void glTexParameteri(GLenum a, GLenum b, GLint c) { if (glTexParameteri_hf) glTexParameteri_hf(a, b, c); }
__SF void glUniform1f(GLint a, GLfloat b) { if (glUniform1f_hf) glUniform1f_hf(a, b); }
__SF void glUniform1i(GLint a, GLint b) { if (glUniform1i_hf) glUniform1i_hf(a, b); }
__SF void glUniform2f(GLint a, GLfloat b, GLfloat c) { if (glUniform2f_hf) glUniform2f_hf(a, b, c); }
__SF void glUniform3f(GLint a, GLfloat b, GLfloat c, GLfloat d) { if (glUniform3f_hf) glUniform3f_hf(a, b, c, d); }
__SF void glUniform3fv(GLint a, GLsizei b, const GLfloat* c) { if (glUniform3fv_hf) glUniform3fv_hf(a, b, c); }
__SF void glUniform4f(GLint a, GLfloat b, GLfloat c, GLfloat d, GLfloat e) { if (glUniform4f_hf) glUniform4f_hf(a, b, c, d, e); }
__SF void glUniform4fv(GLint a, GLsizei b, const GLfloat* c) { if (glUniform4fv_hf) glUniform4fv_hf(a, b, c); }
__SF void glUniformMatrix3fv(GLint a, GLsizei b, GLboolean c, const GLfloat* d) { if (glUniformMatrix3fv_hf) glUniformMatrix3fv_hf(a, b, c, d); }
__SF void glUniformMatrix4fv(GLint a, GLsizei b, GLboolean c, const GLfloat* d) { if (glUniformMatrix4fv_hf) glUniformMatrix4fv_hf(a, b, c, d); }
__SF void glUseProgram(GLuint a) { if (gt() && a) dprintf(2, "[gl] useProg %u\n", a); if (glUseProgram_hf) glUseProgram_hf(a); }
__SF void glVertexAttribPointer(GLuint a, GLint b, GLenum c, GLboolean d, GLsizei e, const void* f) { if (gt()) dprintf(2, "[gl] attribPtr %u sz=%d ty=%x st=%d\n", a, b, c, e); if (glVertexAttribPointer_hf) glVertexAttribPointer_hf(a, b, c, d, e, f); }
__SF void glViewport(GLint a, GLint b, GLsizei c, GLsizei d) { if (gt()) dprintf(2, "[gl] viewport %d %d %dx%d\n", a, b, c, d); if (glViewport_hf) glViewport_hf(a, b, c, d); }
