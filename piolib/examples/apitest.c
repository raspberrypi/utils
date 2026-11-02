/**
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "piolib.h"

#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "echo.pio.h"
#include "genseq.pio.h"

#define DATA_WORDS 16

static void check(const char *label, bool value) {
    if (!value)
        pio_panic(label);
}

int main(int argc, const char **argv) {
    uint32_t databuf[DATA_WORDS];
    bool use_dma = true;
    int ret = 0;
    int i, j;
    uint32_t temp;
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
        check("SM claimed (1)", !pio_sm_is_claimed(pio, sm));

        pio_sm_claim(pio, sm);
        check("SM claim failed", !pio_get_error(pio));

        check("SM not claimed (1)", pio_sm_is_claimed(pio, sm));

        pio_sm_claim(pio, sm);
        check("SM not claimed (2)", pio_get_error(pio));
        pio_clear_error(pio);
    }

    for (sm = 0; sm < pio_get_sm_count(pio); sm++) {
        pio_sm_unclaim(pio, sm);
        check("SM still claimed", !pio_sm_is_claimed(pio, sm));
    }

    pio_claim_sm_mask(pio, (1 << (pio_get_sm_count(pio) - 1)) - 1);
    check("masked claim failed", !pio_get_error(pio));
    check("wrong SM (expected the last)",
          (uint)pio_claim_unused_sm(pio, false) == pio_get_sm_count(pio) - 1);

    for (sm = 0; sm < pio_get_sm_count(pio); sm++) {
        pio_sm_unclaim(pio, sm);
        check("SM still claimed", !pio_sm_is_claimed(pio, sm));
    }

    sm = pio_claim_unused_sm(pio, true);

    for (offset = 0; offset < pio_get_instruction_count(pio); offset++) {
        dummy_program_instructions[0] = offset + 1;
        check("can't add program", pio_can_add_program(pio, &dummy_program));
        check("failed to add program (1)",
              pio_add_program(pio, &dummy_program) != PIO_ORIGIN_ANY);
        check("can't add program again", pio_can_add_program(pio, &dummy_program));
    }

    dummy_program_instructions[0] = offset + 1;

    check("can add program", !pio_can_add_program(pio, &dummy_program));
    check("added program again",
          pio_add_program(pio, &dummy_program) == PIO_ORIGIN_INVALID);
    pio_clear_error(pio);

    for (offset = 0; offset < pio_get_instruction_count(pio); offset++) {
        pio_remove_program(pio, &dummy_program, offset);
        check("remove program failed", !pio_get_error(pio));
        pio_remove_program(pio, &dummy_program, offset);
        check("removed program again", pio_get_error(pio));
        pio_clear_error(pio);
    }

    for (offset = 0; offset < pio_get_instruction_count(pio); offset++) {
        dummy_program_instructions[0] = offset + 1;
        check("can't add program at offset",
              pio_can_add_program_at_offset(pio, &dummy_program, offset));
        pio_add_program_at_offset(pio, &dummy_program, offset);
    }

    for (offset = 0; offset < pio_get_instruction_count(pio); offset++) {
        pio_remove_program(pio, &dummy_program, offset);
    }

    // V2 additions
    {
        pio_sm_config c1 = pio_get_default_sm_config();
        pio_sm_config c2 = pio_get_default_sm_config();
        sm_config_set_out_pins(&c1, 1, 2);
        sm_config_set_out_pin_base(&c2, 1);
        sm_config_set_out_pin_count(&c2, 2);
        check("sm_config_set_out_pins", !memcmp(&c1, &c2, sizeof(c1)));
        sm_config_set_in_pins(&c1, 3);
        sm_config_set_in_pin_base(&c2, 3);
        sm_config_set_in_pin_count(&c2, 32);
        check("sm_config_set_in_pins", !memcmp(&c1, &c2, sizeof(c1)));
        sm_config_set_set_pins(&c1, 4, 5);
        sm_config_set_set_pin_base(&c2, 4);
        sm_config_set_set_pin_count(&c2, 5);
        check("sm_config_set_set_pins", !memcmp(&c1, &c2, sizeof(c1)));
        sm_config_set_sideset_pins(&c1, 6);
        sm_config_set_sideset_pin_base(&c2, 6);
        check("sm_config_set_sideset_pins", !memcmp(&c1, &c2, sizeof(c1)));
    }

    offset = pio_add_program(pio, &echo_program);
    c = pio_get_default_sm_config();
    sm_config_set_wrap(&c, offset + echo_wrap_target, offset + echo_wrap);

    pio_sm_init(pio, sm, offset, &c);

    pio_sm_put(pio, sm, 0);

    check("TX FIFO is empty (1)", !pio_sm_is_tx_fifo_empty(pio, sm));

    pio_sm_clear_fifos(pio, sm);
    pio_sm_clear_flags(pio, sm, PIO_SM_FLAG_ALL);

    check("TX FIFO is not empty (1)", pio_sm_is_tx_fifo_empty(pio, sm));
    check("TX FIFO level is not zero", pio_sm_get_tx_fifo_level(pio, sm) == 0);
    check("FIFO flags are non-zero", pio_sm_get_flags(pio, sm, PIO_SM_FLAG_ALL, false) == 0);
    pio_sm_get(pio, sm);
    check("FIFO flags not RXUNDER",
          pio_sm_get_flags(pio, sm, PIO_SM_FLAG_ALL, true) == PIO_SM_FLAG_RXUNDER);

    printf("%d: %08x\n", 0, pio_sm_get_dmactrl(pio, sm, true));

    temp = pio_sm_get_dmactrl(pio, sm, true);
    pio_sm_set_dmactrl(pio, sm, true, (temp & ~0x1f) | (pio_get_fifo_depth(pio) - 2));

    for (i = 1; i <= (int)pio_get_fifo_depth(pio); i++)
    {
        // Set the DREQ threshold to deassert when the FIFO level is i
        pio_sm_set_dmactrl(pio, sm, true, (temp & ~0x1f) | (i - 1));
        printf("%d: %08x\n", i, pio_sm_get_dmactrl(pio, sm, true));
        check("TX FIFO is full (1)", !pio_sm_is_tx_fifo_full(pio, sm));
        pio_sm_put(pio, sm, i);
        check("TX FIFO is empty (2)", !pio_sm_is_tx_fifo_empty(pio, sm));
        check("Wrong TX FIFO level", (int)pio_sm_get_tx_fifo_level(pio, sm) == i);
        printf(" -> %08x\n", pio_sm_get_dmactrl(pio, sm, true));
    }

    check("TX FIFO is not full (1)", pio_sm_is_tx_fifo_full(pio, sm));

    pio_sm_drain_tx_fifo(pio, sm);

    check("TX FIFO is not empty (2)", pio_sm_is_tx_fifo_empty(pio, sm));

    for (i = 1; i <= (int)pio_get_fifo_depth(pio); i++)
        pio_sm_put(pio, sm, i);

    check("TX FIFO is not full (2)", pio_sm_is_tx_fifo_full(pio, sm));

    check("RX FIFO not empty", pio_sm_is_rx_fifo_empty(pio, sm));

    check("RX FIFO level not 0", (int)pio_sm_get_rx_fifo_level(pio, sm) == 0);

    pio_sm_set_enabled(pio, sm, true);

    check("TX FIFO is not empty (3)", pio_sm_is_tx_fifo_empty(pio, sm));

    check("RX FIFO is not full", pio_sm_is_rx_fifo_full(pio, sm));

    check("RX FIFO level not the maximum",
          pio_sm_get_rx_fifo_level(pio, sm) == pio_get_fifo_depth(pio));

    temp = pio_sm_get_dmactrl(pio, sm, false);
    for (i = pio_get_fifo_depth(pio) - 1; i >= 0; i--)
    {
        // Set the DREQ threshold to deassert when the FIFO level is i
        pio_sm_set_dmactrl(pio, sm, false, (temp & ~0x1f) | (i + 1));
        printf("%d: %08x\n", i, pio_sm_get_dmactrl(pio, sm, false));
        check("RX FIFO is empty", !pio_sm_is_rx_fifo_empty(pio, sm));
        check("wrong RX data",
              pio_sm_get(pio, sm) == (uint)(pio_get_fifo_depth(pio) - i));
        check("wrong RX FIFO level", (int)pio_sm_get_rx_fifo_level(pio, sm) == i);
        printf(" -> %08x\n", pio_sm_get_dmactrl(pio, sm, false));
    }

    check("RX FIFO is not empty", pio_sm_is_rx_fifo_empty(pio, sm));

    offset = pio_add_program(pio, &genseq_program);

    check("DMA configuration didn't fail",
          pio_sm_config_xfer(pio, sm, PIO_DIR_FROM_SM, 4096, 0));
    check("DMA transfer didn't fail",
          pio_sm_xfer_data(pio, sm, PIO_DIR_FROM_SM, 4, NULL) &&
          pio_sm_xfer_data(pio, sm, PIO_DIR_FROM_SM, 0, databuf));
    check("DMA configuration failed",
          !pio_sm_config_xfer(pio, sm, PIO_DIR_FROM_SM, 4096, 2));

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
