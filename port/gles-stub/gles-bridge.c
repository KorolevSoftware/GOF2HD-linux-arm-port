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
 * Build (hardfp): arm-none-linux-gnueabihf-gcc -mfloat-abi=hard
 *   -shared -fPIC -O2 gles-bridge.c -ldl -o libGLESv2.so
 */
#include <dlfcn.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

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
static const char* gtext(unsigned int f) {
    switch (f) {
        case 0x6402: return "GL_ALPHA";      case 0x1908: return "GL_RGBA";
        case 0x8058: return "GL_RGBA8";      case 0x8D64: return "ETC1_RGB8";
        case 0x9274: return "ASTC_4x4";      case 0x9270: return "ASTC_4x4(ETC2?)";
        case 0x8C92: return "GL_ATC_RGB";    case 0x8C93: return "GL_ATC_RGBA";
        case 0x8DBC: return "ETC2/ATC?";     default:    return "?";
    }
}
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
static __HF void (*glAlphaFunc_hf)(GLenum a, GLfloat b);
static __HF void (*glAttachShader_hf)(GLuint a, GLuint b);
static __HF void (*glBindBuffer_hf)(GLenum a, GLuint b);
static __HF void (*glBindFramebuffer_hf)(GLenum a, GLuint b);
static __HF void (*glBindRenderbuffer_hf)(GLenum a, GLuint b);
static __HF void (*glBindTexture_hf)(GLenum a, GLuint b);
static __HF void (*glBlendFunc_hf)(GLenum a, GLenum b);
static __HF void (*glBufferData_hf)(GLenum a, GLsizeiptr b, const void* c, GLenum d);
static __HF GLenum (*glCheckFramebufferStatus_hf)(GLenum a);
static __HF void (*glClear_hf)(GLbitfield a);
static __HF void (*glClearColor_hf)(GLfloat a, GLfloat b, GLfloat c, GLfloat d);
static __HF void (*glClientActiveTexture_hf)(GLenum a);
static __HF void (*glColor4f_hf)(GLfloat a, GLfloat b, GLfloat c, GLfloat d);
static __HF void (*glColor4ub_hf)(GLubyte a, GLubyte b, GLubyte c, GLubyte d);
static __HF void (*glColor4fv_hf)(const GLfloat* a);
static __HF void (*glColorMask_hf)(GLboolean a, GLboolean b, GLboolean c, GLboolean d);
static __HF void (*glColorPointer_hf)(GLint a, GLenum b, GLsizei c, const void* d);
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
static __HF void (*glDisableClientState_hf)(GLenum a);
static __HF void (*glDisableVertexAttribArray_hf)(GLuint a);
static __HF void (*glDrawArrays_hf)(GLenum a, GLint b, GLsizei c);
static __HF void (*glDrawElements_hf)(GLenum a, GLsizei b, GLenum c, const void* d);
static __HF void (*glEnable_hf)(GLenum a);
static __HF void (*glEnableClientState_hf)(GLenum a);
static __HF void (*glEnableVertexAttribArray_hf)(GLuint a);
static __HF void (*glFogf_hf)(GLenum a, GLfloat b);
static __HF void (*glFogfv_hf)(GLenum a, const GLfloat* b);
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
static __HF void (*glHint_hf)(GLenum a, GLenum b);
static __HF void (*glLightfv_hf)(GLenum a, GLenum b, const GLfloat* c);
static __HF void (*glLightModelfv_hf)(GLenum a, const GLfloat* b);
static __HF void (*glLineWidth_hf)(GLfloat a);
static __HF void (*glLinkProgram_hf)(GLuint a);
static __HF void (*glLoadIdentity_hf)(void);
static __HF void (*glLoadMatrixf_hf)(const GLfloat* a);
static __HF void (*glMaterialf_hf)(GLenum a, GLenum b, GLfloat c);
static __HF void (*glMaterialfv_hf)(GLenum a, GLenum b, const GLfloat* c);
static __HF void (*glMatrixMode_hf)(GLenum a);
static __HF void (*glMultMatrixf_hf)(const GLfloat* a);
static __HF void (*glNormalPointer_hf)(GLenum a, GLsizei b, const void* c);
static __HF void (*glPixelStorei_hf)(GLenum a, GLint b);
static __HF void (*glRenderbufferStorage_hf)(GLenum a, GLenum b, GLsizei c, GLsizei d);
static __HF void (*glScalef_hf)(GLfloat a, GLfloat b, GLfloat c);
static __HF void (*glScissor_hf)(GLint a, GLint b, GLsizei c, GLsizei d);
static __HF void (*glShaderSource_hf)(GLuint a, GLsizei b, const GLchar* const* c, const GLint* d);
static __HF void (*glShadeModel_hf)(GLenum a);
static __HF void (*glTexCoordPointer_hf)(GLint a, GLenum b, GLsizei c, const void* d);
static __HF void (*glTexEnvf_hf)(GLenum a, GLenum b, GLfloat c);
static __HF void (*glTexEnvi_hf)(GLenum a, GLenum b, GLint c);
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
static __HF void (*glVertexPointer_hf)(GLint a, GLenum b, GLsizei c, const void* d);
static __HF void (*glViewport_hf)(GLint a, GLint b, GLsizei c, GLsizei d);

