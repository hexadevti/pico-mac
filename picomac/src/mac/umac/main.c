/* umac
 *
 * Micro Mac 128K emulator
 *
 * Main file with:
 * - umac_ entry points,
 * - main loop,
 * - address decoding/memory map, dispatch to VIA/SCC/disc
 * - Keyboard/mouse event dispatch
 *
 * Copyright 2024 Matt Evans
 *
 * Small portions of m68k interrupt code, error handling taken from
 * Musashi, which is Copyright 1998-2002 Karl Stenerud.
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

/* pico-mac: reworked from upstream umac's main.c (see THIRD_PARTY.md):
 * - umac_run() runs a number of CPU cycles in short slices, ending each slice at the
 *   next VIA timer event; VIA time is derived from the CPU cycles run
 * - VIA timers, a keyboard event queue (with the keypad prefix), the RTC and its PRAM,
 *   sound/video page accessors, a run-time RAM size
 * - unmapped accesses are ignored instead of stopping the emulation
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <setjmp.h>

#include "machw.h"
#include "m68k.h"
#include "via.h"
#include "scc.h"
#include "rom.h"
#include "disc.h"
#include "umac.h"
#include "state.h"

#define FAST_FUNC(x)    x

#ifdef DEBUG
#define MDBG(...)       printf(__VA_ARGS__)
#else
#define MDBG(...)       do {} while(0)
#endif

/* Data */
uint8_t *_ram_base;
uint8_t *_rom_base;
uint32_t umac_ram_size = 4 * 1024 * 1024;
int umac_disp_w = 512, umac_disp_h = 342;

int overlay = 1;
static int sim_done = 0;
static int error_guard = 0;
static jmp_buf main_loop_jb;

static int disassemble = 0;

/* Time: CPU cycles run, and the VIA ticks derived from them (CPU clock / (10 * speed)). */
static uint64_t cpu_cycles = 0;         /* at the start of the current slice */
static int      in_slice = 0;
static uint64_t via_base_ticks = 0;     /* VIA ticks at cpu_cycles == cyc_base */
static uint64_t cyc_base = 0;
static int      speed_mult = 1;

#define SLICE_CYCLES            (UMAC_CPU_HZ / 1000)    /* about 1 ms */
#define KBD_REPLY_DELAY         (UMAC_CPU_HZ / 250)     /* 4 ms */

static void    update_overlay_layout(void);

/* After m68k_end_timeslice(), m68k_cycles_run() no longer counts: remember it instead. */
static int      slice_ended = 0;
static int      slice_ended_at = 0;

static uint64_t cycles_now(void)
{
        if (!in_slice)
                return cpu_cycles;
        return cpu_cycles + (uint64_t)(slice_ended ? slice_ended_at : m68k_cycles_run());
}

static void     end_slice(void)
{
        if (in_slice && !slice_ended) {
                slice_ended_at = m68k_cycles_run();
                slice_ended = 1;
                m68k_end_timeslice();
        }
}

static uint64_t via_ticks_at(uint64_t cyc)
{
        return via_base_ticks + (cyc - cyc_base) / (10 * (uint64_t)speed_mult);
}

/* The first CPU cycle count at which the VIA time reaches ticks */
static uint64_t cycles_at_ticks(uint64_t ticks)
{
        return cyc_base + (ticks - via_base_ticks) * 10 * (uint64_t)speed_mult;
}

static uint64_t via_time_now(void)
{
        return via_ticks_at(cycles_now());
}

static void     via_timer_set(void)
{
        /* The new expiry may come before the end of this slice */
        end_slice();
}

void    umac_set_speed(int mult)
{
        if (mult < 1)
                mult = 1;
        uint64_t c = cycles_now();
        via_base_ticks = via_ticks_at(c);
        cyc_base = c;
        speed_mult = mult;
}

////////////////////////////////////////////////////////////////////////////////

static int      m68k_dump_regs(char *buf, int len)
{
        int r;
        int orig_len = len;
        for (int i = 0; i < 8; i++) {
                r = snprintf(buf, len, "D%d: %08x  ", i, m68k_get_reg(NULL, M68K_REG_D0 + i));
                buf += r;
                len -= r;
        }
        *buf++ = '\n';
        len--;
        for (int i = 0; i < 8; i++) {
                r = snprintf(buf, len, "A%d: %08x  ", i, m68k_get_reg(NULL, M68K_REG_A0 + i));
                buf += r;
                len -= r;
        }
        *buf++ = '\n';
        len--;
        r = snprintf(buf, len, "SR: %08x  SP: %08x USP: %08x ISP: %08x MSP: %08x\n",
                     m68k_get_reg(NULL, M68K_REG_SR),
                     m68k_get_reg(NULL, M68K_REG_SP),
                     m68k_get_reg(NULL, M68K_REG_USP),
                     m68k_get_reg(NULL, M68K_REG_ISP),
                     m68k_get_reg(NULL, M68K_REG_MSP));
        return orig_len - len;
}

