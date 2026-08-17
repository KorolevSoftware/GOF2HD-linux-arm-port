/*
 * gputest.c — проверить теорию: возвращает ли драйвер mali GPU-память
 * удалённых текстур.
 *
 * Создаёт окно через SDL2 + GLES2 (через наш мост libGLESv2.so, как хост),
 * циклически создаёт и удаляет текстуры заданного размера и после каждой
 * пачки меряет /proc/self/maps суммарный размер маппингов /dev/mali0.
 *
 * Если драйвер возвращает память — mali0 после create+delete остаётся ~базовым.
 * Если НЕ возвращает (накопление) — mali0 растёт, хотя живых текстур нет.
 *
 * Сборка на устройстве (в port/run-native):
 *   arm-linux-gnueabihf-gcc -O2 -o /tmp/gputest /root/gof2hd/port/tools/gputest.c \
 *       ./libGLESv2.so -L/usr/lib32 -lSDL2main -lSDL2 -ldl -lm
 *
 * Запуск (LD_LIBRARY_PATH=.):
 *   LD_LIBRARY_PATH=. /tmp/gputest [N] [size] [keep]
 *     N    — сколько текстур создать (default 200)
 *     size — сторона текстуры RGBA (default 1024 -> 4 МБ каждая)
 *     keep — 1 = НЕ удалять (для сравнения), 0 = удалять после создания
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <SDL2/SDL.h>

typedef unsigned int  GLenum;
typedef unsigned int  GLuint;
typedef int           GLint;
typedef int           GLsizei;
typedef long          GLsizeiptr;
typedef unsigned char GLboolean;
typedef float         GLfloat;

#define GL_TEXTURE_2D        0x0DE1
#define GL_FLOAT             0x1406

/* GLES2 entry points из нашего моста (softfp pcs("aapcs") — как в wrap_overlay.c).
 * Здесь нет float-аргументов, поэтому ABI совпадает с hardfp. */
#define GLF(name, ret, ...) __attribute__((pcs("aapcs"))) ret name(__VA_ARGS__)
GLF(glGenTextures,      void, GLsizei, GLuint*);
GLF(glBindTexture,      void, GLenum,  GLuint);
GLF(glTexImage2D,       void, GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*);
GLF(glCompressedTexImage2D, void, GLenum, GLint, GLenum, GLsizei, GLsizei, GLint, GLsizei, const void*);
GLF(glDeleteTextures,   void, GLsizei, const GLuint*);
GLF(glTexParameteri,    void, GLenum,  GLenum, GLint);
GLF(glGenBuffers,       void, GLsizei, GLuint*);
GLF(glBindBuffer,       void, GLenum,  GLuint);
GLF(glBufferData,       void, GLenum, GLsizeiptr, const void*, GLenum);
GLF(glDeleteBuffers,    void, GLsizei, const GLuint*);
GLF(glCreateShader,     GLuint, GLenum);
GLF(glShaderSource,     void, GLuint, GLsizei, const char* const*, const GLint*);
GLF(glCompileShader,    void, GLuint);
GLF(glCreateProgram,    GLuint, void);
GLF(glAttachShader,     void, GLuint, GLuint);
GLF(glLinkProgram,      void, GLuint);
GLF(glDeleteShader,     void, GLuint);
GLF(glUseProgram,       void, GLuint);
GLF(glGetUniformLocation, GLint, GLuint, const char*);
GLF(glGetAttribLocation,  GLint, GLuint, const char*);
GLF(glUniform1i,        void, GLint, GLint);
GLF(glActiveTexture,    void, GLenum);
GLF(glVertexAttribPointer, void, GLuint, GLint, GLenum, GLboolean, GLsizei, const void*);
GLF(glEnableVertexAttribArray, void, GLuint);
GLF(glDisableVertexAttribArray, void, GLuint);
GLF(glDrawArrays,       void, GLenum, GLint, GLsizei);
GLF(glFinish,           void, void);
GLF(glFlush,            void, void);
#undef GLF

#define GL_ARRAY_BUFFER 0x8892
#define GL_TRIANGLE_STRIP 0x0005
#define GL_COLOR_BUFFER_BIT 0x4000
#define GL_TEXTURE0 0x84C0

static const char RVS[] =
    "attribute vec2 aP; attribute vec2 aT; varying vec2 vT;\n"
    "void main(){ vT=aT; gl_Position=vec4(aP,0.0,1.0); }\n";
static const char RFS[] =
    "precision mediump float; varying vec2 vT; uniform sampler2D uTex;\n"
    "void main(){ gl_FragColor=texture2D(uTex,vT); }\n";

static GLuint g_prog, g_aP, g_aT, g_uTex;

