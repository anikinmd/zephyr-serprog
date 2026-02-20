/*
 * Copyright (c) 2026 Mikhail Anikin
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <stdio.h>
#include <string.h>
#include "spi.h"

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(spi, LOG_LEVEL_INF);

#define SERPROG_SPI_NODE DT_ALIAS(serprog_spi)

struct spi_config spi_cfg = {
    .frequency = DT_PROP(SERPROG_SPI_NODE, clock_frequency),
    .operation = SPI_OP_MODE_MASTER | SPI_TRANSFER_MSB | SPI_WORD_SET(8),
    .cs = SPI_CS_CONTROL_INIT(DT_NODELABEL(serprog_flash), 1),
};


const struct device *spi;

int spiop_transfer(uint8_t *buf, size_t slen, size_t rlen)
{
    int ret;
    struct spi_config cfg_hold = spi_cfg;
    cfg_hold.operation |= SPI_HOLD_ON_CS;

    if (slen == 0 && rlen == 0)
    {
        return -1;
    }

    if (slen != 0)
    {
        struct spi_buf txb = {.buf = buf, .len = slen};
        struct spi_buf_set txs = {.buffers = &txb, .count = 1};
        ret = spi_write(spi, &cfg_hold, &txs);
        if (ret)
            goto exit;
    }
    if (rlen != 0)
    {
        struct spi_buf rxb = {.buf = buf, .len = rlen};
        struct spi_buf_set rxs = {.buffers = &rxb, .count = 1};
        ret = spi_read(spi, &cfg_hold, &rxs);
    }
exit:
    spi_release(spi, &cfg_hold);
    return ret;
}

int spi_init(void)
{
    spi = device_get_binding(DEVICE_DT_NAME(DT_NODELABEL(spi1)));
    if (!device_is_ready(spi))
    {
        LOG_ERR("Device SPI not ready, aborting");
        return -1;
    }
    return 0;
}

int spiop_set_freq(uint32_t freq)
{
    uint32_t max_freq = DT_PROP(DT_NODELABEL(spi1), clock_frequency);
    if (freq > max_freq)
        freq = max_freq;
    spi_cfg.frequency = freq;
    return 0;
}