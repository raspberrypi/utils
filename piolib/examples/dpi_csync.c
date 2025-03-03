/**
 * Copyright (c) 2024 Raspberry Pi Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

// Generate COMPOSITE SYNC for DPI on Raspberry Pi 5.
//
// There are two cases, for progressive and interlaced video.
// The progressive case works on any kernel that supports PIOLIB.
// Interlaced DPI requires a recent kernel, and this program will
// only work when GPIO 1 has NOT been assigned to DPI, or when
// DPI has not yet been configured in an interlaced video mode
// (otherwise, the DPI driver will have claimed PIO already!)
//
// For correct CSYNC output, horizontal sync width and line period
// (in microseconds) and H and V sync polarities must all be given.
// CSYNC can be mapped to any GPIO except 2,3. The default is GPIO 1.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"

static void usage(const char * progname)
{
    fprintf(stderr, "Usage: %s [options]\n", progname);
    fprintf(stderr, "\t-h               Assume HSync is active low (default)\n");
    fprintf(stderr, "\t+h               Assume HSync is active high\n");
    fprintf(stderr, "\t-v               Assume VSync is active low (default)\n");
    fprintf(stderr, "\t+v               Assume VSync is active high\n");
    fprintf(stderr, "\t-c               CSync output will be active low (default)\n");
    fprintf(stderr, "\t+c               CSync output will be active high\n");
    fprintf(stderr, "\t-o <goio>        Select output GPIO pin number. Default 1\n");
    fprintf(stderr, "\t-s <us>          Set HSync width in microseconds. Default 4.7\n");
    fprintf(stderr, "\t-t <us>          Set line period in microseconds. Default 64.0\n");
    fprintf(stderr, "\t-e <us>          Set EQ pulse width when interlaced. Default 0.0\n");
    fprintf(stderr, "\t-w <halflines>   Set VSW in half-lines when interlaced. Default 6\n");
    fprintf(stderr, "\t-i | --interlace Generate serrated CSync for an interlaced mode\n");
    fprintf(stderr, "\t-p | --pal       Equivalent to -i -s 4.7 -t 64.0 -e 2.35 -w 5\n");
    fprintf(stderr, "\t-n | --ntsc      Equivalent to -i -s 4.7 -t 63.56 -e 2.3 -w 6\n");
}

unsigned int opt_hpos   = 0;
unsigned int opt_vpos   = 0;
unsigned int opt_cpos   = 0;
unsigned int opt_gpio   = 1;
unsigned int opt_ilace  = 0;
unsigned int opt_vsw    = 6;
double       opt_hsw    = 4.7;
double       opt_period = 64.0;
double       opt_eqp    = 0.0;

static void setpal()
{
    opt_ilace  = 1;
    opt_hsw    = 4.7;
    opt_period = 64.0;
    opt_vsw    = 5;
    opt_eqp    = 2.35;
}

static void setntsc()
{
    opt_ilace  = 1;
    opt_hsw    = 4.7;
    opt_period = 63.56;
    opt_vsw    = 6;
    opt_eqp    = 2.3;
}

static int getopts(int argc, const char **argv)
{
    for (; argc > 1; argc--, argv++) {
        if (argv[1][0] != '+' && argv[1][0] != '-')
            return -1;
        if (argv[1][1] == '-') {
            if (!strcmp(argv[1], "--interlace"))
                opt_ilace = 1;
            else if (!strcasecmp(argv[1], "--pal"))
                setpal();
            else if (!strcasecmp(argv[1], "--ntsc"))
                setntsc();
            else
                return -1;
            continue;
        }
        switch (argv[1][1]) {
        case 'h':
            opt_hpos = (argv[1][0]=='+');
            break;
        case 'v':
            opt_vpos = (argv[1][0]=='+');
            break;
        case 'c':
            opt_cpos = (argv[1][0]=='+');
            break;
        case 'o':
            if (--argc < 2) return -1;
            argv++;
            opt_gpio = atoi(argv[1]);
            break;
        case 's':
            if (--argc < 2) return -1;
            argv++;
            opt_hsw = atof(argv[1]);
            break;
        case 't':
            if (--argc < 2) return -1;
            argv++;
            opt_period = atof(argv[1]);
            break;
        case 'e':
            if (--argc < 2) return -1;
            argv++;
            opt_eqp = atof(argv[1]);
            break;
        case 'w':
            if (--argc < 2) return -1;
            argv++;
            opt_vsw = atoi(argv[1]);
            break;
        case 'i':
            opt_ilace = 1;
            break;
        case 'p':
            setpal();
            break;
        case 'n':
            setntsc();
            break;
        default:
            return -1;
        }
    }

    return 0;
}


/*
 * COMPOSITE SYNC FOR PROGRESSIVE
 *
 * Copy HSYNC pulses to CSYNC (adding 1 cycle); then when VSYNC
 * is asserted, extend each pulse by an additional Y + 1 cycles.
 *
 * The following time constant should be written to the FIFO:
 *    (htotal - 2 * hsync_width) * sys_clock / dpi_clock - 2.
 *
 * The default configuration is +HSync, +VSync, -CSync; other
 * polarities can be made by modifying the PIO program code.
 */

