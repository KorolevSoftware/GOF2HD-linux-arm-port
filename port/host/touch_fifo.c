/*
 * touch_fifo.c — line-buffered external touch input.
 */
#include "touch_fifo.h"
#include "host_config.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

int touch_fifo_open(TouchFifo* fifo) {
    if (!fifo) return -1;
    memset(fifo, 0, sizeof(*fifo));
    fifo->fd = -1;

    if (mkfifo(GOF_TOUCH_FIFO_PATH, 0644) != 0 && errno != EEXIST) {
        fprintf(stderr, "[host] touch fifo create failed: %s\n", strerror(errno));
        return -1;
    }

    fifo->fd = open(GOF_TOUCH_FIFO_PATH, O_RDONLY | O_NONBLOCK);
    if (fifo->fd < 0) {
        fprintf(stderr, "[host] touch fifo open failed: %s\n", strerror(errno));
        return -1;
    }
    return 0;
}

static void discard_until_newline(TouchFifo* fifo) {
    char sink[128];
    ssize_t n;
    do {
        n = read(fifo->fd, sink, sizeof(sink));
    } while (n > 0 && memchr(sink, '\n', (size_t)n) == NULL);
}

void touch_fifo_pump(TouchFifo* fifo, HostTouchEventFn fn, void* context) {
    char chunk[1024];
    ssize_t n;

    if (!fifo || fifo->fd < 0 || !fn) return;

    for (;;) {
        n = read(fifo->fd, chunk, sizeof(chunk));
        if (n <= 0) {
            if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK)
                fprintf(stderr, "[host] touch fifo read failed: %s\n", strerror(errno));
            break;
        }

        if ((unsigned int)n > sizeof(fifo->pending) - fifo->pending_len) {
            fprintf(stderr, "[host] touch fifo line buffer full; discarding input\n");
            fifo->pending_len = 0;
            discard_until_newline(fifo);
            continue;
        }

        memcpy(fifo->pending + fifo->pending_len, chunk, (size_t)n);
        fifo->pending_len += (unsigned int)n;

        for (;;) {
            char* nl = memchr(fifo->pending, '\n', fifo->pending_len);
            HostTouchEvent event;
            int action;
            unsigned int line_len;

            if (!nl) break;
            line_len = (unsigned int)(nl - fifo->pending);
            *nl = '\0';
            if (sscanf(fifo->pending, "%d %d %d %d",
                       &event.pid, &action, &event.x, &event.y) == 4) {
                event.action = (HostTouchAction)action;
                fn(&event, context);
            }

            line_len++;
            memmove(fifo->pending, fifo->pending + line_len,
                    fifo->pending_len - line_len);
            fifo->pending_len -= line_len;
        }
    }
}

void touch_fifo_close(TouchFifo* fifo) {
    if (!fifo) return;
    if (fifo->fd >= 0) close(fifo->fd);
    fifo->fd = -1;
    fifo->pending_len = 0;
}
