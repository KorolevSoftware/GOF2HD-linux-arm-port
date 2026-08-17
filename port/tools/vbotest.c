/*
 * vbotest.c — изолированный тест VBO: воспроизводит игру без запуска.
 *
 * Гипотеза (этап B): mali0 mappings ∝ created VBO (движок создаёт ~7000 VBO,
 * удаляет ~4300, но драйвер удерживает device-маппинги всех созданных).
 * Тест: циклически glGenBuffers + glBufferData + glDrawArrays (GPU реально
 * читает VBO) + glDeleteBuffers, и меряет /proc/self/maps /dev/mali0 (число и
 * суммарный размер маппингов) и VmRSS.
 *
 * Критерий: если mappings растут с created (а не live) — гипотеза подтверждена
 * без игры, и на этом тесте можно проверять фиксы драйвера/моста.
 *
 * Сборка на устройстве (в port/run-native):
 *   arm-linux-gnueabihf-gcc -O2 -o /tmp/vbotest /root/gof2hd/port/tools/vbotest.c \
 *       ./libGLESv2.so -L/usr/lib32 -lSDL2main -lSDL2 -ldl -lm
 * Запуск: LD_LIBRARY_PATH=.:/usr/lib32 /tmp/vbotest [N] [vbosize] [live] [interval]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL2/SDL.h>

typedef unsigned int  GLenum;
typedef unsigned int  GLuint;
typedef int           GLint;
typedef int           GLsizei;
typedef long          GLsizeiptr;
typedef float         GLfloat;
typedef unsigned char GLboolean;

#define GLF(name, ret, ...) __attribute__((pcs("aapcs"))) ret name(__VA_ARGS__)
GLF(glGenBuffers,        void, GLsizei, GLuint*);
GLF(glBindBuffer,        void, GLenum,  GLuint);
GLF(glBufferData,        void, GLenum, GLsizeiptr, const void*, GLenum);
GLF(glDeleteBuffers,     void, GLsizei, const GLuint*);
GLF(glCreateShader,      GLuint, GLenum);
GLF(glShaderSource,      void, GLuint, GLsizei, const char* const*, const GLint*);
GLF(glCompileShader,     void, GLuint);
GLF(glCreateProgram,     GLuint, void);
GLF(glAttachShader,      void, GLuint, GLuint);
GLF(glLinkProgram,       void, GLuint);
GLF(glDeleteShader,      void, GLuint);
GLF(glUseProgram,        void, GLuint);
GLF(glGetAttribLocation, GLint, GLuint, const char*);
GLF(glVertexAttribPointer, void, GLuint, GLint, GLenum, GLboolean, GLsizei, const void*);
GLF(glEnableVertexAttribArray, void, GLuint);
GLF(glDisableVertexAttribArray, void, GLuint);
GLF(glDrawArrays,        void, GLenum, GLint, GLsizei);
#undef GLF

#define GL_ARRAY_BUFFER 0x8892
#define GL_TRIANGLES    0x0004
#define GL_FLOAT        0x1406
#define GL_VERTEX_SHADER   0x8B31
#define GL_FRAGMENT_SHADER 0x8B30

static const char VS[] =
    "attribute vec2 aP;\n"
    "void main(){ gl_Position=vec4(aP,0.0,1.0); }\n";
static const char FS[] =
    "precision mediump float;\n"
    "void main(){ gl_FragColor=vec4(1.0,0.0,0.0,1.0); }\n";

static GLuint g_prog, g_aP;

static void shader_init(void) {
    const char* vsrc = VS;
    const char* fsrc = FS;
    GLuint vs = glCreateShader(GL_VERTEX_SHADER);
    GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(vs, 1, &vsrc, NULL);
    glShaderSource(fs, 1, &fsrc, NULL);
    glCompileShader(vs);
    glCompileShader(fs);
    g_prog = glCreateProgram();
    glAttachShader(g_prog, vs);
    glAttachShader(g_prog, fs);
    glLinkProgram(g_prog);
    glDeleteShader(vs);
    glDeleteShader(fs);
    g_aP = (GLuint)glGetAttribLocation(g_prog, "aP");
}

static long mali0_count(void) {
    FILE* f = fopen("/proc/self/maps", "r");
    if (!f) return -1;
    char line[256];
    long n = 0;
    while (fgets(line, sizeof(line), f))
        if (strstr(line, "/dev/mali0")) n++;
    fclose(f);
    return n;
}
static long mali0_kb(void) {
    FILE* f = fopen("/proc/self/maps", "r");
    if (!f) return -1;
    char line[256];
    unsigned long long t = 0;
    while (fgets(line, sizeof(line), f)) {
        if (strstr(line, "/dev/mali0")) {
            unsigned long long a, b;
            if (sscanf(line, "%llx-%llx", &a, &b) == 2) t += b - a;
        }
    }
    fclose(f);
    return (long)(t / 1024);
}
static long rss_kb(void) {
    FILE* f = fopen("/proc/self/status", "r");
    if (!f) return -1;
    char line[128];
    long v = -1;
    while (fgets(line, sizeof(line), f))
        if (sscanf(line, "VmRSS: %ld", &v) == 1) break;
    fclose(f);
    return v;
}

int main(int argc, char** argv) {
    long niter   = argc > 1 ? atol(argv[1]) : 20000;
    long vsize   = argc > 2 ? atol(argv[2]) : 8192;   /* bytes per VBO */
    long live    = argc > 3 ? atol(argv[3]) : 0;       /* keep this many alive (FIFO) */
    long interval= argc > 4 ? atol(argv[4]) : 1000;

    if (SDL_Init(SDL_INIT_VIDEO) != 0) { fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()); return 1; }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_Window* win = SDL_CreateWindow("vbotest", 0, 0, 320, 240,
                                       SDL_WINDOW_OPENGL | SDL_WINDOW_FULLSCREEN_DESKTOP);
    if (!win) { fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError()); return 1; }
    SDL_GLContext ctx = SDL_GL_CreateContext(win);
    if (!ctx) { fprintf(stderr, "SDL_GL_CreateContext: %s\n", SDL_GetError()); return 1; }
    shader_init();

    printf("vbotest: N=%ld vsize=%ld live=%ld interval=%ld\n", niter, vsize, live, interval);
    printf("start   mali0 maps=%ld  %ld KB   VmRSS %ld KB\n",
           mali0_count(), mali0_kb(), rss_kb());

    GLfloat* verts = malloc((size_t)vsize);
    if (!verts) return 1;
    memset(verts, 0, (size_t)vsize);

    static GLuint live_ids[4096];
    long live_n = 0;

    glUseProgram(g_prog);
    glEnableVertexAttribArray(g_aP);

    for (long i = 0; i < niter; i++) {
        GLuint id = 0;
        glGenBuffers(1, &id);
        glBindBuffer(GL_ARRAY_BUFFER, id);
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)vsize, verts, 0x88E8 /*GL_DYNAMIC_DRAW*/);
        /* GPU реально читает VBO: атрибут из буфера + draw */
        glVertexAttribPointer(g_aP, 2, GL_FLOAT, 0, 0, NULL);
        glDrawArrays(GL_TRIANGLES, 0, (GLsizei)(vsize / 8));

        if (live > 0) {
            if (live_n >= live && live_n > 0) {
                glDeleteBuffers(1, &live_ids[0]);
                memmove(&live_ids[0], &live_ids[1], (size_t)(live_n - 1) * sizeof(GLuint));
                live_n--;
            }
            if (live_n < 4096) live_ids[live_n++] = id;
        } else {
            glDeleteBuffers(1, &id);
        }

        if (i % interval == 0 || i == niter - 1) {
            printf("iter %ld created=%ld live=%ld mali0 maps=%ld (%ld KB) VmRSS=%ld KB\n",
                   i + 1, i + 1, live_n, mali0_count(), mali0_kb(), rss_kb());
            fflush(stdout);
            SDL_GL_SwapWindow(win);
        }
    }

    printf("done    mali0 maps=%ld  %ld KB   VmRSS %ld KB\n", mali0_count(), mali0_kb(), rss_kb());
    free(verts);
    SDL_GL_DeleteContext(ctx);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
