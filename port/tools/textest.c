/*
 * textest.c — texture lifecycle reproducer (чистый churn текстур, вне игры).
 *
 * Проверяет: воспроизводит ли ПРОСТОЙ цикл
 *   glGenTextures -> glBindTexture -> glTexImage2D -> glTexParameteri -> glDeleteTextures
 * рост mali0 mappings / VmRSS / RssFile (то, что растёт у игры до OOM).
 *
 * Варианты (последний аргумент):
 *   0 = create -> upload -> delete
 *   1 = create -> upload -> glFinish -> delete
 *   2 = create -> upload -> delete -> glFinish
 *   3 = create -> upload -> delete -> SwapBuffers
 *   4 = create -> upload -> delete -> glFinish -> SwapBuffers
 *
 * Сборка на устройстве (в port/run-native):
 *   arm-linux-gnueabihf-gcc -O2 -o /tmp/textest /root/gof2hd/port/tools/textest.c \
 *       ./libGLESv2.so -L/usr/lib32 -lSDL2main -lSDL2 -ldl -lm
 * Запуск: LD_LIBRARY_PATH=.:/usr/lib32 /tmp/textest [N] [size] [variant] [interval]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL2/SDL.h>

typedef unsigned int  GLenum;
typedef unsigned int  GLuint;
typedef int           GLint;
typedef int           GLsizei;

#define GLF(name, ret, ...) __attribute__((pcs("aapcs"))) ret name(__VA_ARGS__)
GLF(glGenTextures,      void, GLsizei, GLuint*);
GLF(glBindTexture,      void, GLenum,  GLuint);
GLF(glTexImage2D,       void, GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*);
GLF(glTexParameteri,    void, GLenum, GLenum, GLint);
GLF(glDeleteTextures,   void, GLsizei, const GLuint*);
GLF(glFinish,           void, void);
#undef GLF

#define GL_TEXTURE_2D 0x0DE1
#define GL_RGBA       0x1908
#define GL_UNSIGNED_BYTE 0x1401
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_NEAREST     0x2600

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
static void get_rss(long* rss, long* rssfile) {
    FILE* f = fopen("/proc/self/status", "r");
    if (!f) return;
    char line[128];
    while (fgets(line, sizeof(line), f)) {
        if (sscanf(line, "VmRSS: %ld", rss) == 1) continue;
        if (sscanf(line, "RssFile: %ld", rssfile) == 1) continue;
    }
    fclose(f);
}

int main(int argc, char** argv) {
    long n     = argc > 1 ? atol(argv[1]) : 100000;
    long size  = argc > 2 ? atol(argv[2]) : 512;
    int  var   = argc > 3 ? atoi(argv[3]) : 0;
    long interval = argc > 4 ? atol(argv[4]) : 10000;

    if (SDL_Init(SDL_INIT_VIDEO) != 0) { fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()); return 1; }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_Window* win = SDL_CreateWindow("textest", 0, 0, 320, 240,
                                       SDL_WINDOW_OPENGL | SDL_WINDOW_FULLSCREEN_DESKTOP);
    if (!win) { fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError()); return 1; }
    SDL_GLContext ctx = SDL_GL_CreateContext(win);
    if (!ctx) { fprintf(stderr, "SDL_GL_CreateContext: %s\n", SDL_GetError()); return 1; }

    long rss = 0, rssfile = 0;
    printf("textest: N=%ld size=%ld variant=%d interval=%ld\n", n, size, var, interval);
    get_rss(&rss, &rssfile);
    printf("start  mali0 maps=%ld (%ld KB)  VmRSS=%ld  RssFile=%ld\n",
           mali0_count(), mali0_kb(), rss, rssfile);

    unsigned char* data = malloc((size_t)size * size * 4);
    if (!data) return 1;
    memset(data, 0x80, (size_t)size * size * 4);

    for (long i = 0; i < n; i++) {
        GLuint t = 0;
        glGenTextures(1, &t);
        glBindTexture(GL_TEXTURE_2D, t);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, (GLsizei)size, (GLsizei)size, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, data);
        switch (var) {
        case 1: glFinish(); break;
        case 2: glDeleteTextures(1, &t); glFinish(); continue;
        case 3: glDeleteTextures(1, &t); SDL_GL_SwapWindow(win); continue;
        case 4: glDeleteTextures(1, &t); glFinish(); SDL_GL_SwapWindow(win); continue;
        default: break;
        }
        glDeleteTextures(1, &t);

        if (i % interval == 0 || i == n - 1) {
            get_rss(&rss, &rssfile);
            printf("iter %ld  mali0 maps=%ld (%ld KB)  VmRSS=%ld  RssFile=%ld\n",
                   i + 1, mali0_count(), mali0_kb(), rss, rssfile);
            fflush(stdout);
        }
    }

    free(data);
    SDL_GL_DeleteContext(ctx);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
