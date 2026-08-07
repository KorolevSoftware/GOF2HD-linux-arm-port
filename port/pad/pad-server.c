#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define UDP_PORT 4444
#define DEV_NAME "GOF2HD Virtual Gamepad"

static int uin = -1;

static void emit(int type, int code, int val) {
    struct input_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = type; ev.code = code; ev.value = val;
    if (write(uin, &ev, sizeof(ev)) < 0) {
        fprintf(stderr, "[pad] write err %s\n", strerror(errno));
    }
}
static void syn(void) {
    struct input_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = EV_SYN; ev.code = SYN_REPORT; ev.value = 0;
    write(uin, &ev, sizeof(ev));
}

int main(void) {
    uin = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (uin < 0) { perror("open uinput"); return 1; }

    struct uinput_setup us;
    memset(&us, 0, sizeof(us));
    strcpy(us.name, DEV_NAME);
    us.id.bustype = BUS_USB; us.id.vendor = 0x1234; us.id.product = 0x5678; us.id.version = 1;

    ioctl(uin, UI_SET_EVBIT, EV_KEY);
    ioctl(uin, UI_SET_EVBIT, EV_ABS);
    static const int keys[] = {
        BTN_A, BTN_B, BTN_X, BTN_Y,
        BTN_DPAD_UP, BTN_DPAD_DOWN, BTN_DPAD_LEFT, BTN_DPAD_RIGHT,
        BTN_START, BTN_SELECT, BTN_TL, BTN_TR
    };
    for (unsigned i = 0; i < sizeof(keys)/sizeof(keys[0]); i++)
        ioctl(uin, UI_SET_KEYBIT, keys[i]);
    ioctl(uin, UI_SET_ABSBIT, ABS_X);
    ioctl(uin, UI_SET_ABSBIT, ABS_Y);
    ioctl(uin, UI_SET_ABSBIT, ABS_RX);
    ioctl(uin, UI_SET_ABSBIT, ABS_RY);
    struct uinput_abs_setup ax = { .code = ABS_X,
        .absinfo = { .minimum = -32768, .maximum = 32767, .fuzz = 0, .flat = 0 } };
    ioctl(uin, UI_ABS_SETUP, &ax); ax.code = ABS_Y; ioctl(uin, UI_ABS_SETUP, &ax);
    ax.code = ABS_RX; ioctl(uin, UI_ABS_SETUP, &ax); ax.code = ABS_RY; ioctl(uin, UI_ABS_SETUP, &ax);

    if (ioctl(uin, UI_DEV_SETUP, &us) < 0) { perror("UI_DEV_SETUP"); return 1; }
    if (ioctl(uin, UI_DEV_CREATE) < 0) { perror("UI_DEV_CREATE"); return 1; }
    printf("[pad] virtual gamepad created, waiting UDP %d\n", UDP_PORT);

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) { perror("socket"); return 1; }
    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET; a.sin_port = htons(UDP_PORT); a.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(sock, (struct sockaddr*)&a, sizeof(a)) < 0) { perror("bind"); return 1; }

    char buf[256];
    for (;;) {
        ssize_t n = recvfrom(sock, buf, sizeof(buf)-1, 0, NULL, NULL);
        if (n <= 0) { usleep(10000); continue; }
        buf[n] = 0;
        int code = 0, val = 0;
        if (sscanf(buf, "KEY %d %d", &code, &val) == 2) {
            emit(EV_KEY, code, val); syn();
        } else if (sscanf(buf, "ABS %d %d", &code, &val) == 2) {
            emit(EV_ABS, code, val); syn();
        }
    }
    return 0;
}
