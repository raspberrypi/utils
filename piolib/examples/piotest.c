#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "piolib.h"
#include "ws2812.pio.h"

int main(int argc, const char **argv)
{
    PIO pio;
    int sm;
    uint offset;
    const uint pixels = 60;
    uint8_t databuf[pixels * 4];
    uint gpio = 2;
    int pass;
    uint i;

    if (argc == 2)
        gpio = (uint)strtoul(argv[1], NULL, 0);
    pio = pio0;
    sm = pio_claim_unused_sm(pio, true);
    pio_sm_config_xfer(pio, sm, PIO_DIR_TO_SM, 256, 1);

    offset = pio_add_program(pio, &ws2812_program);
    printf("Loaded program at %d, using sm %d, gpio %d\n", offset, sm, gpio);

    pio_sm_clear_fifos(pio, sm);
    pio_sm_set_clkdiv(pio, sm, 1.0);
    ws2812_program_init(pio, sm, offset, gpio, 800000.0, false);

    pass = 0;
    while (1) {
        for (i = 0; i < pixels; i++)
        {
            if (i == (pass % pixels))
            {
                databuf[4*i + 0] = 0;
                databuf[4*i + 1] = 255;
                databuf[4*i + 2] = 255;
                databuf[4*i + 3] = 255;
            }
            else
            {
                int led = i % 3;
                databuf[4*i + 0] = 0;
                databuf[4*i + 1] = (led == 0) ? 255 : 0;
                databuf[4*i + 2] = (led == 1) ? 255 : 0;
                databuf[4*i + 3] = (led == 2) ? 255 : 0;
            }
        }
        pio_sm_xfer_data(pio, sm, PIO_DIR_TO_SM, sizeof(databuf), databuf);
        sleep_ms(10);
        pass++;
    }

    return 0;
}