/* Exit with an error message.  Use printf syntax. */
void exit_error(char* fmt, ...)
{
        char buff[500];
        unsigned int pc;
        va_list args;

        if (error_guard)
                return;
        error_guard = 1;

        va_start(args, fmt);
        vfprintf(stderr, fmt, args);
        va_end(args);
        fprintf(stderr, "\n");
        pc = m68k_get_reg(NULL, M68K_REG_PPC);
        m68k_disassemble(buff, pc, M68K_CPU_TYPE_68000);
        fprintf(stderr, "At %04x: %s\n", pc, buff);

        m68k_dump_regs(buff, 500);
        fprintf(stderr, "%s", buff);
        sim_done = 1;
        longjmp(main_loop_jb, 1);
}


////////////////////////////////////////////////////////////////////////////////
// RTC: driven through VIA port B: [0] data, [1] clock, [2] /enable.
// Commands are a byte, MSB first, sampled on the clock's rising edge; bit 7 = read.
// A read then shifts the data byte out (a bit after each rising edge), a write in.

static uint32_t rtc_seconds = 0;
static uint8_t  rtc_pram[20];
static uint8_t  rtc_wp = 0;
static int      rtc_pram_dirty = 0;
static uint8_t  rtc_last_pb = 0x07;
static int      rtc_state = 0;          /* 0 = command, 1 = write data, 2 = read data */
static int      rtc_bits = 0;
static uint8_t  rtc_shift = 0;
static uint8_t  rtc_cmd = 0;
static uint8_t  rtc_out = 1;            /* the data bit the RTC drives */

static uint8_t *rtc_reg(uint8_t cmd, int *is_seconds, int *byte)
{
        int a = (cmd >> 2) & 0x1f;
        *is_seconds = 0;
        *byte = 0;
        if (a < 8) {
                *is_seconds = 1;
                *byte = a & 3;
                return NULL;
        }
        if (a < 12)
                return &rtc_pram[0x10 + (a & 3)];
        if (a == 13)
                return &rtc_wp;
        if (a >= 16)
                return &rtc_pram[a & 15];
        return NULL;    /* test register etc. */
}

static uint8_t  rtc_read_reg(uint8_t cmd)
{
        int sec, byte;
        uint8_t *r = rtc_reg(cmd, &sec, &byte);
        if (sec)
                return (rtc_seconds >> (8 * byte)) & 0xff;
        return r ? *r : 0;
}

static void     rtc_write_reg(uint8_t cmd, uint8_t val)
{
        int sec, byte;
        uint8_t *r = rtc_reg(cmd, &sec, &byte);
        if (r == &rtc_wp) {
                rtc_wp = val;
                return;
        }
        if (rtc_wp & 0x80)
                return;
        if (sec) {
                rtc_seconds = (rtc_seconds & ~(0xffu << (8 * byte))) | ((uint32_t)val << (8 * byte));
        } else if (r && *r != val) {
                *r = val;
                rtc_pram_dirty = 1;
        }
}

static void     rtc_port_b(uint8_t pb)
{
        uint8_t old = rtc_last_pb;
        rtc_last_pb = pb;
        if (pb & 4) {                   /* disabled: reset the transfer */
                rtc_state = 0;
                rtc_bits = 0;
                return;
        }
        if ((old & 2) || !(pb & 2))     /* only rising clock edges matter */
                return;
        if (rtc_state == 2) {
                rtc_out = (rtc_shift >> 7) & 1;
                rtc_shift <<= 1;
                if (++rtc_bits == 8) {
                        rtc_state = 0;
                        rtc_bits = 0;
                }
                return;
        }
        rtc_shift = (rtc_shift << 1) | (pb & 1);
        if (++rtc_bits < 8)
                return;
        rtc_bits = 0;
        if (rtc_state == 1) {
                rtc_write_reg(rtc_cmd, rtc_shift);
                rtc_state = 0;
        } else {
                rtc_cmd = rtc_shift;
                if (rtc_cmd & 0x80) {
                        rtc_shift = rtc_read_reg(rtc_cmd);
                        rtc_state = 2;
                } else {
                        rtc_state = 1;
                }
        }
}

uint32_t umac_rtc_seconds(void)
{
        return rtc_seconds;
}

void    umac_rtc_set_seconds(uint32_t s)
{
        rtc_seconds = s;
}

uint8_t *umac_rtc_pram(void)
{
        return rtc_pram;
}

int     umac_rtc_pram_dirty(void)
{
        int d = rtc_pram_dirty;
        rtc_pram_dirty = 0;
        return d;
}

