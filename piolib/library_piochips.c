// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2025 Raspberry Pi Ltd.
 * All rights reserved.
 */

#include "piolib.h"
#include "piolib_priv.h"

#define ARRAY_SIZE(a) (sizeof(a)/sizeof(a[0]))

#define EXTERN_PIO_CHIP(name) extern const PIO_CHIP_T PIO_CHIP(name)

EXTERN_PIO_CHIP(rp1);

const PIO_CHIP_T *const library_piochips[] =
{
    &PIO_CHIP(rp1),
};

const int library_piochips_count = ARRAY_SIZE(library_piochips);