static void resolve_all(void) {
    void* h = libmali();
    *(void**)&glActiveTexture_hf = sym(h, "glActiveTexture");
    *(void**)&glAlphaFunc_hf = sym(h, "glAlphaFunc");
    *(void**)&glAttachShader_hf = sym(h, "glAttachShader");
    *(void**)&glBindBuffer_hf = sym(h, "glBindBuffer");
    *(void**)&glBindFramebuffer_hf = sym(h, "glBindFramebuffer");
    *(void**)&glBindRenderbuffer_hf = sym(h, "glBindRenderbuffer");
    *(void**)&glBindTexture_hf = sym(h, "glBindTexture");
    *(void**)&glBlendFunc_hf = sym(h, "glBlendFunc");
    *(void**)&glBufferData_hf = sym(h, "glBufferData");
    *(void**)&glCheckFramebufferStatus_hf = sym(h, "glCheckFramebufferStatus");
    *(void**)&glClear_hf = sym(h, "glClear");
    *(void**)&glClearColor_hf = sym(h, "glClearColor");
    *(void**)&glClientActiveTexture_hf = sym(h, "glClientActiveTexture");
    *(void**)&glColor4f_hf = sym(h, "glColor4f");
    *(void**)&glColor4ub_hf = sym(h, "glColor4ub");
    *(void**)&glColor4fv_hf = sym(h, "glColor4fv");
    *(void**)&glColorMask_hf = sym(h, "glColorMask");
    *(void**)&glColorPointer_hf = sym(h, "glColorPointer");
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
    *(void**)&glDisableClientState_hf = sym(h, "glDisableClientState");
    *(void**)&glDisableVertexAttribArray_hf = sym(h, "glDisableVertexAttribArray");
    *(void**)&glDrawArrays_hf = sym(h, "glDrawArrays");
    *(void**)&glDrawElements_hf = sym(h, "glDrawElements");
    *(void**)&glEnable_hf = sym(h, "glEnable");
    *(void**)&glEnableClientState_hf = sym(h, "glEnableClientState");
    *(void**)&glEnableVertexAttribArray_hf = sym(h, "glEnableVertexAttribArray");
    *(void**)&glFogf_hf = sym(h, "glFogf");
    *(void**)&glFogfv_hf = sym(h, "glFogfv");
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
    *(void**)&glHint_hf = sym(h, "glHint");
    *(void**)&glLightfv_hf = sym(h, "glLightfv");
    *(void**)&glLightModelfv_hf = sym(h, "glLightModelfv");
    *(void**)&glLineWidth_hf = sym(h, "glLineWidth");
    *(void**)&glLinkProgram_hf = sym(h, "glLinkProgram");
    *(void**)&glLoadIdentity_hf = sym(h, "glLoadIdentity");
    *(void**)&glLoadMatrixf_hf = sym(h, "glLoadMatrixf");
    *(void**)&glMaterialf_hf = sym(h, "glMaterialf");
    *(void**)&glMaterialfv_hf = sym(h, "glMaterialfv");
    *(void**)&glMatrixMode_hf = sym(h, "glMatrixMode");
    *(void**)&glMultMatrixf_hf = sym(h, "glMultMatrixf");
    *(void**)&glNormalPointer_hf = sym(h, "glNormalPointer");
    *(void**)&glPixelStorei_hf = sym(h, "glPixelStorei");
    *(void**)&glRenderbufferStorage_hf = sym(h, "glRenderbufferStorage");
    *(void**)&glScalef_hf = sym(h, "glScalef");
    *(void**)&glScissor_hf = sym(h, "glScissor");
    *(void**)&glShaderSource_hf = sym(h, "glShaderSource");
    *(void**)&glShadeModel_hf = sym(h, "glShadeModel");
    *(void**)&glTexCoordPointer_hf = sym(h, "glTexCoordPointer");
    *(void**)&glTexEnvf_hf = sym(h, "glTexEnvf");
    *(void**)&glTexEnvi_hf = sym(h, "glTexEnvi");
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
    *(void**)&glVertexPointer_hf = sym(h, "glVertexPointer");
    *(void**)&glViewport_hf = sym(h, "glViewport");
}

