/*
 * Copyright (c) 2026 Mikhail Anikin
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <sample_usbd.h>

#include <stdio.h>
#include <string.h>

#include <zephyr/logging/log.h>

#include "serprog.h"
#include "spi.h"
#include "uart.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

#define RX_MSGQ_LEN 1024
K_MSGQ_DEFINE(rx_msgq, sizeof(uint8_t), RX_MSGQ_LEN, 4);

static void worker_thread(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	uint8_t c;

	while (1)
	{
		k_msgq_get(&rx_msgq, &c, K_FOREVER);
		process_sp_char(c);
	}
}

K_THREAD_DEFINE(cdc_worker_tid,
				1024,
				worker_thread,
				NULL, NULL, NULL,
				5,
				0,
				0);

int main(void)
{
	int ret;

	ret = spi_init();
	if (ret)
	{
		LOG_ERR("Failed to enable SPI, ret=%d", ret);
		return ret;
	}

	ret = uart_init_dev();
	if (ret != 0)
	{
		LOG_ERR("Failed to init uart cdc, ret %d", ret);
		return 0;
	}
	return 0;
}
