/*
 * External touch injection channel.
 *
 * The FIFO is a debug/input adapter. It converts complete text records into
 * host touch events; it does not know anything about JNI or the game engine.
 */
#ifndef GOF2HD_TOUCH_FIFO_H
#define GOF2HD_TOUCH_FIFO_H

/* Android MotionEvent action values used by handleTouchEvent. */
typedef enum HostTouchAction {
    HOST_TOUCH_ACTION_DOWN = 0,
    HOST_TOUCH_ACTION_UP = 1,
    HOST_TOUCH_ACTION_MOVE = 2,
    HOST_TOUCH_ACTION_CANCEL = 3
} HostTouchAction;

typedef struct HostTouchEvent {
    int pid;
    HostTouchAction action;
    int x;
    int y;
} HostTouchEvent;

typedef void (*HostTouchEventFn)(const HostTouchEvent* event, void* context);

typedef struct TouchFifo {
    int fd;
    char pending[4096];
    unsigned int pending_len;
} TouchFifo;

int touch_fifo_open(TouchFifo* fifo);
void touch_fifo_pump(TouchFifo* fifo, HostTouchEventFn fn, void* context);
void touch_fifo_close(TouchFifo* fifo);

#endif