void    umac_1hz_event(void)
{
        rtc_seconds++;
        via_caX_event(1);
}

////////////////////////////////////////////////////////////////////////////////
// VIA-related controls
static uint8_t  via_ra_old = 0x10;      /* pico-mac: out of the function, for snapshots */
static void     via_ra_changed(uint8_t val)
{
        // 7 = scc w/req,a,b (in, indicates RX pending, w/o IRQ)
        // 6 = vid.pg2 (screen buffer select)
        // 5 = hd.sel (SEL line, select head)
        // 4 = overlay
        // 3 = snd.pg2 (sound buffer select)
        // [2:0] = sound volume
        overlay = !!(val & 0x10);
        if ((via_ra_old ^ val) & 0x10) {
                MDBG("OVERLAY CHANGING\n");
                update_overlay_layout();
        }

        via_ra_old = val;
}

static void     via_rb_changed(uint8_t val)
{
        // 7 = sndres (sound enable/disable)
        // 6 = hblank
        // 5 = mouse8 (in, mouse Y2)
        // 4 = mouse4 (in, mouse X2)
        // 3 = mouse7 (in, 0 = button pressed)
        // [2:0] = RTC controls
        rtc_port_b(val);
}

static uint8_t  via_ra_in(void)
{
        return 0;
}

// Quadrature bits read from B[5:4] (Y=5, X=4)
static uint8_t via_quadbits = 0;
static uint8_t via_mouse_pressed = 0;

static uint8_t  via_rb_in(void)
{
        uint8_t v = via_quadbits | rtc_out;
        // Mouse not pressed!
        if (!via_mouse_pressed)
                v |= (1 << 3);
        return v;
}

/* Keyboard interface:
 *
 * Very roughly, it uses CB2 as bidirectional data and CB1 as clock
 * always from keyboard.  There's a handshake with the mac driving
 * data low as a "request to start clocking", with the kbd receiving a
 * byte (clocking out from SR) after that.  The mac does this by
 * "transmitting a byte" of all zeroes, which looks like pulling data
 * low.
 *
 * The VIA SR has a sequence of interrupts as follows:
 * - Mac pulls data low (transmits zero) then immediately loads SR
 *   with the data to TX (a command such as Inquiry)
 * - The VIA asserts SR IRQ when the command's transmitted (the kbd
 *   has woken and clocked it out).
 * - The keyboard -- some time later, importantly -- responds with
 *   a byte in SR, and VIA asserts SR IRQ again.
 *
 * The keyboard does nothing except for respond to commands from the
 * host (i.e. there's nothing proactively transmitted).
 */
#define KBD_CMD_INQUIRY         0x10
#define KBD_CMD_INSTANT         0x14
#define KBD_CMD_GET_MODEL       0x16
#define KBD_CMD_TEST            0x36
#define KBD_MODEL               5
#define KBD_RSP_NULL            0x7b
#define KBD_RSP_ACK             0x7d
#define KBD_KEYPAD_PREFIX       0x79

static int kbd_last_cmd = 0;
static uint64_t kbd_last_cmd_time = 0;

/* pico-mac: a queue of protocol bytes instead of one pending event */
#define KBD_QUEUE_LEN   64
static uint8_t kbd_queue[KBD_QUEUE_LEN];
static unsigned int kbd_q_head = 0, kbd_q_tail = 0;

static void     kbd_push(uint8_t b)
{
        unsigned int n = (kbd_q_head + 1) % KBD_QUEUE_LEN;
        if (n == kbd_q_tail)
                return;         /* full: drop */
        kbd_queue[kbd_q_head] = b;
        kbd_q_head = n;
}

static int      kbd_pop(void)
{
        if (kbd_q_head == kbd_q_tail)
                return -1;
        uint8_t b = kbd_queue[kbd_q_tail];
        kbd_q_tail = (kbd_q_tail + 1) % KBD_QUEUE_LEN;
        return b;
}

static void     via_sr_tx(uint8_t data)
{
        if (kbd_last_cmd) {
                MDBG("KBD: Oops, transmitting %02x whilst cmd %02x pending!\n",
                     data, kbd_last_cmd);
        }
        kbd_last_cmd = data;
        kbd_last_cmd_time = cycles_now();
}

/* Emulate the keyboard: receive commands (such as an inquiry, polling
 * for keypresses) and respond using via_sr_rx().
 */
static void     kbd_rx(uint8_t data)
{
        int e;
        /* Respond to requests with potted keyboard banter */
        switch (data) {
        case KBD_CMD_GET_MODEL:
                via_sr_rx(0x01 | (KBD_MODEL << 1));
                break;
        case KBD_CMD_INQUIRY:
        case KBD_CMD_INSTANT:
                e = kbd_pop();
                via_sr_rx(e < 0 ? KBD_RSP_NULL : (uint8_t)e);
                break;
        case KBD_CMD_TEST:
                via_sr_rx(KBD_RSP_ACK);
                break;
        default:
                MDBG("KBD: Unhandled TX %02x\n", data);
        }
}