static int setup_pio_for_csync_prog(PIO pio)
{
    double tc;
    unsigned int i, offset;
    unsigned short instructions[] = {  /* This is mutable */
        0x90a0, //  0: pull   block      side 1
        0x7040, //  1: out    y, 32      side 1
        //     .wrap_target
        0xb322, //  2: mov    x, y       side 1 [3]
        0x3083, //  3: wait   1 gpio, 3  side 1
        0xa422, //  4: mov    x, y       side 0 [4]
        0x2003, //  5: wait   0 gpio, 3  side 0
        0x00c7, //  6: jmp    pin, 7     side 0    ; modify to flip VSync polarity
        //     .wrap                               ; modify to flip VSync polarity
        0x0047, //  7: jmp    x--, 7     side 0
        0x1002, //  8: jmp    2          side 1
    };
    struct pio_program prog = {
        .instructions = instructions,
        .length = sizeof(instructions) / sizeof(instructions[0]),
        .origin = -1
    };
    pio_sm_config cfg = pio_get_default_sm_config();
    int sm = pio_claim_unused_sm(pio, true);
    if (sm < 0)
        return -1;

    /* Adapt program code for sync polarity; configure program */
    pio_sm_set_enabled(pio, sm, false);
    if (!opt_vpos)
        instructions[6] = 0x00c2; /* jmp pin, 2 side 0 */
    if (!opt_hpos) {
        instructions[3] ^= 0x80;
        instructions[5] ^= 0x80;
    }
    if (opt_cpos) {
        for (i = 0; i < prog.length; i++)
            instructions[i] ^= 0x1000;
    }
    offset = pio_add_program(pio, &prog);
    if (offset == PIO_ORIGIN_ANY)
        return -1;

    /* Configure pins and SM */
    sm_config_set_wrap(&cfg, offset + 2,
               offset + (opt_vpos ? 6 : 7));
    sm_config_set_sideset(&cfg, 1, false, false);
    sm_config_set_sideset_pins(&cfg, opt_gpio);
    pio_gpio_init(pio, opt_gpio);
    sm_config_set_jmp_pin(&cfg, 2); /* VSync on GPIO 2 */
    pio_sm_init(pio, sm, offset, &cfg);
    pio_sm_set_consecutive_pindirs(pio, sm, opt_gpio, 1, true);

    /* Place time constant into the FIFO; start the SM */
    tc = 1.0e-6 * ((opt_period - 2.0 * opt_hsw) * (double) clock_get_hz(clk_sys));
    pio_sm_put(pio, sm, (unsigned int)(tc - 2.0));
    pio_sm_set_enabled(pio, sm, true);

    return 0;
}

