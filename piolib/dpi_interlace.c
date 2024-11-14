/**
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"

// timings for the shorter of the two fields
// These are Argon-style timings where sync overlaps BP
// Note that vertical datum is delayed by about 3 lines,
// so some of the vertical front porch is here in VBP.

#define LINES      287 // Both "half-lines" deleted, leaving 2*287 = 574 lines
#define LINE_WIDTH 720
#define HBP        132 // includes HSYNC
#define HFP         12
#define VBP         25 // includes EQ pulses and VSYNC
#define VFP          0 // this will be increased for every other field

// Special sync widths to help PIO
#define HSYNC_WIDTH ((HBP + LINE_WIDTH + HFP) >> 1)
#define VSYNC_WIDTH (VBP + LINES + VFP - 1)

#define DMA_STRIDE 8 // for a 2-field diagonal pattern
#define PORCH_EN     ((8*!!HFP)+(4*!!VFP)+(2*!!HBP)+!!VBP)
#define IMASK 0x3ff
#define OMASK 0x3fc // mask out untestable bits (anticipating PRJY-1921). Only 3x8 bits emerge from VideoOut, so this is no loss.
#define SHIFT_R 9
#define SHIFT_G 19
#define SHIFT_B 29

#define BPP 4 // Bytes per pixel

#define dpi_ncsync_tv_wrap_target 12
#define dpi_ncsync_tv_wrap 31

static const uint16_t dpi_ncsync_tv_program_instructions[] = {
    0x3083, //  0: wait   1 gpio, 3       side 1     
    0xa122, //  1: mov    x, y            side 0 [1] 
    0x0142, //  2: jmp    x--, 2          side 0 [1] 
    0x3003, //  3: wait   0 gpio, 3       side 1     
    0x13c0, //  4: jmp    pin, 0          side 1 [3] 
    0xb0e6, //  5: mov    osr, isr        side 1     
    0xb022, //  6: mov    x, y            side 1     
    0x3083, //  7: wait   1 gpio, 3       side 1     
    0x0048, //  8: jmp    x--, 8          side 0     
    0xa022, //  9: mov    x, y            side 0     
    0x00df, // 10: jmp    pin, 31         side 0     
    0x004b, // 11: jmp    x--, 11         side 0     
            //     .wrap_target
    0x7021, // 12: out    x, 1            side 1     
    0x3003, // 13: wait   0 gpio, 3       side 1     
    0x0031, // 14: jmp    !x, 17          side 0     
    0xa022, // 15: mov    x, y            side 0     
    0x0a50, // 16: jmp    x--, 16         side 0 [10]
    0xa022, // 17: mov    x, y            side 0     
    0x0052, // 18: jmp    x--, 18         side 0     
    0x7021, // 19: out    x, 1            side 1     
    0x1020, // 20: jmp    !x, 0           side 1     
    0x7021, // 21: out    x, 1            side 1     
    0x3083, // 22: wait   1 gpio, 3       side 1     
    0x003a, // 23: jmp    !x, 26          side 0     
    0xa022, // 24: mov    x, y            side 0     
    0x0a59, // 25: jmp    x--, 25         side 0 [10]
    0xa022, // 26: mov    x, y            side 0     
    0x005b, // 27: jmp    x--, 27         side 0     
    0x7021, // 28: out    x, 1            side 1     
    0x1023, // 29: jmp    !x, 3           side 1     
    0x100c, // 30: jmp    12              side 1     
    0x7022, // 31: out    x, 2            side 1     
            //     .wrap
};

static const struct pio_program dpi_ncsync_tv_program_program = {
    .instructions = dpi_ncsync_tv_program_instructions,
    .length = 32,
    .origin = 0,
};

static inline pio_sm_config dpi_ncsync_tv_program_get_default_config(uint offset) {
    pio_sm_config c = pio_get_default_sm_config();
    sm_config_set_wrap(&c, offset + dpi_ncsync_tv_wrap_target, offset + dpi_ncsync_tv_wrap);
    sm_config_set_sideset(&c, 1, false, false);
    return c;
}

static void setup_pio_for_ncsync_tv(uint32_t line_rate, // normally 15625 Hz
                                    bool ntsc)
{
    PIO pio = pio0;
    uint sm = pio_claim_unused_sm(pio, true);
    uint offset = pio_add_program(pio, &dpi_ncsync_tv_program_program);
    uint32_t core_clk;
    uint32_t n;

    pio_sm_config c = dpi_ncsync_tv_program_get_default_config(offset);

    sm_config_set_sideset_pins(&c, 1);
    sm_config_set_jmp_pin(&c, 2); // GPIO 2 (nVSYNC)

    pio_gpio_init(pio, 1);
    pio_sm_init(pio, sm, offset, &c);

    pio_sm_set_consecutive_pindirs(pio, sm, 1, 1, true);

    core_clk = clock_get_hz(clk_sys);

    // set narrow pulse to 2.34us, normal = 2 * narrow, broad = 12 * narrow
    n = (2 * core_clk) / line_rate;
    n = ((75 * n + 2048) >> 12) - 2;

    pio_sm_put(pio, sm, ntsc ? 0xAAFFFAAA : 0x0AAFFEAA);
    pio_sm_put(pio, sm, n);
    pio_sm_exec(pio, sm, pio_encode_pull(false, false));
    pio_sm_exec(pio, sm, pio_encode_out(pio_y, 32));
    pio_sm_exec(pio, sm, pio_encode_in(pio_y, 32));
    pio_sm_exec(pio, sm, pio_encode_pull(false, false));
    pio_sm_exec(pio, sm, pio_encode_out(pio_y, 32));
    pio_sm_set_enabled(pio, sm, true);
}

int main(int argc, const char **argv)
{
    bool ntsc = false;
    int argn = 1;

    while (argn < argc && argv[argn][0] == '-') {
        const char *arg = argv[argn++];
        if (!strcmp(arg, "-n") || !strcmp(arg, "--ntsc") ) {
            ntsc = true;
        } else {
            printf("* unknown option '%s'\n", arg);
            return 1;
        }
    }

    setup_pio_for_ncsync_tv(15625, ntsc);

    while (true)
        sleep_ms(1000);

    return 0;
}