static void     kbd_check_work(void)
{
        /* Process a keyboard command a little later than the transmit
         * time (i.e. not immediately, which makes the mac feel rushed
         * and causes it to ignore the response to punish our
         * hastiness).
         */
        if (kbd_last_cmd && (cpu_cycles - kbd_last_cmd_time) > KBD_REPLY_DELAY) {
                MDBG("KBD: got cmd 0x%x\n", kbd_last_cmd);
                kbd_rx(kbd_last_cmd);
                kbd_last_cmd = 0;
        }
}

void    umac_kbd_event(uint8_t scancode, int down)
{
        /* keymap.h gives the arrows' ADB codes; the Plus keyboard's are on its keypad */
        switch (scancode) {
        case 0x7b: scancode = 0x46; break;      /* left */
        case 0x7c: scancode = 0x42; break;      /* right */
        case 0x7d: scancode = 0x48; break;      /* down */
        case 0x7e: scancode = 0x4d; break;      /* up */
        }
        if (scancode >= 0x60)
                return;                         /* no such key on a Plus */
        if (scancode >= 0x40)
                kbd_push(KBD_KEYPAD_PREFIX);
        kbd_push(((scancode << 1) | 1) | (down ? 0 : 0x80));
}

// VIA IRQ output hook:
static void     via_irq_set(int status)
{
        MDBG("[IRQ: VIA IRQ %d]\n", status);
        m68k_set_virq(1, status ? 1 : 0);
        /* Musashi only checks interrupts between timeslices */
        if (status)
                end_slice();
}

// Ditto, for SCC
static int scc_irq_state = 0;
static void     scc_irq_set(int status)
{
        MDBG("[IRQ: SCC IRQ %d]\n", status);
        m68k_set_virq(2, status ? 1 : 0);
        if (status)
                end_slice();
        scc_irq_state = status;
}

////////////////////////////////////////////////////////////////////////////////
// IWM

static uint8_t iwm_regs[16];

void    iwm_write(unsigned int address, uint8_t data)
{
        unsigned int r = (address >> 9) & 0xf;
        MDBG("[IWM: WR %02x -> %d]\n", data, r);
        iwm_regs[r] = data;
}

uint8_t iwm_read(unsigned int address)
{
        unsigned int r = (address >> 9) & 0xf;
        uint8_t data = iwm_regs[r];
        switch (r) {
        case 8:
                data = 0xff;
                break;
        case 14:
                data = 0x1f;
                break;
        default:
                MDBG("[IWM: unhandled RD of reg %d]\n", r);
        }
        MDBG("[IWM: RD %d <- %02x]\n", r, data);
        return data;
}

////////////////////////////////////////////////////////////////////////////////

static unsigned int  FAST_FUNC(cpu_read_instr_normal)(unsigned int address)
{
        /* Can check for 0x400000 (ROM) and otherwise RAM */
        if ((address & 0xf00000) != ROM_ADDR)
                return RAM_RD_ALIGNED_BE16(CLAMP_RAM_ADDR(address));
        else
                return ROM_RD_ALIGNED_BE16(address & (ROM_SIZE - 1));
}

static unsigned int  FAST_FUNC(cpu_read_instr_overlay)(unsigned int address)
{
        /* Need to check for both 0=ROM, 0x400000=ROM, and RAM at 0x600000...
         */
        if (IS_ROM(address))
                return ROM_RD_ALIGNED_BE16(address & (ROM_SIZE - 1));
        else /* RAM */
                return RAM_RD_ALIGNED_BE16(CLAMP_RAM_ADDR(address));
}

unsigned int (*cpu_read_instr)(unsigned int address) = cpu_read_instr_overlay;

/* Read data from RAM, ROM, or a device */
unsigned int    FAST_FUNC(cpu_read_byte)(unsigned int address)
{
        /* Most likely a RAM access, followed by a ROM access, then I/O */
        if (IS_RAM(address))
                return RAM_RD8(CLAMP_RAM_ADDR(address));
        if (IS_ROM(address))
                return ROM_RD8(address & (ROM_SIZE - 1));

        // decode IO etc
        if (IS_VIA(address))
                return via_read(address);
        if (IS_IWM(address))
                return iwm_read(address);
        if (IS_SCC_RD(address))
                return scc_read(address);
        MDBG("Read byte from unmapped %08x\n", address);
        return 0;
}

/* pico-mac: word/long accesses to devices go to the byte handler (the data is on one
 * byte lane); other unmapped accesses read 0 instead of stopping the emulation.
 */
