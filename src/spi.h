/*
 * Copyright (c) 2026 Mikhail Anikin
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef _SPI_H_
#define _SPI_H_

#include <stdint.h>

int spi_init(void);
int spiop_transfer(uint8_t *buf, size_t slen, size_t rlen);
int spiop_set_freq(uint32_t freq);

#endif // _SPI_H_