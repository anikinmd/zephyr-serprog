/*
 * Copyright (c) 2026 Mikhail Anikin
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef _UART_H_
#define _UART_H_
#include <stdint.h>
#include <zephyr/kernel.h>

int uart_send_char(uint8_t c);
int uart_send_buf(uint8_t *buf, size_t len);
int uart_init_dev(void);


#endif // _UART_H_