static int      is_device_rd(unsigned int address)
{
        return IS_VIA(address) || IS_IWM(address) || IS_SCC_RD(address);
}

static int      is_device_wr(unsigned int address)
{
        return IS_VIA(address) || IS_IWM(address) || IS_SCC_WR(address);
}

unsigned int    FAST_FUNC(cpu_read_word)(unsigned int address)
{
        if (IS_RAM(address))
                return RAM_RD16(CLAMP_RAM_ADDR(address));
        if (IS_ROM(address))
                return ROM_RD16(address & (ROM_SIZE - 1));
        if (is_device_rd(address)) {
                unsigned int v = cpu_read_byte(address);
                return (v << 8) | v;
        }
        MDBG("Read word from unmapped %08x\n", address);
        return 0;
}

unsigned int    FAST_FUNC(cpu_read_long)(unsigned int address)
{
        if (IS_RAM(address))
                return RAM_RD32(CLAMP_RAM_ADDR(address));
        if (IS_ROM(address))
                return ROM_RD32(address & (ROM_SIZE - 1));
        if (is_device_rd(address))
                return (cpu_read_word(address) << 16) | cpu_read_word(address + 2);
        MDBG("Read long from unmapped %08x\n", address);
        return 0;
}


unsigned int    cpu_read_word_dasm(unsigned int address)
{
        if (IS_RAM(address))
                return RAM_RD16(CLAMP_RAM_ADDR(address));
        if (IS_ROM(address))
                return ROM_RD16(address & (ROM_SIZE - 1));
        return 0;
}

unsigned int    cpu_read_long_dasm(unsigned int address)
{
        if (IS_RAM(address))
                return RAM_RD32(CLAMP_RAM_ADDR(address));
        if (IS_ROM(address))
                return ROM_RD32(address & (ROM_SIZE - 1));
        return 0;
}


/* First sound-buffer word written since umac_sound_mark_reset() (-1 if none).
 * The Sound Manager refills the buffer as a ring; this is where the ring starts. */
static int snd_first_write = -1;

static inline void snd_note_write(unsigned int a)
{
        if (snd_first_write < 0) {
                unsigned int m = a - (umac_ram_size - 0x300);
                unsigned int x = a - (umac_ram_size - 0x5f00);
                if (m < 2 * 370)
                        snd_first_write = m >> 1;
                else if (x < 2 * 370)
                        snd_first_write = x >> 1;
        }
}

/* Write data to RAM or a device */
void    FAST_FUNC(cpu_write_byte)(unsigned int address, unsigned int value)
{
        if (IS_RAM(address)) {
                snd_note_write(CLAMP_RAM_ADDR(address));
                RAM_WR8(CLAMP_RAM_ADDR(address), value);
                return;
        }

        // decode IO
        if (IS_VIA(address)) {
                via_write(address, value);
                return;
        }
        if (IS_IWM(address)) {
                iwm_write(address, value);
                return;
        }
        if (IS_SCC_WR(address)) {
                scc_write(address, value);
                return;
        }
        if (ADR24(address) == PV_SONY_ADDR) {
                int r = disc_pv_hook(value);
                if (r)
                        exit_error("Disc PV hook failed (%02x)", value);
                return;
        }
        MDBG("Ignoring write %02x to address %08x\n", value&0xff, address);
}

void    FAST_FUNC(cpu_write_word)(unsigned int address, unsigned int value)
{
        if (IS_RAM(address)) {
                snd_note_write(CLAMP_RAM_ADDR(address));
                RAM_WR16(CLAMP_RAM_ADDR(address), value);
                return;
        }
        if (is_device_wr(address)) {
                cpu_write_byte(address, (value >> 8) & 0xff);
                return;
        }
        MDBG("Ignoring write %04x to address %08x\n", value&0xffff, address);
}

void    FAST_FUNC(cpu_write_long)(unsigned int address, unsigned int value)
{
        if (IS_RAM(address)) {
                snd_note_write(CLAMP_RAM_ADDR(address));
                RAM_WR32(CLAMP_RAM_ADDR(address), value);
                return;
        }
        if (is_device_wr(address)) {
                cpu_write_word(address, value >> 16);
                cpu_write_word(address + 2, value & 0xffff);
                return;
        }
        MDBG("Ignoring write %08x to address %08x\n", value, address);
}

/* Update function pointers for memory accessors based on overlay state/memory map layout */
static void     update_overlay_layout(void)
{
        if (overlay) {
                cpu_read_instr = cpu_read_instr_overlay;
        } else {
                cpu_read_instr = cpu_read_instr_normal;
        }
}

/* Called when the CPU pulses the RESET line */
void    cpu_pulse_reset(void)
{
        /* Reset IRQs etc. */
}

/* Called when the CPU acknowledges an interrupt */
int     cpu_irq_ack(int level)
{
        (void)level;
        /* Level really means line, so do an ack per device */
        return M68K_INT_ACK_AUTOVECTOR;
}

