/*
 * Host-side configuration.
 *
 * These values describe the Linux device/input adapter. Android game
 * protocol details remain in wrap_overlay until a second game needs a
 * configurable virtual-input contract.
 */
#ifndef GOF2HD_HOST_CONFIG_H
#define GOF2HD_HOST_CONFIG_H

#define GOF_FRAME_PERIOD_MS     33

#define GOF_GAMEPAD_NAME        "ANBERNIC"
#define GOF_AXIS_DEADZONE       4000
/* Linux joydev button map for ANBERNIC-keys:
 * b10=BTN_SELECT (the physical L2), b11=BTN_START (the physical R2 on
 * this device).  b13 is KEY_GOTO and is not treated as a gameplay button. */
#define GOF_L2_RAW_BUTTON       10
#define GOF_R2_RAW_BUTTON       11
#define GOF_B_RAW_BUTTON        1
#define GOF_Y_RAW_BUTTON        2
#define GOF_L2_CLICK_X          541
#define GOF_L2_CLICK_Y          446
#define GOF_B_CLICK_X           35
#define GOF_B_CLICK_Y           345
#define GOF_Y_CLICK_X           37
#define GOF_Y_CLICK_Y           287

#define GOF_TOUCH_FIFO_PATH     "/tmp/gof2hd_touch"

#define GOF_ENV_VERBOSE_JNI     "GOF_VERBOSE_JNI"
#define GOF_ENV_GDB             "GOF_GDB"
#define GOF_ENV_GYRO            "GOF_GYRO"

#endif
