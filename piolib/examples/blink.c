/**
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <stdio.h>
#include <stdlib.h>

#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "blink.pio.h"

int main(int argc, const char **argv) {
    int ret = 0;
    int i;
    stdio_init_all();

    PIO pio = pio0;
    int sm = pio_claim_unused_sm(pio, true);
    uint offset = pio_add_program(pio, &blink_program);
    uint gpio = 4;

    if (argc == 2)
        gpio = (uint)strtoul(argv[1], NULL, 0);
    printf("Loaded program at %d, using sm %d, gpio %d\n", offset, sm, gpio);

    pio_gpio_init(pio, gpio);
    pio_sm_set_consecutive_pindirs(pio, sm, gpio, 1, true);
    pio_sm_config c = blink_program_get_default_config(offset);
    sm_config_set_clkdiv_int_frac(&c, 20000, 0);
    sm_config_set_out_pin_base(&c, gpio);
    pio_sm_init(pio, sm, offset, &c);
    pio_sm_set_enabled(pio, sm, true);

    for (i = 0; i < 20; i++) {
        const char *spinner = "|/-\\";
        pio_sm_put_blocking(pio, sm, (i & 1) ? 0 : ~0);
        printf("\r%c", spinner[i % 4]);
        fflush(stdout);
        sleep_ms(100);
    }
    printf("\n");

    if (ret)
        printf("* error %d\n", ret);
    return ret;
}
