/**
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <stdio.h>
#include <stdlib.h>

#include "piolib.h"

#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "echo.pio.h"
#include "genseq.pio.h"

#define DATA_WORDS 16

int main(int argc, const char **argv) {
    uint32_t databuf[DATA_WORDS];
    bool use_dma = true;
    int ret = 0;
    int i, j;
    uint16_t dummy_program_instructions[1];
    struct pio_program dummy_program = {
        .instructions = dummy_program_instructions,
        .length = 1,
        .origin = -1,
    };
    pio_sm_config c;
    PIO pio = pio0;
    uint sm;
    uint offset;
    uint gpio = 4;
    if (argc == 2)
        gpio = (uint)strtoul(argv[1], NULL, 0);

    pio_enable_fatal_errors(pio, false);

    for (sm = 0; sm < pio_get_sm_count(pio); sm++) {
        if (pio_sm_is_claimed(pio, sm))
            pio_panic("SM claimed (1)");

        pio_sm_claim(pio, sm);
        if (pio_get_error(pio))
            pio_panic("SM claim failed");

        if (!pio_sm_is_claimed(pio, sm))
            pio_panic("SM not claimed (1)");

        pio_sm_claim(pio, sm);
        if (!pio_get_error(pio))
            pio_panic("SM not claimed (2)");
        pio_clear_error(pio);
    }

    for (sm = 0; sm < pio_get_sm_count(pio); sm++) {
        pio_sm_unclaim(pio, sm);
        if (pio_sm_is_claimed(pio, sm))
            pio_panic("SM still claimed");
    }

    pio_claim_sm_mask(pio, (1 << (pio_get_sm_count(pio) - 1)) - 1);
    if (pio_get_error(pio))
        pio_panic("masked claim failed");
    if ((uint)pio_claim_unused_sm(pio, false) != pio_get_sm_count(pio) - 1)
        pio_panic("wrong SM (expected the last)");

    for (sm = 0; sm < pio_get_sm_count(pio); sm++) {
        pio_sm_unclaim(pio, sm);
        if (pio_sm_is_claimed(pio, sm))
            pio_panic("SM still claimed");
    }

    sm = pio_claim_unused_sm(pio, true);

    for (offset = 0; offset < pio_get_instruction_count(pio); offset++) {
        dummy_program_instructions[0] = offset + 1;
        if (!pio_can_add_program(pio, &dummy_program))
            pio_panic("can't add program");
        if (pio_add_program(pio, &dummy_program) == PIO_ORIGIN_ANY)
            pio_panic("failed to add program (1)");
    }

    dummy_program_instructions[0] = offset + 1;

    if (pio_can_add_program(pio, &dummy_program))
        pio_panic("can add program");
    if (pio_add_program(pio, &dummy_program) != PIO_ORIGIN_INVALID)
        pio_panic("added program again");
    pio_clear_error(pio);

    for (offset = 0; offset < pio_get_instruction_count(pio); offset++) {
        pio_remove_program(pio, &dummy_program, offset);
        if (pio_get_error(pio))
            pio_panic("remove program failed");
        pio_remove_program(pio, &dummy_program, offset);
        if (!pio_get_error(pio))
            pio_panic("removed program again");
        pio_clear_error(pio);
    }

    for (offset = 0; offset < pio_get_instruction_count(pio); offset++) {
        dummy_program_instructions[0] = offset + 1;
        if (!pio_can_add_program_at_offset(pio, &dummy_program, offset))
            pio_panic("can't add program at offset");
        pio_add_program_at_offset(pio, &dummy_program, offset);
    }

    for (offset = 0; offset < pio_get_instruction_count(pio); offset++) {
        pio_remove_program(pio, &dummy_program, offset);
    }

    offset = pio_add_program(pio, &echo_program);
    c = pio_get_default_sm_config();
    sm_config_set_wrap(&c, offset + echo_wrap_target, offset + echo_wrap);

    pio_sm_init(pio, sm, offset, &c);

    pio_sm_put(pio, sm, 0);

    if (pio_sm_is_tx_fifo_empty(pio, sm))
        pio_panic("TX FIFO is empty (1)");

    pio_sm_clear_fifos(pio, sm);

    if (!pio_sm_is_tx_fifo_empty(pio, sm))
        pio_panic("TX FIFO is not empty (1)");
    if (pio_sm_get_tx_fifo_level(pio, sm) != 0)
        pio_panic("TX FIFO level is not zero");
    for (i = 1; i <= (int)pio_get_fifo_depth(pio); i++)
    {
        if (pio_sm_is_tx_fifo_full(pio, sm))
            pio_panic("TX FIFO is full (1)");
        pio_sm_put(pio, sm, i);
        if (pio_sm_is_tx_fifo_empty(pio, sm))
            pio_panic("TX FIFO is empty (2)");
        if ((int)pio_sm_get_tx_fifo_level(pio, sm) != i)
            pio_panic("Wrong TX FIFO level");
    }

    if (!pio_sm_is_tx_fifo_full(pio, sm))
        pio_panic("TX FIFO is not full (1)");

    pio_sm_drain_tx_fifo(pio, sm);

    if (!pio_sm_is_tx_fifo_empty(pio, sm))
        pio_panic("TX FIFO is not empty (2)");

    for (i = 1; i <= (int)pio_get_fifo_depth(pio); i++)
        pio_sm_put(pio, sm, i);

    if (!pio_sm_is_tx_fifo_full(pio, sm))
        pio_panic("TX FIFO is not full (2)");

    if (!pio_sm_is_rx_fifo_empty(pio, sm))
        pio_panic("RX FIFO not empty");

    if ((int)pio_sm_get_rx_fifo_level(pio, sm) != 0)
        pio_panic("RX FIFO level not 0");

    pio_sm_set_enabled(pio, sm, true);

    if (!pio_sm_is_tx_fifo_empty(pio, sm))
        pio_panic("TX FIFO is not empty (3)");

    if (!pio_sm_is_rx_fifo_full(pio, sm))
        pio_panic("RX FIFO is not full");

    if (pio_sm_get_rx_fifo_level(pio, sm) != pio_get_fifo_depth(pio))
        pio_panic("RX FIFO level not the maximum");

    for (i = pio_get_fifo_depth(pio) - 1; i >= 0; i--)
    {
        if (pio_sm_is_rx_fifo_empty(pio, sm))
            pio_panic("RX FIFO is empty");
        if (pio_sm_get(pio, sm) != (uint)(pio_get_fifo_depth(pio) - i))
            pio_panic("wrong RX data");
        if ((int)pio_sm_get_rx_fifo_level(pio, sm) != i)
            pio_panic("wrong RX FIFO level");
    }

    if (!pio_sm_is_rx_fifo_empty(pio, sm))
        pio_panic("RX FIFO is not empty");

    offset = pio_add_program(pio, &genseq_program);

    if (!pio_sm_config_xfer(pio, sm, PIO_DIR_FROM_SM, 4096, 0))
        pio_panic("DMA configuration didn't fail");
    if (!pio_sm_xfer_data(pio, sm, PIO_DIR_FROM_SM, 4, NULL) ||
        !pio_sm_xfer_data(pio, sm, PIO_DIR_FROM_SM, 0, databuf))
        pio_panic("DMA transfer didn't fail");
    if (pio_sm_config_xfer(pio, sm, PIO_DIR_FROM_SM, 4096, 2))
        pio_panic("DMA configuration failed");

    pio_gpio_init(pio, gpio);
    pio_sm_set_consecutive_pindirs(pio, sm, gpio, 1, true);
    c = genseq_program_get_default_config(offset);
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

    if (!ret)
    {
        const uint32_t words = 0x10000;
        uint32_t *bigbuf = malloc(words * sizeof(bigbuf[0]));

        pio_sm_put_blocking(pio, sm, words - 1);
        ret = pio_sm_xfer_data(pio, sm, PIO_DIR_FROM_SM, words * sizeof(bigbuf[0]), bigbuf);
        if (!ret) {
            for (i = words - 1; i >= 0; i--)
            {
                int v = bigbuf[words - 1 - i];
                if (v != i)
                    printf(" %x: %x\n", i, v);
            }
        }
        free(bigbuf);
    }

    if (ret)
        printf("* error %d\n", ret);
    return ret;
}
