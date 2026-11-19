/**
 * Copyright (c) 2026 Raspberry Pi Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * mirror: use a PIO state machine to continuously copy the levels of one
 * range of GPIOs (the "source" pins, typically wired to some other hardware)
 * onto another range of GPIOs (the "mirror" pins), so that a logic analyser
 * connected to the mirror pins can snoop on the source pins.
 *
 * The state machine runs a single instruction - "mov pins, pins" - in a tight
 * loop, so the mirror pins follow the source pins with a latency of one PIO
 * clock cycle (plus the input/output synchroniser delays).
 *
 * The source pins are not reconfigured; they are sampled as-is, whatever
 * function they are currently muxed to.
 */

#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "mirror.pio.h"

static volatile bool stop;

static void handle_signal(int sig)
{
    (void)sig;
    stop = true;
}

static void usage(const char *prog)
{
    fprintf(stderr,
            "Usage: %s <src_base> <dst_base> <count> [clkdiv]\n"
            "  src_base  first GPIO of the range to snoop on\n"
            "  dst_base  first GPIO of the range to drive with a copy\n"
            "  count     number of GPIOs in each range (1-32)\n"
            "  clkdiv    integer PIO clock divider (default 1)\n"
            "The two ranges must not overlap.\n",
            prog);
    exit(1);
}

int main(int argc, const char **argv)
{
    uint src_base, dst_base, count, clkdiv = 1;
    uint i;

    if (argc < 4 || argc > 5)
        usage(argv[0]);

    src_base = (uint)strtoul(argv[1], NULL, 0);
    dst_base = (uint)strtoul(argv[2], NULL, 0);
    count = (uint)strtoul(argv[3], NULL, 0);
    if (argc == 5)
        clkdiv = (uint)strtoul(argv[4], NULL, 0);

    if (count < 1 || count > 32) {
        fprintf(stderr, "count must be between 1 and 32\n");
        usage(argv[0]);
    }
    if (clkdiv < 1 || clkdiv > 65535) {
        fprintf(stderr, "clkdiv must be between 1 and 65535\n");
        usage(argv[0]);
    }
    if (src_base < dst_base + count && dst_base < src_base + count) {
        fprintf(stderr, "source and destination ranges overlap\n");
        usage(argv[0]);
    }

    stdio_init_all();

    PIO pio = pio0;
    int sm = pio_claim_unused_sm(pio, true);
    uint offset = pio_add_program(pio, &mirror_program);

    printf("Loaded program at %d, using sm %d\n", offset, sm);
    printf("Mirroring GPIOs %u-%u onto GPIOs %u-%u, clkdiv %u\n",
           src_base, src_base + count - 1,
           dst_base, dst_base + count - 1, clkdiv);

    // Only the mirror (output) pins are handed over to the PIO. The source
    // pins are left alone so as not to disturb the hardware they're wired to.
    for (i = 0; i < count; i++)
        pio_gpio_init(pio, dst_base + i);
    pio_sm_set_consecutive_pindirs(pio, sm, dst_base, count, true);

    pio_sm_config c = mirror_program_get_default_config(offset);
    sm_config_set_in_pin_base(&c, src_base);
    sm_config_set_out_pins(&c, dst_base, count);
    sm_config_set_clkdiv_int_frac(&c, clkdiv, 0);
    pio_sm_init(pio, sm, offset, &c);
    pio_sm_set_enabled(pio, sm, true);

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    printf("Running - press Ctrl-C to stop\n");
    while (!stop)
        sleep_ms(100);

    // Tidy up: stop the state machine and release the output pins.
    pio_sm_set_enabled(pio, sm, false);
    pio_sm_set_consecutive_pindirs(pio, sm, dst_base, count, false);
    pio_remove_program(pio, &mirror_program, offset);
    pio_sm_unclaim(pio, sm);

    printf("\nStopped\n");
    return 0;
}