static int start_timers(PIO pio, int hedge, const unsigned int tc[3])
{
    static const unsigned short instructions[2][4] = {
        { 0xa022, 0x2083, 0x0042, 0xc010 }, /* posedge */
        { 0xa022, 0x2003, 0x0042, 0xc010 }, /* negedge */
    };
    const struct pio_program prog = {
        .instructions = instructions[hedge],
        .length = 4,
        .origin = -1
    };
    unsigned int offset, i;

    pio_claim_sm_mask(pio, 0xE); /* Claim three SMs, starting from #1 */

    offset = pio_add_program(pio, &prog);
    if (offset == PIO_ORIGIN_ANY)
        return -1;

    for (i = 0; i < 3; i++) {
        pio_sm_config cfg = pio_get_default_sm_config();

        pio_sm_set_enabled(pio, i + 1, false);
        sm_config_set_wrap(&cfg, offset, offset + 3);
        pio_sm_init(pio, i + 1, offset, &cfg);

        pio_sm_put(pio, i + 1, tc[i] - 4);
        pio_sm_exec(pio, i + 1, pio_encode_pull(false, false));
        pio_sm_exec(pio, i + 1, pio_encode_out(pio_y, 32));
        pio_sm_set_enabled(pio, i + 1, true);
    }

    return 0;
}

/*
 * COMPOSITE SYNC FOR INTERLACED
 *
 * DPI VSYNC (GPIO2) must be a modified signal which is always active-low.
 * It should go low for 1 or 2 scanlines, 2 or 2.5 lines before Vsync-start.
 * Desired VSync width minus 1 (in half-lines) should be written to the FIFO.
 *
 * Three PIO SMs will be configured as timers, to fire at the end of a left
 * broad pulse, the middle of a scanline, and the end of a right broad pulse.
 *
 * HSYNC->CSYNC latency is about 5 cycles, with a jitter of up to 1 cycle.
 *
 * Default program is compiled for +HSync, -CSync. The program may be
 * modified for other polarities. GPIO2 polarity is always active low.
 */

static int setup_pio_for_csync_ilace(PIO pio)
{
    static const int wrap_target = 2;
    static const int wrap = 23;
    unsigned short instructions[] = {  /* This is mutable */
        0x90a0, //  0: pull   block        side 1
        0x7040, //  1: out    y, 32        side 1
        //     .wrap_target
        0x3083, //  2: wait   1 gpio, 3    side 1
        0xa422, //  3: mov    x, y         side 0 [4]
        0x2003, //  4: wait   0 gpio, 3    side 0
        0x12c2, //  5: jmp    pin, 2       side 1 [2]
        0x3083, //  6: wait   1 gpio, 3    side 1
        0xc442, //  7: irq    clear 2      side 0 [4]
        0x2003, //  8: wait   0 gpio, 3    side 0
        0x30c2, //  9: wait   1 irq, 2     side 1
        0x10d4, // 10: jmp    pin, 20      side 1
        0x3083, // 11: wait   1 gpio, 3    side 1
        0xa442, // 12: nop                 side 0 [4]
        0x2003, // 13: wait   0 gpio, 3    side 0
        0xd042, // 14: irq    clear 2      side 1
        0xd043, // 15: irq    clear 3      side 1
        0x30c2, // 16: wait   1 irq, 2     side 1
        0x20c3, // 17: wait   1 irq, 3     side 0
        0x1054, // 18: jmp    x--, 20      side 1
        0x1002, // 19: jmp    2            side 1
        0xd041, // 20: irq    clear 1      side 1
        0x3083, // 21: wait   1 gpio, 3    side 1
        0x20c1, // 22: wait   1 irq, 1     side 0
        0x104e, // 23: jmp    x--, 14      side 1
        //     .wrap
    };
    struct pio_program prog = {
        .instructions = instructions,
        .length = sizeof(instructions)/sizeof(instructions[0]),
        .origin = -1
    };
    pio_sm_config cfg = pio_get_default_sm_config();
    unsigned int i, offset;
    unsigned int tc[3];
    unsigned int sm = 0;
    pio_claim_sm_mask(pio, 1);

    /* Compute mid-line and broad-sync time constants and start the 3 "timer" SMs */
    tc[1] = 5.0e-7 * opt_period * (double)clock_get_hz(clk_sys);
    tc[0] = tc[1] - 1.0e-6 * opt_hsw * (double)clock_get_hz(clk_sys);
    tc[2] = tc[1] + tc[0];
    if (start_timers(pio, opt_hpos ? 0 : 1, tc) < 0) {
        pio_sm_unclaim(pio, sm);
        return -1;
    }

    /* Adapt program code according to CSync polarity; configure program */
    pio_sm_set_enabled(pio, sm, false);
    for (i = 0; i < prog.length; i++) {
        if (opt_cpos)
            instructions[i] ^= 0x1000;
        if (!opt_hpos && (instructions[i] & 0xe07f) == 0x2003)
            instructions[i] ^= 0x0080;
    }
    offset = pio_add_program(pio, &prog);
    if (offset == PIO_ORIGIN_ANY)
        return -1;

    /* Configure pins and SM; set VSync width; start the SM */
    sm_config_set_wrap(&cfg, offset + wrap_target, offset + wrap);
    sm_config_set_sideset(&cfg, 1, false, false);
    sm_config_set_sideset_pins(&cfg, opt_gpio);
    pio_gpio_init(pio, opt_gpio);
    sm_config_set_jmp_pin(&cfg, 2); /* DPI VSync "helper" signal is GPIO 2 */
    pio_sm_init(pio, sm, offset, &cfg);
    pio_sm_set_consecutive_pindirs(pio, sm, opt_gpio, 1, true);
    pio_sm_put(pio, sm, opt_vsw - 1);
    pio_sm_set_enabled(pio, sm, true);

    return 0;
}

