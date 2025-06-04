#include "gpiochip.h"
#include "util.h"

#define GPIO_CHIP(name) name ## _chip
#define EXTERN_GPIO_CHIP(name) extern const GPIO_CHIP_T GPIO_CHIP(name)

EXTERN_GPIO_CHIP(bcm2835);
EXTERN_GPIO_CHIP(bcm2711);
EXTERN_GPIO_CHIP(bcm2712);
EXTERN_GPIO_CHIP(bcm2712_aon);
EXTERN_GPIO_CHIP(bcm2712c0);
EXTERN_GPIO_CHIP(bcm2712c0_aon);
EXTERN_GPIO_CHIP(bcm2712d0);
EXTERN_GPIO_CHIP(bcm2712d0_aon);
EXTERN_GPIO_CHIP(brcmstb);
EXTERN_GPIO_CHIP(rp1);
EXTERN_GPIO_CHIP(firmware);

const GPIO_CHIP_T *const library_gpiochips[] =
{
    &GPIO_CHIP(bcm2835),
    &GPIO_CHIP(bcm2711),
    &GPIO_CHIP(bcm2712),
    &GPIO_CHIP(bcm2712_aon),
    &GPIO_CHIP(bcm2712c0),
    &GPIO_CHIP(bcm2712c0_aon),
    &GPIO_CHIP(bcm2712d0),
    &GPIO_CHIP(bcm2712d0_aon),
    &GPIO_CHIP(brcmstb),
    &GPIO_CHIP(rp1),
    &GPIO_CHIP(firmware),
};

const int library_gpiochips_count = ARRAY_SIZE(library_gpiochips);