__attribute__((constructor)) static void bridge_init(void) { resolve_all(); }

/* ---------------- softfp entry points (pcs aapcs) ---------------- */

__SF void glActiveTexture(GLenum a) { if (gt()) dprintf(2, "[gl] activeTex %x\n", a); if (glActiveTexture_hf) glActiveTexture_hf(a); }
__SF void glAlphaFunc(GLenum a, GLfloat b) { if (glAlphaFunc_hf) glAlphaFunc_hf(a, b); }
__SF void glAttachShader(GLuint a, GLuint b) { if (glAttachShader_hf) glAttachShader_hf(a, b); }
__SF void glBindBuffer(GLenum a, GLuint b) { if (gt()) dprintf(2, "[gl] bindBuffer %x %u\n", a, b); if (glBindBuffer_hf) glBindBuffer_hf(a, b); }
__SF void glBindFramebuffer(GLenum a, GLuint b) { if (gt()) dprintf(2, "[gl] bindFbo %u\n", b); if (glBindFramebuffer_hf) glBindFramebuffer_hf(a, b); }
__SF void glBindRenderbuffer(GLenum a, GLuint b) { if (glBindRenderbuffer_hf) glBindRenderbuffer_hf(a, b); }
__SF void glBindTexture(GLenum a, GLuint b) { if (gt() && b) dprintf(2, "[gl] bindTex %u\n", b); if (glBindTexture_hf) glBindTexture_hf(a, b); }
__SF void glBlendFunc(GLenum a, GLenum b) { if (glBlendFunc_hf) glBlendFunc_hf(a, b); }
__SF void glBufferData(GLenum a, GLsizeiptr b, const void* c, GLenum d) { if (glBufferData_hf) glBufferData_hf(a, b, c, d); }
__SF GLenum glCheckFramebufferStatus(GLenum a) { return glCheckFramebufferStatus_hf ? glCheckFramebufferStatus_hf(a) : 0; }
__SF void glClear(GLbitfield a) { if (gt()) dprintf(2, "[gl] clear 0x%x\n", a); if (glClear_hf) glClear_hf(a); }
__SF void glClearColor(GLfloat a, GLfloat b, GLfloat c, GLfloat d) { if (gt()) dprintf(2, "[gl] clearColor %.2f %.2f %.2f %.2f\n", a, b, c, d); if (glClearColor_hf) glClearColor_hf(a, b, c, d); }
__SF void glClientActiveTexture(GLenum a) { if (glClientActiveTexture_hf) glClientActiveTexture_hf(a); }
__SF void glColor4f(GLfloat a, GLfloat b, GLfloat c, GLfloat d) { if (glColor4f_hf) glColor4f_hf(a, b, c, d); }
__SF void glColor4ub(GLubyte a, GLubyte b, GLubyte c, GLubyte d) { if (glColor4ub_hf) glColor4ub_hf(a, b, c, d); }
__SF void glColor4fv(const GLfloat* a) { if (glColor4fv_hf) glColor4fv_hf(a); }
__SF void glColorMask(GLboolean a, GLboolean b, GLboolean c, GLboolean d) { if (glColorMask_hf) glColorMask_hf(a, b, c, d); }
__SF void glColorPointer(GLint a, GLenum b, GLsizei c, const void* d) { if (glColorPointer_hf) glColorPointer_hf(a, b, c, d); }
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
    if (gt()) dprintf(2, "[gl] glCompressedTexImage2D(target=%u lvl=%d format=%s %dx%d size=%d\n", a, b, gtext(c), d, e, g);
    if (glCompressedTexImage2D_hf) glCompressedTexImage2D_hf(a, b, c, d, e, f, g, h);
}__SF GLuint glCreateProgram(void) { GLuint r = glCreateProgram_hf ? glCreateProgram_hf() : 0; if (gt()) dprintf(2, "[gl] createProgram -> %u\n", r); return r; }
__SF GLuint glCreateShader(GLenum a) { GLuint r = glCreateShader_hf ? glCreateShader_hf(a) : 0; if (gt()) dprintf(2, "[gl] createShader %x -> %u\n", a, r); return r; }
__SF void glCullFace(GLenum a) { if (glCullFace_hf) glCullFace_hf(a); }
__SF void glDeleteBuffers(GLsizei a, const GLuint* b) { if (glDeleteBuffers_hf) glDeleteBuffers_hf(a, b); }
__SF void glDeleteFramebuffers(GLsizei a, const GLuint* b) { if (glDeleteFramebuffers_hf) glDeleteFramebuffers_hf(a, b); }
__SF void glDeleteProgram(GLuint a) { if (glDeleteProgram_hf) glDeleteProgram_hf(a); }
__SF void glDeleteRenderbuffers(GLsizei a, const GLuint* b) { if (glDeleteRenderbuffers_hf) glDeleteRenderbuffers_hf(a, b); }
__SF void glDeleteShader(GLuint a) { if (glDeleteShader_hf) glDeleteShader_hf(a); }
__SF void glDeleteTextures(GLsizei a, const GLuint* b) { if (glDeleteTextures_hf) glDeleteTextures_hf(a, b); }
__SF void glDepthFunc(GLenum a) { if (glDepthFunc_hf) glDepthFunc_hf(a); }
__SF void glDepthMask(GLboolean a) { if (glDepthMask_hf) glDepthMask_hf(a); }
__SF void glDisable(GLenum a) { if (gt()) dprintf(2, "[gl] disable %x\n", a); if (glDisable_hf) glDisable_hf(a); }
__SF void glDisableClientState(GLenum a) { if (glDisableClientState_hf) glDisableClientState_hf(a); }
__SF void glDisableVertexAttribArray(GLuint a) { if (glDisableVertexAttribArray_hf) glDisableVertexAttribArray_hf(a); }
__SF void glDrawArrays(GLenum a, GLint b, GLsizei c) { if (gt()) dprintf(2, "[gl] draw %x n=%d\n", a, c); if (glDrawArrays_hf) glDrawArrays_hf(a, b, c); }
__SF void glDrawElements(GLenum a, GLsizei b, GLenum c, const void* d) { if (gt()) dprintf(2, "[gl] drawElements %x n=%d\n", a, b); if (glDrawElements_hf) glDrawElements_hf(a, b, c, d); }
__SF void glEnable(GLenum a) { if (gt()) dprintf(2, "[gl] enable %x\n", a); if (glEnable_hf) glEnable_hf(a); }
__SF void glEnableClientState(GLenum a) { if (gt()) dprintf(2, "[gl] enableClientState %x\n", a); if (glEnableClientState_hf) glEnableClientState_hf(a); }
__SF void glEnableVertexAttribArray(GLuint a) { if (gt()) dprintf(2, "[gl] enableAttrib %u\n", a); if (glEnableVertexAttribArray_hf) glEnableVertexAttribArray_hf(a); }
__SF void glFogf(GLenum a, GLfloat b) { if (glFogf_hf) glFogf_hf(a, b); }
__SF void glFogfv(GLenum a, const GLfloat* b) { if (glFogfv_hf) glFogfv_hf(a, b); }
__SF void glFramebufferRenderbuffer(GLenum a, GLenum b, GLenum c, GLuint d) { if (glFramebufferRenderbuffer_hf) glFramebufferRenderbuffer_hf(a, b, c, d); }
__SF void glFramebufferTexture2D(GLenum a, GLenum b, GLenum c, GLuint d, GLint e) { if (glFramebufferTexture2D_hf) glFramebufferTexture2D_hf(a, b, c, d, e); }
__SF void glGenBuffers(GLsizei a, GLuint* b) { if (glGenBuffers_hf) glGenBuffers_hf(a, b); }
__SF void glGenerateMipmap(GLenum a) { if (glGenerateMipmap_hf) glGenerateMipmap_hf(a); }
__SF void glGenFramebuffers(GLsizei a, GLuint* b) { if (glGenFramebuffers_hf) glGenFramebuffers_hf(a, b); }
__SF void glGenRenderbuffers(GLsizei a, GLuint* b) { if (glGenRenderbuffers_hf) glGenRenderbuffers_hf(a, b); }
__SF void glGenTextures(GLsizei a, GLuint* b) { if (glGenTextures_hf) glGenTextures_hf(a, b); if (gt() && b) { dprintf(2, "[gl] genTextures n=%d -> ", a); for (GLsizei i = 0; i < a; i++) dprintf(2, "%u ", b[i]); dprintf(2, "\n"); } }
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
__SF void glHint(GLenum a, GLenum b) { if (glHint_hf) glHint_hf(a, b); }
__SF void glLightfv(GLenum a, GLenum b, const GLfloat* c) { if (glLightfv_hf) glLightfv_hf(a, b, c); }
__SF void glLightModelfv(GLenum a, const GLfloat* b) { if (glLightModelfv_hf) glLightModelfv_hf(a, b); }
__SF const GLubyte* glGetString(GLenum a) { const GLubyte* r = glGetString_hf ? glGetString_hf(a) : 0; if (gt()) dprintf(2, "[gl] getString %x -> '%s'\n", a, r ? (const char*)r : "(null)"); return r; }
__SF GLint glGetUniformLocation(GLuint a, const GLchar* b) { return glGetUniformLocation_hf ? glGetUniformLocation_hf(a, b) : -1; }
__SF void glLineWidth(GLfloat a) { if (glLineWidth_hf) glLineWidth_hf(a); }
__SF void glLoadIdentity(void) { if (glLoadIdentity_hf) glLoadIdentity_hf(); }
__SF void glLoadMatrixf(const GLfloat* a) { if (glLoadMatrixf_hf) glLoadMatrixf_hf(a); }
__SF void glMaterialf(GLenum a, GLenum b, GLfloat c) { if (glMaterialf_hf) glMaterialf_hf(a, b, c); }
__SF void glMaterialfv(GLenum a, GLenum b, const GLfloat* c) { if (glMaterialfv_hf) glMaterialfv_hf(a, b, c); }
__SF void glMatrixMode(GLenum a) { if (gt()) dprintf(2, "[gl] matrixMode %x\n", a); if (glMatrixMode_hf) glMatrixMode_hf(a); }
__SF void glMultMatrixf(const GLfloat* a) { if (glMultMatrixf_hf) glMultMatrixf_hf(a); }
__SF void glNormalPointer(GLenum a, GLsizei b, const void* c) { if (glNormalPointer_hf) glNormalPointer_hf(a, b, c); }
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
__SF void glRenderbufferStorage(GLenum a, GLenum b, GLsizei c, GLsizei d) { if (glRenderbufferStorage_hf) glRenderbufferStorage_hf(a, b, c, d); }
__SF void glScalef(GLfloat a, GLfloat b, GLfloat c) { if (glScalef_hf) glScalef_hf(a, b, c); }
__SF void glScissor(GLint a, GLint b, GLsizei c, GLsizei d) { if (glScissor_hf) glScissor_hf(a, b, c, d); }
__SF void glShaderSource(GLuint a, GLsizei b, const GLchar* const* c, const GLint* d) { if (gt()) dprintf(2, "[gl] shaderSource %u\n", a); if (glShaderSource_hf) glShaderSource_hf(a, b, c, d); }
__SF void glTexImage2D(GLenum a, GLint b, GLint c, GLsizei d, GLsizei e, GLint f, GLenum g, GLenum h, const void* i) {
    if (gt()) dprintf(2, "[gl] glTexImage2D lvl=%d %dx%d ifmt=%s pixfmt=%s target=%d\n", b, d, e, gtext(c), gtext(g), a);
    if (glTexImage2D_hf) glTexImage2D_hf(a, b, c, d, e, f, g, h, i);
}
__SF void glTexParameterf(GLenum a, GLenum b, GLfloat c) { if (glTexParameterf_hf) glTexParameterf_hf(a, b, c); }
__SF void glTexParameteri(GLenum a, GLenum b, GLint c) { if (glTexParameteri_hf) glTexParameteri_hf(a, b, c); }
__SF void glShadeModel(GLenum a) { if (glShadeModel_hf) glShadeModel_hf(a); }
__SF void glTexCoordPointer(GLint a, GLenum b, GLsizei c, const void* d) { if (glTexCoordPointer_hf) glTexCoordPointer_hf(a, b, c, d); }
__SF void glTexEnvf(GLenum a, GLenum b, GLfloat c) { if (glTexEnvf_hf) glTexEnvf_hf(a, b, c); }
__SF void glTexEnvi(GLenum a, GLenum b, GLint c) { if (glTexEnvi_hf) glTexEnvi_hf(a, b, c); }
__SF void glUniform1f(GLint a, GLfloat b) { if (glUniform1f_hf) glUniform1f_hf(a, b); }
__SF void glUniform1i(GLint a, GLint b) { if (glUniform1i_hf) glUniform1i_hf(a, b); }
__SF void glUniform2f(GLint a, GLfloat b, GLfloat c) { if (glUniform2f_hf) glUniform2f_hf(a, b, c); }
__SF void glUniform3f(GLint a, GLfloat b, GLfloat c, GLfloat d) { if (glUniform3f_hf) glUniform3f_hf(a, b, c, d); }
__SF void glUniform3fv(GLint a, GLsizei b, const GLfloat* c) { if (glUniform3fv_hf) glUniform3fv_hf(a, b, c); }
__SF void glUniform4f(GLint a, GLfloat b, GLfloat c, GLfloat d, GLfloat e) { if (glUniform4f_hf) glUniform4f_hf(a, b, c, d, e); }
__SF void glUniform4fv(GLint a, GLsizei b, const GLfloat* c) { if (glUniform4fv_hf) glUniform4fv_hf(a, b, c); }
__SF void glUniformMatrix3fv(GLint a, GLsizei b, GLboolean c, const GLfloat* d) { if (glUniformMatrix3fv_hf) glUniformMatrix3fv_hf(a, b, c, d); }
__SF void glUniformMatrix4fv(GLint a, GLsizei b, GLboolean c, const GLfloat* d) { if (glUniformMatrix4fv_hf) glUniformMatrix4fv_hf(a, b, c, d); }
__SF void glUseProgram(GLuint a) { if (gt()) dprintf(2, "[gl] useProg %u\n", a); if (gt() && a) dprintf(2, "[gl] useProg %u\n", a); if (glUseProgram_hf) glUseProgram_hf(a); }
__SF void glVertexAttribPointer(GLuint a, GLint b, GLenum c, GLboolean d, GLsizei e, const void* f) { if (gt()) dprintf(2, "[gl] attribPtr %u sz=%d ty=%x st=%d\n", a, b, c, e); if (glVertexAttribPointer_hf) glVertexAttribPointer_hf(a, b, c, d, e, f); }
__SF void glVertexPointer(GLint a, GLenum b, GLsizei c, const void* d) { if (gt()) dprintf(2, "[gl] vertexPointer %d %x %d\n", a, b, c); if (glVertexPointer_hf) glVertexPointer_hf(a, b, c, d); }
__SF void glViewport(GLint a, GLint b, GLsizei c, GLsizei d) { if (gt()) dprintf(2, "[gl] viewport %d %d %dx%d\n", a, b, c, d); if (glViewport_hf) glViewport_hf(a, b, c, d); }