static void render_init(void) {
    const char* vsrc = RVS;
    const char* fsrc = RFS;
    GLuint vs = glCreateShader(0x8B31); /* GL_VERTEX_SHADER */
    GLuint fs = glCreateShader(0x8B30); /* GL_FRAGMENT_SHADER */
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
    g_aT = (GLuint)glGetAttribLocation(g_prog, "aT");
    g_uTex = (GLuint)glGetUniformLocation(g_prog, "uTex");
    fprintf(stderr, "[gputest] shader prog=%u aP=%d aT=%d uTex=%d\n",
            g_prog, (int)g_aP, (int)g_aT, (int)g_uTex);
}

/* draw one textured quad using the given texture */
static void render_quad(GLuint tex) {
    static const GLfloat pos[8]  = { -1,-1, 1,-1, -1,1, 1,1 };
    static const GLfloat uv[8]   = { 0,0, 1,0, 0,1, 1,1 };
    if (!g_prog || g_uTex == 0xFFFFFFFFu || g_aP == 0xFFFFFFFFu) return;
    glUseProgram(g_prog);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex);
    glUniform1i((GLint)g_uTex, 0);
    glEnableVertexAttribArray(g_aP);
    glEnableVertexAttribArray(g_aT);
    glVertexAttribPointer(g_aP, 2, GL_FLOAT, 0, 0, pos);
    glVertexAttribPointer(g_aT, 2, GL_FLOAT, 0, 0, uv);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glDisableVertexAttribArray(g_aP);
    glDisableVertexAttribArray(g_aT);
}

typedef unsigned int GLbitfield;
#define GL_RGBA              0x1908
#define GL_UNSIGNED_BYTE     0x1401
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_NEAREST           0x2600
#define GL_ETC1_RGB8_OES     0x8D64

