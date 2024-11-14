/**
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <stdio.h>
#include <stdlib.h>

#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "genseq.pio.h"

#define DATA_WORDS 1024

int main(int argc, const char **argv) {
    uint32_t databuf[DATA_WORDS];
    bool use_dma = true;
    int ret = 0;
    int i, j;
    stdio_init_all();

    PIO pio = pio0;
    int sm = pio_claim_unused_sm(pio, true);
    uint offset = pio_add_program(pio, &genseq_program);
    uint gpio = 4;
    if (argc == 2)
        gpio = (uint)strtoul(argv[1], NULL, 0);
    printf("Loaded program at %d, using sm %d, gpio %d\n", offset, sm, gpio);

    pio_sm_config_xfer(pio, sm, PIO_DIR_FROM_SM, 256, 1);

    pio_gpio_init(pio, gpio);
    pio_sm_set_consecutive_pindirs(pio, sm, gpio, 1, true);
    pio_sm_config c = genseq_program_get_default_config(offset);
    sm_config_set_sideset_pins(&c, gpio);
    pio_sm_init(pio, sm, offset, &c);
    pio_sm_set_enabled(pio, sm, true);

    for (i = 0; i < DATA_WORDS; i++) {
        printf("Iter %d:\n", i);
        pio_sm_put_blocking(pio, sm, i);
        if (use_dma) {
            ret = pio_sm_xfer_data(pio, sm, PIO_DIR_FROM_SM, (i + 1) * sizeof(databuf[0]), databuf);
            if (ret)
               break;

            for (j = i; j >= 0; j--)
            {
                int v = databuf[i - j];
                if (v != j)
                    printf(" %d: %d\n", j, v);
            }
        } else {
            for (j = i; j >= 0; j--)
            {
                int v = pio_sm_get_blocking(pio, sm);
                if (v != j)
                    printf(" %d: %d\n", j, v);
            }
        }
        sleep_ms(10);
    }

    if (ret)
        printf("* error %d\n", ret);
    return ret;
}
