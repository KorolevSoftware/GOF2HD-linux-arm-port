/*
 * gof2hd-viewer — host-side (x86_64) SDL2 window for the ARM software GLES2.
 *
 * Reads RGBA frames from /tmp/gof2hd_fb (written by gles-stub.c) and shows
 * them.  Mouse events are forwarded to the game as touches via the FIFO
 * /tmp/gof2hd_touch as:  722 <action> <x> <y>\n
 *   action: 0 = down, 1 = up, 2 = move
 * The ARM host (gof2hd.c) reads that FIFO and calls handleTouchEvent.
 */
#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>

#define FB_PATH   "/tmp/gof2hd_fb"
#define TOUCH_FIFO "/tmp/gof2hd_touch"

static int fd_fifo = -1;

static void fifo_send(int action, int x, int y) {
    if (fd_fifo < 0) return;
    char buf[64];
    int n = snprintf(buf, sizeof(buf), "722 %d %d %d\n", action, x, y);
    ssize_t r = write(fd_fifo, buf, n);
    (void)r;
}

int main(int argc, char** argv) {
    int win_w = 640, win_h = 480;
    if (argc >= 3) { win_w = atoi(argv[1]); win_h = atoi(argv[2]); }

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    SDL_Window* win = SDL_CreateWindow("GOF2HD",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, win_w, win_h,
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!win) { fprintf(stderr, "window: %s\n", SDL_GetError()); return 1; }
    SDL_Renderer* ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    if (!ren) { fprintf(stderr, "renderer: %s\n", SDL_GetError()); return 1; }

    SDL_Texture* tex = NULL;
    int fb_w = 0, fb_h = 0;
    uint32_t last_mtime = 0;

    /* open FIFO for writing (create it); game must open it read side */
    mkfifo(TOUCH_FIFO, 0644);
    fd_fifo = open(TOUCH_FIFO, O_WRONLY | O_NONBLOCK);
    if (fd_fifo < 0) {
        /* game may not have started yet; retry later */
        fprintf(stderr, "[viewer] note: cannot open %s yet (%s)\n", TOUCH_FIFO, strerror(errno));
    }

    struct stat st;
    if (stat(FB_PATH, &st) == 0) last_mtime = (uint32_t)st.st_mtime;

    int running = 1;
    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            switch (ev.type) {
                case SDL_QUIT: running = 0; break;
                case SDL_MOUSEBUTTONDOWN: {
                    int w = 0, h = 0;
                    SDL_GetWindowSize(win, &w, &h);
                    int gx = fb_w ? ev.button.x * 640 / (w ? w : 1) : ev.button.x;
                    int gy = fb_h ? ev.button.y * 480 / (h ? h : 1) : ev.button.y;
                    fifo_send(0, gx, gy);
                    break;
                }
                case SDL_MOUSEBUTTONUP: {
                    int w = 0, h = 0;
                    SDL_GetWindowSize(win, &w, &h);
                    int gx = fb_w ? ev.button.x * 640 / (w ? w : 1) : ev.button.x;
                    int gy = fb_h ? ev.button.y * 480 / (h ? h : 1) : ev.button.y;
                    fifo_send(1, gx, gy);
                    break;
                }
                case SDL_MOUSEMOTION:
                    if (ev.motion.state & SDL_BUTTON_LMASK) {
                        int w = 0, h = 0;
                        SDL_GetWindowSize(win, &w, &h);
                        int gx = fb_w ? ev.motion.x * 640 / (w ? w : 1) : ev.motion.x;
                        int gy = fb_h ? ev.motion.y * 480 / (h ? h : 1) : ev.motion.y;
                        fifo_send(2, gx, gy);
                    }
                    break;
                default: break;
            }
        }

        if (fd_fifo < 0) {
            fd_fifo = open(TOUCH_FIFO, O_WRONLY | O_NONBLOCK);
        }

        /* read new frame if changed */
        if (stat(FB_PATH, &st) == 0) {
            uint32_t mt = (uint32_t)st.st_mtime;
            if (mt != last_mtime) {
                last_mtime = mt;
                FILE* f = fopen(FB_PATH, "rb");
                if (f) {
                    uint8_t hdr[8];
                    if (fread(hdr, 1, 8, f) == 8) {
                        int w = hdr[0] | (hdr[1] << 8) | (hdr[2] << 16) | (hdr[3] << 24);
                        int h = hdr[4] | (hdr[5] << 8) | (hdr[6] << 16) | (hdr[7] << 24);
                        if (w > 0 && h > 0 && w <= 4096 && h <= 4096) {
                            uint8_t* px = malloc(w * h * 4);
                            if (fread(px, 1, w * h * 4, f) == (size_t)(w * h * 4)) {
                                if (!tex || fb_w != w || fb_h != h) {
                                    if (tex) SDL_DestroyTexture(tex);
                                    tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888,
                                                            SDL_TEXTUREACCESS_STREAMING, w, h);
                                    fb_w = w; fb_h = h;
                                }
                                if (tex) {
                                    /* convert RGBA -> ARGB for SDL */
                                    uint32_t* argb = malloc(w * h * 4);
                                    for (int i = 0; i < w * h; i++) {
                                        uint8_t r = px[4*i+0], g = px[4*i+1],
                                                b = px[4*i+2], a = px[4*i+3];
                                        argb[i] = (a << 24) | (r << 16) | (g << 8) | b;
                                    }
                                    SDL_UpdateTexture(tex, NULL, argb, w * 4);
                                    free(argb);
                                }
                            }
                            free(px);
                        }
                    }
                    fclose(f);
                }
            }
        }

        SDL_RenderClear(ren);
        if (tex) {
            int ww = 0, wh = 0;
            SDL_GetWindowSize(win, &ww, &wh);
            SDL_Rect dst = {0, 0, ww, wh};
            SDL_RenderCopy(ren, tex, NULL, &dst);
        }
        SDL_RenderPresent(ren);
        SDL_Delay(16);
    }

    if (tex) SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
