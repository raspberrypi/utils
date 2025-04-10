#include "gpiochip.h"
#include "util.h"

#define GPIO_CHIP(name) name ## _chip
#define EXTERN_GPIO_CHIP(name) extern const GPIO_CHIP_T GPIO_CHIP(name)

EXTERN_GPIO_CHIP(bcm2835);
EXTERN_GPIO_CHIP(bcm2711);
EXTERN_GPIO_CHIP(bcm2712);
EXTERN_GPIO_CHIP(rp1);

const GPIO_CHIP_T *const library_gpiochips[] =
{
    &GPIO_CHIP(bcm2835),
    &GPIO_CHIP(bcm2711),
    &GPIO_CHIP(bcm2712),
    &GPIO_CHIP(rp1),
};

const int library_gpiochips_count = ARRAY_SIZE(library_gpiochips);
