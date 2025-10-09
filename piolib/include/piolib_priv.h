// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2023-24 Raspberry Pi Ltd.
 * All rights reserved.
 */

#ifndef _PIOLIB_PRIV_H
#define _PIOLIB_PRIV_H

#include "pio_platform.h"

#define PIO_CHIP(name) name ## _pio_chip

#if LIBRARY_BUILD
#define DECLARE_PIO_CHIP(chip) \
    const PIO_CHIP_T chip ## _pio_chip =

extern const PIO_CHIP_T *const library_piochips[];
extern const int library_piochips_count;
#else
#define DECLARE_PIO_CHIP(chip) \
    const PIO_CHIP_T PIO_CHIP(chip); \
    const PIO_CHIP_T *__ptr_ ## chip __attribute__ ((section ("piochips"))) __attribute__ ((used)) = &PIO_CHIP(chip); \
    const PIO_CHIP_T PIO_CHIP(chip) =

extern const PIO_CHIP_T *__start_piochips;
extern const PIO_CHIP_T *__stop_piochips;
#endif

#endif
