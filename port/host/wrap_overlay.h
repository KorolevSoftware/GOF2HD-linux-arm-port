/*
 * wrap_overlay.h — GL overlay API for the GOF2HD host.
 */
#ifndef WRAP_OVERLAY_H
#define WRAP_OVERLAY_H

/* 1 if GOF_SHOW_CURSOR is set (cursor reticle enabled). */
int overlay_enabled(void);

/* One-time setup with the game resolution (used to build the ortho
 * projection).  Safe to call only after the GL context is current. */
int overlay_init(int width, int height);

/* Draw the cursor reticle centred at screen pixel (x, y); called every
 * frame right before the swap.  No-op unless enabled + init'd. */
void overlay_draw(int x, int y);

#endif