/* Disassembler */
static void     make_hex(char* buff, unsigned int pc, unsigned int length)
{
        char* ptr = buff;

        for(;length>0;length -= 2)
        {
                sprintf(ptr, "%04x", cpu_read_word_dasm(pc));
                pc += 2;
                ptr += 4;
                if(length > 2)
                        *ptr++ = ' ';
        }
}

void    cpu_instr_callback(int pc)
{
        static char buff[100];
        static char buff2[100];
        static unsigned int instr_size;

        if (!disassemble)
                return;

        instr_size = m68k_disassemble(buff, pc, M68K_CPU_TYPE_68000);
        make_hex(buff2, pc, instr_size);
        printf("E %06x: %-20s: %s\n", pc, buff2, buff);
}

static disc_descr_t saved_discs[DISC_NUM_DRIVES];
static int disc_ejected = 0;
static int old_dcd_a = 0, old_dcd_b = 0;
static int pending_mouse_deltax = 0;
static int pending_mouse_deltay = 0;

static void     machine_reset(void)
{
        overlay = 1;
        update_overlay_layout();
        sim_done = 0;
        error_guard = 0;
        disc_ejected = 0;
        kbd_last_cmd = 0;
        kbd_q_head = kbd_q_tail = 0;
        rtc_state = 0;
        rtc_bits = 0;
        rtc_last_pb = 0x07;
        rtc_out = 1;
        via_quadbits = 0;
        via_mouse_pressed = 0;
        old_dcd_a = old_dcd_b = 0;
        pending_mouse_deltax = pending_mouse_deltay = 0;

        struct via_cb vcb = { .ra_change = via_ra_changed,
                              .rb_change = via_rb_changed,
                              .ra_in = via_ra_in,
                              .rb_in = via_rb_in,
                              .sr_tx = via_sr_tx,
                              .irq_set = via_irq_set,
                              .time_now = via_time_now,
                              .timer_set = via_timer_set,
        };
        via_init(&vcb);
        struct scc_cb scb = { .irq_set = scc_irq_set,
        };
        scc_init(&scb);
        scc_irq_state = 0;
        disc_init(saved_discs);

        m68k_set_virq(1, 0);
        m68k_set_virq(2, 0);
        m68k_pulse_reset();
}

int     umac_init(void *ram_base, void *rom_base, uint32_t ram_size, disc_descr_t discs[DISC_NUM_DRIVES])
{
        _ram_base = ram_base;
        _rom_base = rom_base;
        umac_ram_size = ram_size;
        memcpy(saved_discs, discs, sizeof(saved_discs));

        cpu_cycles = 0;
        cyc_base = 0;
        via_base_ticks = 0;
        speed_mult = 1;
        in_slice = 0;

        m68k_init();
        m68k_set_cpu_type(M68K_CPU_TYPE_68000);
        machine_reset();
        return 0;
}

////////////////////////////////////////////////////////////////////////////////
// pico-mac: snapshots. The machine is saved between umac_run calls; a load goes into a machine
// umac_init started with the same RAM size, screen size and disks.

static const char umac_state_magic[8] = { 'U', 'M', 'A', 'C', 'S', 'T', '0', '1' };

static void     machine_state(umac_state_t *s, void *cpu, uint32_t cpu_size)
{
        umac_st(s, cpu, cpu_size);
        UMAC_ST(s, cpu_cycles);
        UMAC_ST(s, via_base_ticks);
        UMAC_ST(s, cyc_base);
        UMAC_ST(s, speed_mult);
        UMAC_ST(s, overlay);
        UMAC_ST(s, rtc_seconds);
        umac_st(s, rtc_pram, sizeof(rtc_pram));
        UMAC_ST(s, rtc_wp);
        UMAC_ST(s, rtc_last_pb);
        UMAC_ST(s, rtc_state);
        UMAC_ST(s, rtc_bits);
        UMAC_ST(s, rtc_shift);
        UMAC_ST(s, rtc_cmd);
        UMAC_ST(s, rtc_out);
        UMAC_ST(s, via_ra_old);
        UMAC_ST(s, via_quadbits);
        UMAC_ST(s, via_mouse_pressed);
        UMAC_ST(s, kbd_last_cmd);
        UMAC_ST(s, kbd_last_cmd_time);
        umac_st(s, kbd_queue, sizeof(kbd_queue));
        UMAC_ST(s, kbd_q_head);
        UMAC_ST(s, kbd_q_tail);
        UMAC_ST(s, scc_irq_state);
        umac_st(s, iwm_regs, sizeof(iwm_regs));
        UMAC_ST(s, disc_ejected);
        UMAC_ST(s, old_dcd_a);
        UMAC_ST(s, old_dcd_b);
        UMAC_ST(s, pending_mouse_deltax);
        UMAC_ST(s, pending_mouse_deltay);
        via_state(s);
        scc_state(s);
        disc_state(s);
        umac_st(s, _ram_base, umac_ram_size);
}