/*
 * COMPOSITE SYNC (TV-STYLE) for 625/25i [-w 5] and 525/30i [-w 6] only.
 *
 * DPI VSYNC (GPIO2) must be a modified signal which is always active-low.
 * It should go low for 1 or 2 scanlines, VSyncWidth/2.0 or (VSyncWidth+1)/2.0
 * lines before Vsync-start, i.e. just after the last full active TV line
 * (noting that RP1 DPI does not generate half-lines).
 *
 * This will push the image up by 1 line compared to customary DRM timings in
 * "PAL" mode, or 2 lines in "NTSC" mode (which is arguably too low anyway),
 * but avoids a collision between an active line and an equalizing pulse.
 *
 * Another wrinkle is that when the first equalizing pulse aligns with HSync,
 * it becomes a normal-width sync pulse. This was a deliberate simplification.
 * It is unlikely that any TV will notice or care.
 */

static int setup_pio_for_csync_tv(PIO pio)
{
    static const int wrap_target = 6;
    static const int wrap = 27;
    unsigned short instructions[] = {  /* This is mutable */
        0x3703, //  0: wait  0 gpio, 3  side 1 [7] ; while (HSync) delay;
        0x3083, //  1: wait  1 gpio, 3  side 1     ; do { @HSync
        0xa7e6, //  2: mov   osr, isr   side 0 [7] ;   CSYNC: rewind sequence
        0x2003, //  3: wait  0 gpio, 3  side 0     ;   CSYNC: HSync->CSync
        0xb7e6, //  4: mov   osr, isr   side 1 [7] ;   delay
        0x10c1, //  5: jmp   pin, 1     side 1     ; } while (VSync)
        //     .wrap_target                        ; while (true) {
        0xd042, //  6: irq   clear 2    side 1     ;   flush stale IRQ
        0xd043, //  7: irq   clear 3    side 1     ;   flush stale IRQ
        0xb022, //  8: mov   x, y       side 1     ;   X = EQWidth - 3
        0x30c2, //  9: wait  1 irq, 2   side 1     ;   @midline
        0x004a, // 10: jmp   x--, 10    side 0     ;   CSYNC: while (x--) ;
        0x6021, // 11: out   x, 1       side 0     ;   CSYNC: next pulse broad?
        0x002e, // 12: jmp   !x, 14     side 0     ;   CSYNC: if (broad)
        0x20c3, // 13: wait  1 irq, 3   side 0     ;   CSYNC:   @BroadRight
        0x7021, // 14: out   x, 1       side 1     ;   sequence not finished?
        0x1020, // 15: jmp   !x, 0      side 1     ;   if (finished) break
        0xd041, // 16: irq   clear 1    side 1     ;   flush stale IRQ
        0xb022, // 17: mov   x, y       side 1     ;   X = EQWidth - 3
        0x3083, // 18: wait  1 gpio, 3  side 1     ;   @HSync
        0x0053, // 19: jmp   x--, 19    side 0     ;   CSYNC: while (x--) ;
        0x6021, // 20: out   x, 1       side 0     ;   CSYNC: next pulse broad?
        0x0037, // 21: jmp   !x, 23     side 0     ;   CSYNC: if (broad)
        0x20c1, // 22: wait  1 irq, 1   side 0     ;   CSYNC:  @BroadLeft
        0x7021, // 23: out   x, 1       side 1     ;   sequence not finished?
        0x1020, // 24: jmp   !x, 0      side 1     ;   if (finished) break
        0x10c6, // 25: jmp   pin, 6     side 1     ;   if (VSync) continue
        0xb0e6, // 26: mov   osr, isr   side 1     ;   rewind sequence
        0x7022, // 27: out   x, 2       side 1     ;   skip 2 bits
        //     .wrap                               ; }
    };
     struct pio_program prog = {
        .instructions = instructions,
        .length = sizeof(instructions)/sizeof(instructions[0]),
        .origin = -1
    };
    pio_sm_config cfg = pio_get_default_sm_config();
    unsigned int i, offset;
    unsigned int tc[3];
    unsigned int sm = 0;

    pio_claim_sm_mask(pio, 1);

    /* Compute mid-line and broad-sync time constants and start the 3 "timer" SMs */
    tc[1] = 5.0e-7 * opt_period * (double)clock_get_hz(clk_sys);
    tc[0] = tc[1] - 1.0e-6 * opt_hsw * (double)clock_get_hz(clk_sys);
    tc[2] = tc[1] + tc[0];
    if (start_timers(pio, opt_hpos ? 0 : 1, tc) < 0) {
        pio_sm_unclaim(pio, sm);
        return -1;
    }

    /* Adapt program code according to CSync polarity; configure program */
    pio_sm_set_enabled(pio, sm, false);
    for (i = 0; i < prog.length; i++) {
        if (opt_cpos)
            instructions[i] ^= 0x1000;
        if (!opt_hpos && (instructions[i] & 0xe07f) == 0x2003)
            instructions[i] ^= 0x0080;
    }
    offset = pio_add_program(pio, &prog);
    if (offset == PIO_ORIGIN_ANY)
        return -1;

    /* Configure pins and SM */
    sm_config_set_wrap(&cfg, offset + wrap_target, offset + wrap);
    sm_config_set_sideset(&cfg, 1, false, false);
    sm_config_set_sideset_pins(&cfg, opt_gpio);
    pio_gpio_init(pio, opt_gpio);
    sm_config_set_jmp_pin(&cfg, 2); /* DPI VSync "helper" signal is GPIO 2 */
    pio_sm_init(pio, sm, offset, &cfg);
    pio_sm_set_consecutive_pindirs(pio, sm, opt_gpio, 1, true);

    /* Load parameters (Vsync pattern; EQ pulse width) into ISR and Y */
    tc[0] = (unsigned)(1.0e-6 * opt_eqp * (double) clock_get_hz(clk_sys));
    pio_sm_put(pio, sm, (opt_vsw <= 5) ? 0x02ABFFAA : 0xAABFFEAA);
    pio_sm_put(pio, sm, tc[0] - 3);
    pio_sm_exec(pio, sm, pio_encode_pull(false, false));
    pio_sm_exec(pio, sm, pio_encode_out(pio_y, 32));
    pio_sm_exec(pio, sm, pio_encode_in(pio_y, 32));
    pio_sm_exec(pio, sm, pio_encode_pull(false, false));
    pio_sm_exec(pio, sm, pio_encode_out(pio_y, 32));
    pio_sm_set_enabled(pio, sm, true);

    /* Start the SM */
    pio_sm_set_enabled(pio, sm, true);

    return 0;
}

int main(int argc, const char **argv)
{
    int r = 0;

    if (getopts(argc, argv)) {
        const char * progname = (argc > 0 && argv[0]) ? argv[0] : "dpi_csync";
        usage(progname);
        return 1;
    }

    if (!opt_ilace)
        r = setup_pio_for_csync_prog(pio0);
    else if (opt_eqp <= 0 || opt_vsw < 5 || opt_vsw > 6)
        r = setup_pio_for_csync_ilace(pio0);
    else
        r = setup_pio_for_csync_tv(pio0);

    if (r) {
        fprintf(stderr, "PIO setup failed\n");
        return 1;
    }

    while (true)
        sleep_ms(1000);

    return 0;
}
