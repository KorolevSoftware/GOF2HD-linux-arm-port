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
#define GOF_R2_RAW_BUTTON       11

#define GOF_TOUCH_FIFO_PATH     "/tmp/gof2hd_touch"

#define GOF_ENV_VERBOSE_JNI     "GOF_VERBOSE_JNI"
#define GOF_ENV_GDB             "GOF_GDB"
#define GOF_ENV_GYRO            "GOF_GYRO"

#endif