int     umac_save_state(umac_state_io io, void *ctx)
{
        umac_state_t s = { io, ctx, 0, 0 };
        uint32_t head[4] = { umac_ram_size, (uint32_t)umac_disp_w, (uint32_t)umac_disp_h, m68k_context_size() };
        void *cpu = malloc(head[3]);
        if (!cpu)
                return -1;
        m68k_get_context(cpu);
        umac_st(&s, (void *)umac_state_magic, sizeof(umac_state_magic));
        umac_st(&s, head, sizeof(head));
        machine_state(&s, cpu, head[3]);
        free(cpu);
        return s.err ? -1 : 0;
}

int     umac_load_state(umac_state_io io, void *ctx)
{
        umac_state_t s = { io, ctx, 1, 0 };
        char magic[8];
        uint32_t head[4];
        umac_st(&s, magic, sizeof(magic));
        umac_st(&s, head, sizeof(head));
        if (s.err)
                return -1;
        if (memcmp(magic, umac_state_magic, 8) || head[0] != umac_ram_size || head[1] != (uint32_t)umac_disp_w ||
            head[2] != (uint32_t)umac_disp_h || head[3] != m68k_context_size())
                return -2;
        void *cpu = malloc(head[3]);
        if (!cpu)
                return -1;
        machine_state(&s, cpu, head[3]);
        if (!s.err)
                m68k_set_context_keep_hooks(cpu);
        free(cpu);
        if (s.err)
                return -1;
        in_slice = 0;
        slice_ended = 0;
        sim_done = 0;
        error_guard = 0;
        update_overlay_layout();
        return 0;
}

void    umac_reset(void)
{
        machine_reset();
}

void    umac_opt_disassemble(int enable)
{
        disassemble = enable;
}

#define MOUSE_MAX_PENDING_PIX   60

/* Provide mouse input (movement, button) data.
 *
 * X is positive going right; Y is positive going upwards.
 */
void    umac_mouse(int deltax, int deltay, int button)
{
        pending_mouse_deltax += deltax;
        pending_mouse_deltay += deltay;

        /* Clamp if the UI has flooded with lots and lots of steps!
         */
        if (pending_mouse_deltax > MOUSE_MAX_PENDING_PIX)
                pending_mouse_deltax = MOUSE_MAX_PENDING_PIX;
        if (pending_mouse_deltax < -MOUSE_MAX_PENDING_PIX)
                pending_mouse_deltax = -MOUSE_MAX_PENDING_PIX;
        if (pending_mouse_deltay > MOUSE_MAX_PENDING_PIX)
                pending_mouse_deltay = MOUSE_MAX_PENDING_PIX;
        if (pending_mouse_deltay < -MOUSE_MAX_PENDING_PIX)
                pending_mouse_deltay = -MOUSE_MAX_PENDING_PIX;

        via_mouse_pressed = button;
}

static void     mouse_tick(void)
{
        /* Periodically, check if the mouse X/Y deltas are non-zero.
         * If a movement is required, encode one step in X and/or Y
         * and deduct from the pending delta.
         *
         * The step ultimately posts an SCC IRQ, so we _don't_ try to
         * make any more steps while an IRQ is currently pending.
         */
        if (pending_mouse_deltax == 0 && pending_mouse_deltay == 0)
                return;

        if (scc_irq_state == 1)
                return;

        /* Mouse X/Y quadrature signals are wired to:
         *  VIA Port B[4] & SCC DCD_A for X
         *  VIA Port B[5] & SCC DCD_B for Y
         *
         * As VIA mouse signals aren't sampled until IRQ, can do this
         * in one step, toggling existing DCD states and setting VIA
         * either equal or opposite to DCD:
         */
        int dcd_a = old_dcd_a;
        int dcd_b = old_dcd_b;
        int deltax = pending_mouse_deltax;
        int deltay = pending_mouse_deltay;
        uint8_t qb = via_quadbits;

        if (deltax) {
                dcd_a = !dcd_a;
                qb = (qb & ~0x10) | ((deltax < 0) == dcd_a ? 0x10 : 0);
                pending_mouse_deltax += (deltax > 0) ? -1 : 1;
        }

        if (deltay) {
                dcd_b = !dcd_b;
                qb = (qb & ~0x20) | ((deltay < 0) == dcd_b ? 0x20 : 0);
                pending_mouse_deltay += (deltay > 0) ? -1 : 1;
        }

        via_quadbits = qb;
        old_dcd_a = dcd_a;
        old_dcd_b = dcd_b;
        scc_set_dcd(dcd_a, dcd_b);
}