static long mali0_total_kb(void) {
    FILE* f = fopen("/proc/self/maps", "r");
    if (!f) return -1;
    char line[256];
    unsigned long long total = 0;
    while (fgets(line, sizeof(line), f)) {
        if (strstr(line, "/dev/mali0")) {
            unsigned long long a, b;
            if (sscanf(line, "%llx-%llx", &a, &b) == 2) total += b - a;
        }
    }
    fclose(f);
    return (long)(total / 1024);
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
    int ntex = argc > 1 ? atoi(argv[1]) : 200;
    int size = argc > 2 ? atoi(argv[2]) : 1024;
    int keep = argc > 3 ? atoi(argv[3]) : 0;
    int vary = argc > 4 ? atoi(argv[4]) : 0;   /* 1 = циклировать размеры 128..2048 */
    int etc  = argc > 5 ? atoi(argv[5]) : 0;   /* 1 = сжатые ETC1-текстуры */
    int bufs = argc > 6 ? atoi(argv[6]) : 0;   /* 1 = VBO (glBufferData) вместо текстур */
    int respec = argc > 7 ? atoi(argv[7]) : 0; /* 1 = один и тот же буфер, glBufferData много раз */
    int mips = argc > 8 ? atoi(argv[8]) : 0;   /* 1 = загружать полные mip-цепочки (как движок) */
    int render = argc > 9 ? atoi(argv[9]) : 0; /* 1 = рисовать текстурированный квад перед удалением (как движок) */
    int live = argc > 10 ? atoi(argv[10]) : 0; /* сколько текстур держать живыми одновременно (FIFO), как движок ~60 */
    int finish = argc > 11 ? atoi(argv[11]) : 0; /* 1 = звать glFinish() каждый 25 циклов (принудительно освобождать) */
    int interval = argc > 12 ? atoi(argv[12]) : 25; /* печатать/мерить каждые N циклов */

    if (SDL_Init(SDL_INIT_VIDEO) != 0) { fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()); return 1; }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_Window* win = SDL_CreateWindow("gputest", 0, 0, 320, 240,
                                       SDL_WINDOW_OPENGL | SDL_WINDOW_FULLSCREEN_DESKTOP);
    if (!win) { fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError()); return 1; }
    SDL_GLContext ctx = SDL_GL_CreateContext(win);
    if (!ctx) { fprintf(stderr, "SDL_GL_CreateContext: %s\n", SDL_GetError()); return 1; }

    printf("gputest: N=%d size=%d keep=%d\n", ntex, size, keep);
    printf("baseline       mali0: %8ld KB   VmRSS: %8ld KB\n", mali0_total_kb(), rss_kb());

    int maxsz = size > 2048 ? size : 2048;   /* vary-цикл доходит до 2048 */
    unsigned char* data = malloc((size_t)maxsz * maxsz * 4);
    if (!data) { fprintf(stderr, "malloc\n"); return 1; }
    memset(data, 0x80, (size_t)maxsz * maxsz * 4);

    static GLuint g_live[512];
    int g_live_n = 0;

    if (respec) {
        /* Re-spec одного и того же буфера много раз (как движок каждый кадр).
         * Каждый glBufferData с данными = новая аллокация GPU; смотрим, растёт ли mali0. */
        GLuint b = 0;
        glGenBuffers(1, &b);
        glBindBuffer(GL_ARRAY_BUFFER, b);
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)size * size * 4, data, 0x88E8);
        for (int i = 0; i < ntex; i++) {
            glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)size * size * 4, data, 0x88E8);
            if (i % interval == 0 || i == ntex - 1) {
                printf("respec %4d mali0: %8ld KB   VmRSS: %8ld KB\n",
                       i + 1, mali0_total_kb(), rss_kb());
                fflush(stdout);
                SDL_GL_SwapWindow(win);
                usleep(20000);
            }
        }
        printf("final          mali0: %8ld KB   VmRSS: %8ld KB\n", mali0_total_kb(), rss_kb());
        free(data);
        SDL_GL_DeleteContext(ctx);
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 0;
    }

    if (render) render_init();
    for (int i = 0; i < ntex; i++) {
        int sz = size;
        if (vary) {
            static const int sizes[] = { 128, 256, 512, 1024, 2048, 1024, 512, 256 };
            sz = sizes[i % 8];
        }
        GLuint t = 0;
        if (bufs) {
            glGenBuffers(1, &t);
            glBindBuffer(GL_ARRAY_BUFFER, t);
            glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)sz * sz * 4, data, 0x88E8 /*GL_DYNAMIC_DRAW*/);
            if (!keep) glDeleteBuffers(1, &t);
        } else {
            glGenTextures(1, &t);
            glBindTexture(GL_TEXTURE_2D, t);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            if (etc) {
                int esz = (sz * sz) / 2;
                glCompressedTexImage2D(GL_TEXTURE_2D, 0, GL_ETC1_RGB8_OES, sz, sz, 0, esz, data);
                if (mips) {
                    int w = sz / 2, h = sz / 2;
                    for (int lvl = 1; w >= 1 && lvl < 12; lvl++, w /= 2, h /= 2) {
                        if (w < 1) w = 1;
                        if (h < 1) h = 1;
                        int ms = (w * h) / 2; if (ms < 8) ms = 8;
                        glCompressedTexImage2D(GL_TEXTURE_2D, lvl, GL_ETC1_RGB8_OES, w, h, 0, ms, data);
                    }
                }
            } else {
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, sz, sz, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
                if (mips) {
                    int w = sz / 2, h = sz / 2;
                    for (int lvl = 1; w >= 1 && lvl < 12; lvl++, w /= 2, h /= 2) {
                        if (w < 1) w = 1;
                        if (h < 1) h = 1;
                        glTexImage2D(GL_TEXTURE_2D, lvl, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
                    }
                }
            }
            if (render && g_prog) {
                render_quad(t);
                SDL_GL_SwapWindow(win);   /* показать кадр -> GPU реально работает с текстурой */
            }
            if (live > 0) {
                /* держать до `live` текстур живыми (FIFO), как движок */
                if (g_live_n >= live && live < 512) {
                    glDeleteTextures(1, &g_live[0]);
                    memmove(&g_live[0], &g_live[1], (size_t)(g_live_n - 1) * sizeof(GLuint));
                    g_live_n--;
                }
                if (g_live_n < 512) g_live[g_live_n++] = t;
            } else if (!keep) {
                glDeleteTextures(1, &t);
            }
        }
        if (i % interval == 0 || i == ntex - 1) {
            if (finish) glFinish();   /* принудительно дождаться GPU и освободить удалённое */
            printf("iter %4d sz=%5d mali0: %8ld KB   VmRSS: %8ld KB%s\n",
                   i + 1, sz, mali0_total_kb(), rss_kb(), keep ? "  (kept)" : "  (deleted)");
            fflush(stdout);
            SDL_GL_SwapWindow(win);   /* дать драйверу шанс обработать */
            usleep(20000);
        }
    }

    printf("final          mali0: %8ld KB   VmRSS: %8ld KB\n", mali0_total_kb(), rss_kb());
    printf("done%s\n", keep ? " (textures kept; will now delete to test release)" : "");

    if (keep) {
        /* зачистить: удалить всё и посмотреть, вернётся ли mali0 к базовому */
        for (int i = 0; i < ntex; i++) {
            GLuint t = (GLuint)(i + 1);
            glDeleteTextures(1, &t);
        }
        SDL_GL_SwapWindow(win);
        usleep(200000);
        printf("after delete-all mali0: %8ld KB   VmRSS: %8ld KB\n", mali0_total_kb(), rss_kb());
    }

    free(data);
    SDL_GL_DeleteContext(ctx);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
