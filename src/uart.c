/*
 * Copyright (c) 2026 Mikhail Anikin
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "uart.h"
#include <zephyr/drivers/uart.h>
#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>
#include <zephyr/sys/ring_buffer.h>
#include <zephyr/usb/usb_device.h>
#include <zephyr/usb/usbd.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(uart_cdc, LOG_LEVEL_INF);

const struct device *const uart_dev = DEVICE_DT_GET_ONE(zephyr_cdc_acm_uart);
extern struct k_msgq rx_msgq;

#define TX_RING_BUF_SIZE 1024
RING_BUF_DECLARE(tx_ringbuf, TX_RING_BUF_SIZE);

static struct k_sem tx_space_sem;
static bool tx_drain_ringbuf(const struct device *dev)
{
    uint8_t *data;
    uint32_t len = ring_buf_get_claim(&tx_ringbuf, &data, UINT32_MAX);

    if (len == 0)
    {
        return false;
    }

    int wrote = uart_fifo_fill(dev, data, len);

    if (wrote < 0)
    {
        wrote = 0;
    }

    ring_buf_get_finish(&tx_ringbuf, wrote);

    if (wrote > 0)
    {
        k_sem_give(&tx_space_sem);
    }

    return ring_buf_size_get(&tx_ringbuf) > 0;
}

static void interrupt_handler(const struct device *dev, void *user_data)
{
    ARG_UNUSED(user_data);

    while (uart_irq_update(dev) && uart_irq_is_pending(dev))
    {

        if (uart_irq_rx_ready(dev))
        {
            uint8_t buf[64];
            int r = uart_fifo_read(dev, buf, sizeof(buf));
            int dropped = 0;

            if (r < 0)
            {
                LOG_ERR("uart_fifo_read failed");
                r = 0;
            }
            for (int i = 0; i < r; i++)
            {
                if (k_msgq_put(&rx_msgq, &buf[i], K_NO_WAIT))
                {
                    dropped++;
                }
            }
            if (dropped)
            {
                LOG_WRN("RX queue full: dropped %d byte(s)",
                        dropped);
            }
        }

        if (uart_irq_tx_ready(dev))
        {
            if (!tx_drain_ringbuf(dev))
            {
                uart_irq_tx_disable(dev);
            }
        }
    }
}

int uart_send_buf(uint8_t *buf, size_t len)
{
    size_t sent = 0;

    while (sent < len)
    {
        uint32_t n = ring_buf_put(&tx_ringbuf,
                                  buf + sent,
                                  len - sent);
        sent += n;
        uart_irq_tx_enable(uart_dev);
        if (sent < len)
        {
            k_sem_take(&tx_space_sem, K_MSEC(10));
        }
    }

    return 0;
}

int uart_send_char(uint8_t c)
{
    return uart_send_buf(&c, 1);
}

static inline void print_baudrate(const struct device *dev)
{
    uint32_t baudrate;
    int ret = uart_line_ctrl_get(dev, UART_LINE_CTRL_BAUD_RATE, &baudrate);

    if (ret)
    {
        LOG_WRN("Failed to get baudrate, ret=%d", ret);
    }
    else
    {
        LOG_INF("Baudrate %u", baudrate);
    }
}

int uart_init_dev(void)
{
    int ret = 0;

    if (!device_is_ready(uart_dev))
    {
        LOG_ERR("CDC ACM device not ready");
        return -1;
    }

    ret = usb_enable(NULL);
    if (ret != 0)
    {
        LOG_ERR("Failed to enable USB");
        return ret;
    }

    LOG_INF("Wait for DTR");
    while (true)
    {
        uint32_t dtr = 0U;

        uart_line_ctrl_get(uart_dev, UART_LINE_CTRL_DTR, &dtr);
        if (dtr)
        {
            break;
        }
        k_sleep(K_MSEC(100));
    }
    LOG_INF("DTR set");

    ret = uart_line_ctrl_set(uart_dev, UART_LINE_CTRL_DCD, 1);
    if (ret)
    {
        LOG_WRN("Failed to set DCD, ret=%d", ret);
    }
    ret = uart_line_ctrl_set(uart_dev, UART_LINE_CTRL_DSR, 1);
    if (ret)
    {
        LOG_WRN("Failed to set DSR, ret=%d", ret);
    }

    k_msleep(100);
    print_baudrate(uart_dev);

    k_sem_init(&tx_space_sem, 0, 1);

    uart_irq_callback_set(uart_dev, interrupt_handler);
    uart_irq_rx_enable(uart_dev);

    return 0;
}