/* Called by the disc code when an eject op happens: the frontend decides what to do. */
void    umac_disc_ejected(void)
{
        disc_ejected = 1;
}

int     umac_disc_was_ejected(void)
{
        return disc_ejected;
}

/* Run the emulator for about this many CPU cycles.
 * Returns 0 for not-done, 1 when an exit/done condition arises.
 */
/* pico-mac: for finding where a hung Mac loops (mac_core's "@mac" serial command). The PC
 * after each slice of umac_run goes to buf, up to n of them. */
static uint32_t *pc_samples;
static int       pc_want, pc_count;

void    umac_pc_sample(uint32_t *buf, int n)
{
        pc_samples = buf;
        pc_count = 0;
        pc_want = buf ? n : 0;
}

int     umac_pc_sampled(void)
{
        return pc_count;
}

int     umac_dump_regs(char *buf, int len)
{
        return m68k_dump_regs(buf, len);
}

/* The System 7 sound driver can VInstall its VBL task while the task is still in the queue (its
 * "running" flag is cleared before the ROM dequeues it, and a nested interrupt can start a sound
 * in that window). Enqueue then links the tail element to itself, and the ROM's VBL loop spins
 * forever. Once per umac_run the queue is walked; a link back to an element already seen is cut
 * there, which leaves the element queued once, as the second VInstall should have.
 */
static int vbl_fixes;

static void     vbl_queue_repair(void)
{
        enum { QHEAD = 0x162, QTAIL = 0x166, MAXQ = 64 };
        uint32_t seen[MAXQ];
        int n = 0;
        uint32_t e = RAM_RD32(QHEAD) & 0xffffff;
        while (e) {
                if (e >= umac_ram_size - 4 || (e & 1) || n == MAXQ)
                        return;                 /* not a list we can judge */
                seen[n++] = e;
                uint32_t next = RAM_RD32(e) & 0xffffff;
                for (int i = 0; i < n; i++) {
                        if (seen[i] == next) {
                                RAM_WR32(e, 0);
                                RAM_WR32(QTAIL, e);
                                vbl_fixes++;
                                return;
                        }
                }
                e = next;
        }
}

int     umac_vbl_fixes(void)
{
        return vbl_fixes;
}

int     umac_run(uint32_t cycles)
{
        if (sim_done)
                return 1;
        if (setjmp(main_loop_jb)) {
                in_slice = 0;
                return 1;
        }

        uint64_t end = cpu_cycles + cycles;
        while (cpu_cycles < end && !sim_done) {
                uint64_t t = via_ticks_at(cpu_cycles);
                via_tick(t);
                uint64_t slice = end - cpu_cycles;
                if (slice > SLICE_CYCLES)
                        slice = SLICE_CYCLES;
                uint64_t ev = via_next_event(t);
                if (ev != UINT64_MAX) {
                        uint64_t at = cycles_at_ticks(t + (ev ? ev : 1));
                        uint64_t need = at > cpu_cycles ? at - cpu_cycles : 1;
                        if (need < slice)
                                slice = need;
                }
                in_slice = 1;
                slice_ended = 0;
                int used = m68k_execute((int)slice);
                in_slice = 0;
                if (pc_count < pc_want)
                        pc_samples[pc_count++] = m68k_get_reg(NULL, M68K_REG_PC);
                if (used < 1)
                        used = 1;
                cpu_cycles += used;

                // Device polling
                mouse_tick();
                kbd_check_work();
        }
        vbl_queue_repair();
        return sim_done;
}

////////////////////////////////////////////////////////////////////////////////
// Video and sound: buffers at the top of RAM, picked by VIA port A.

unsigned int    umac_get_fb_offset(void)
{
        unsigned int dist = (unsigned int)(DISP_WIDTH * DISP_HEIGHT / 8) + 0x380;
        unsigned int off = umac_ram_size - dist;
        /* PA6 = 0 selects the alternate page (only on the stock 512x342 layout) */
        if (DISP_WIDTH == 512 && DISP_HEIGHT == 342 && !(via_get_ora() & 0x40))
                off -= 0x8000;
        return off;
}

const uint8_t *umac_sound_buffer(void)
{
        /* PA3 = 1: main buffer at memtop - 0x300, else alternate at memtop - 0x5f00 */
        unsigned int off = umac_ram_size - ((via_get_ora() & 0x08) ? 0x300 : 0x5f00);
        return _ram_base + off;
}

int     umac_sound_enabled(void)
{
        return !(via_get_orb() & 0x80);
}

int     umac_sound_volume(void)
{
        return via_get_ora() & 7;
}

int     umac_sound_first_write(void)
{
        return snd_first_write;
}

void    umac_sound_mark_reset(void)
{
        snd_first_write = -1;
}
