/*
 * wrap_overlay.h — GL overlay + wrapper input state API for the GOF2HD host.
 */
#ifndef WRAP_OVERLAY_H
#define WRAP_OVERLAY_H

/* Input mode: in cursor mode the input vector moves the on-screen reticle,
 * in gyro mode it feeds the engine accelerometer instead (and the cursor
 * stays where it is — no point moving it). */
typedef enum {
    WRAW_MODE_CURSOR = 0,
    WRAW_MODE_GYRO   = 1,
} WrawMode;

/* Logical buttons understood by the wrapper state.  gof2hd.c is the input
 * backend: it maps SDL/raw device events onto these. */
typedef enum {
    WRAW_BTN_A,        /* tap at the cursor position (touch pid 722) */
    WRAW_BTN_B,        /* BackButtonPressed */
    WRAW_BTN_R2,       /* fire (touch pid 723, fixed fire zone) */
    WRAW_BTN_START,    /* toggle gyro mode */
    WRAW_BTN_COUNT,
} WrawButton;

/* 1 if GOF_SHOW_CURSOR is set (cursor reticle enabled). */
int overlay_enabled(void);

/* One-time setup with the game resolution (used to build the ortho
 * projection).  Safe to call only after the GL context is current. */
int overlay_init(int width, int height);

/* ---- input (values arrive already prepared: deadzone applied, stick and
 *      D-pad combined and normalized to [-1,1] by gof2hd.c) ---- */

/* Button edge event.  WRAW_BTN_START toggles the input mode internally. */
void overlay_input_button(WrawButton btn, int down);

/* Per-frame input vector.  In cursor mode it moves the reticle (speed
 * proportional to deflection); in gyro mode the cursor is left alone and
 * the vector feeds overlay_get_gyro(). */
void overlay_input_vector(float nx, float ny);

/* ---- state reads used to drive the engine ---- */

/* Current input mode. */
WrawMode overlay_get_mode(void);

/* Current cursor (virtual finger) position, clamped to the window. */
void overlay_get_cursor(int* x, int* y);

/* Current held state (0/1) of a button. */
int overlay_get_btn(WrawButton btn);

/* Engine accelerometer values to pass to handleAccelerometer (ready
 * mapping): gyro mode ax=0, ay=-nx, az=1+ny; otherwise neutral (0,0,1). */
void overlay_get_gyro(float* ax, float* ay, float* az);

/* Force the input mode (e.g. GOF_GYRO=1 at startup). */
void overlay_set_mode(WrawMode mode);

/* Draw the cursor reticle from the wrapper state; called every frame
 * right before the swap.  No-op unless enabled + init'd. */
void overlay_draw(void);

#endif