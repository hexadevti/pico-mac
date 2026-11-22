/*
 * Copyright 2024 Matt Evans
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation files
 * (the "Software"), to deal in the Software without restriction,
 * including without limitation the rights to use, copy, modify, merge,
 * publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef UMAC_H
#define UMAC_H

#include "disc.h"
#include "via.h"
#include "machw.h"

/* pico-mac: the API below differs from upstream umac (see THIRD_PARTY.md):
 * - RAM size and screen size are chosen at run time, in umac_init;
 * - umac_run(cycles) replaces umac_loop(), and slices execution at VIA timer events;
 * - an RTC with PRAM, sound buffer access, a keyboard event queue.
 */

/* The screen size the ROM is patched for (rom_patch reads it): 512x342 is the real Mac. */
extern int umac_disp_w, umac_disp_h;
#define DISP_WIDTH      umac_disp_w
#define DISP_HEIGHT     umac_disp_h

/* The Mac Plus' clock */
#define UMAC_CPU_HZ     7833600

/* ram_size: a power of two from 512 KB to 4 MB; the buffer needs 4 bytes of slack past it. */
int     umac_init(void *ram_base, void *rom_base, uint32_t ram_size, disc_descr_t discs[DISC_NUM_DRIVES]);
/* Runs about this many CPU cycles. Returns 0, or 1 once the emulation has stopped on an error. */
int     umac_run(uint32_t cycles);
/* VIA timers count at the CPU clock / (10 * this): 2 runs the CPU twice as fast against them. */
void    umac_set_speed(int mult);
void    umac_reset(void);
/* Snapshots, between umac_run calls: io moves n bytes to the stream (save) or from it (load),
 * nonzero = failed. A load goes into a machine umac_init started with the same RAM size, screen
 * size, ROM and disks. 0 = done, -1 = I/O error (after a failed load the machine is unusable),
 * -2 = not a state for this machine (nothing was changed). */
int     umac_save_state(int (*io)(void *ctx, void *p, unsigned int n), void *ctx);
int     umac_load_state(int (*io)(void *ctx, void *p, unsigned int n), void *ctx);
void    umac_opt_disassemble(int enable);
void    umac_mouse(int deltax, int deltay, int button);
/* scancode: the Mac key code (keymap.h MKC_*), not yet shifted into the keyboard protocol */
void    umac_kbd_event(uint8_t scancode, int down);
/* 1 once the boot disk was ejected (the Mac's Shut Down or an eject); cleared by umac_reset */
int     umac_disc_was_ejected(void);

/* RTC: seconds since 1904 and the 20 bytes of parameter RAM. umac_1hz_event advances the clock. */
uint32_t umac_rtc_seconds(void);
void    umac_rtc_set_seconds(uint32_t s);
uint8_t *umac_rtc_pram(void);           /* 20 bytes */
int     umac_rtc_pram_dirty(void);      /* 1 when the Mac wrote PRAM since the last call */

/* Sound: 370 samples a frame, the high byte of each word of the current sound buffer. */
#define UMAC_SND_SAMPLES        370
const uint8_t *umac_sound_buffer(void); /* word i's sample is [2 * i] */
int     umac_sound_enabled(void);
int     umac_sound_volume(void);        /* 0..7 */
int     umac_sound_first_write(void);   /* first buffer word written since the reset, or -1 */
void    umac_sound_mark_reset(void);
/* Debugging (pico-mac): PCs after each umac_run slice go to buf (up to n); the registers as
 * text. */
void    umac_pc_sample(uint32_t *buf, int n);
int     umac_pc_sampled(void);
int     umac_dump_regs(char *buf, int len);
/* Times umac_run cut a loop out of the VBL queue (see vbl_queue_repair in main.c). */
int     umac_vbl_fixes(void);
uint8_t via_peek(int reg);

void    umac_1hz_event(void);

static inline void      umac_vsync_event(void)
{
        via_caX_event(2);
}

/* Return the offset into RAM of the current display buffer */
unsigned int    umac_get_fb_offset(void);

#endif
