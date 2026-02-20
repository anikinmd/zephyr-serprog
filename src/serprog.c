/*
 * Copyright (c) 2026 Mikhail Anikin
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "serprog.h"
#include <stdint.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>
#include <zephyr/logging/log.h>
#include "spi.h"
#include "uart.h"

LOG_MODULE_REGISTER(serprog, LOG_LEVEL_INF);

#define BUSTYPE_SPI_ONLY 0b1000
#define BUF_SIZE 4096
#define SP_DATA_TIMEOUT_MS 300

enum serprog_cmd_state
{
    SP_WAIT_CMD,
    SP_WAIT_DATA
};

struct spiop_header
{
    uint32_t slen;
    uint32_t rlen;
};

int state = SP_WAIT_CMD;
int cmd_in_process = 0;
int byte_counter = 0;
uint32_t last_rx_ms;

uint8_t prog_name[16] = "zephyr-serprog";

uint8_t byte_buf[BUF_SIZE];

static inline uint32_t u24_le(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16);
}

static inline uint32_t u32_le(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

void sp_reset_state(void)
{
    state = SP_WAIT_CMD;
    byte_counter = 0;
}

static inline void sp_mark_rx(void)
{
    last_rx_ms = k_uptime_get_32();
}

static inline void sp_check_timeout(void)
{
    if (state != SP_WAIT_DATA)
    {
        return;
    }
    uint32_t now = k_uptime_get_32();
    if ((uint32_t)(now - last_rx_ms) > SP_DATA_TIMEOUT_MS)
    {
        LOG_WRN("serprog: timeout in cmd 0x%02x, dropping partial", cmd_in_process);
        sp_reset_state();
    }
}

void supported_commands(uint8_t *buff)
{
    memset(buff, 0, 32);
    uint8_t commands[] = {
        S_CMD_NOP,
        S_CMD_Q_IFACE,
        S_CMD_Q_CMDMAP,
        S_CMD_Q_PGMNAME,
        S_CMD_SYNCNOP,
        S_CMD_Q_SERBUF,
        S_CMD_Q_OPBUF,
        S_CMD_Q_WRNMAXLEN,
        S_CMD_Q_RDNMAXLEN,
        S_CMD_Q_BUSTYPE,
        S_CMD_S_BUSTYPE,
        S_CMD_O_SPIOP,
        S_CMD_S_SPI_FREQ,
    };
    for (int i = 0; i < ARRAY_SIZE(commands); i++)
    {
        uint8_t cmd = commands[i];
        buff[cmd >> 3] |= (uint8_t)(1u << (cmd & 7));
    }
}

void process_sp_command(uint8_t cmd)
{
    uint32_t ret_len = 1; // at least one byte (ACK/NACK)
    byte_buf[0] = S_ACK;
    cmd_in_process = cmd;
    byte_counter = 0;
    LOG_DBG("CMD 0x%x", cmd);
    switch (cmd)
    {
    case S_CMD_NOP:
        break;

    case S_CMD_Q_IFACE:
        byte_buf[1] = 1;
        byte_buf[2] = 0;
        ret_len = 3;
        break;
    case S_CMD_Q_CMDMAP:
        supported_commands(byte_buf + 1);
        ret_len = 33;
        break;
    case S_CMD_Q_PGMNAME:
        memcpy(byte_buf + 1, prog_name, sizeof(prog_name));
        ret_len = 17;
        break;
    case S_CMD_SYNCNOP:
        byte_buf[0] = S_NAK;
        byte_buf[1] = S_ACK;
        ret_len = 2;
        break;
    case S_CMD_Q_SERBUF:
    case S_CMD_Q_OPBUF:
        byte_buf[1] = (uint8_t)(BUF_SIZE & 0xFF);
        byte_buf[2] = (uint8_t)((BUF_SIZE >> 8) & 0xFF);
        ret_len = 3;
        break;
    case S_CMD_Q_WRNMAXLEN:
    case S_CMD_Q_RDNMAXLEN:
        byte_buf[1] = (uint8_t)(BUF_SIZE & 0xFF);
        byte_buf[2] = (uint8_t)((BUF_SIZE >> 8) & 0xFF);
        byte_buf[3] = 0;
        ret_len = 4;
        break;
    case S_CMD_Q_BUSTYPE:
        byte_buf[1] = BUSTYPE_SPI_ONLY;
        ret_len = 2;
        break;
    case S_CMD_S_BUSTYPE:
    case S_CMD_O_SPIOP:
    case S_CMD_S_SPI_FREQ:
        ret_len = 0;
        state = SP_WAIT_DATA;
        sp_mark_rx();
        break;

    default:
        byte_buf[0] = S_NAK;
        break;
    }
    if (ret_len > 0)
    {
        uart_send_buf(byte_buf, ret_len);
    }
}

void process_sp_spiop(uint8_t c)
{
    static struct spiop_header spiop_params;
    if (byte_counter <= 5)
    {
        byte_buf[byte_counter] = c;
        if (byte_counter == 5)
        {
            spiop_params.slen = u24_le(&byte_buf[0]);
            spiop_params.rlen = u24_le(&byte_buf[3]);
            LOG_HEXDUMP_DBG(byte_buf, 6, "PARAMS:");
            LOG_DBG("%s: parsed spiop params: slen: %d, rlen: %d", __func__, spiop_params.slen, spiop_params.rlen);
        }
        goto exit;
    }
    int send_counter = byte_counter - 6;
    if (send_counter < spiop_params.slen)
    {
        byte_buf[send_counter] = c;
        if (send_counter != (spiop_params.slen - 1))
            goto exit;
    }
    LOG_DBG("%s: Sending %d bytes", __func__, spiop_params.slen);
    LOG_HEXDUMP_DBG(byte_buf, spiop_params.slen, "TX:");
    int ret = spiop_transfer(byte_buf, spiop_params.slen, spiop_params.rlen);
    if (ret < 0)
    {
        LOG_ERR("%s: Failed to make SPI write, ret %d", __func__, ret);
        uart_send_char(S_NAK);
    }
    else
    {
        LOG_HEXDUMP_DBG(byte_buf, spiop_params.rlen, "RX:");
        uart_send_char(S_ACK);
        uart_send_buf(byte_buf, spiop_params.rlen);
    }
    sp_reset_state();
    return;
exit:
    byte_counter++;
    if (byte_counter > BUF_SIZE) {
        uart_send_char(S_NAK);
        sp_reset_state();
    }
    return;
}

void process_sp_set_spi_freq(uint8_t c)
{
    uint8_t ret = 0;
    if (byte_counter < 3)
    {
        byte_buf[byte_counter] = c;
        goto exit;
    }
    byte_buf[byte_counter] = c;
    uint32_t freq_hz = u32_le(&byte_buf[0]);
    if (freq_hz == 0)
    {
        uart_send_char(S_NAK);
        return;
    }
    LOG_INF("Setting SPI freq to %ul", freq_hz);
    ret = spiop_set_freq(freq_hz);
    if (ret < 0)
    {
        uart_send_char(S_NAK);
        return;
    }
    uart_send_char(S_ACK);
    uart_send_buf(byte_buf, 4);
    sp_reset_state();
exit:
    byte_counter++;
    if (byte_counter > BUF_SIZE) {
        uart_send_char(S_NAK);
        sp_reset_state();
    }
    return;
}

void process_sp_set_bustype(uint8_t c)
{
    uint8_t ret = S_NAK;
    if (c == BUSTYPE_SPI_ONLY)
    {
        ret = S_ACK;
    }
    uart_send_char(ret);
    state = SP_WAIT_CMD;
}

void process_sp_data(uint8_t c)
{
    switch (cmd_in_process)
    {
    case S_CMD_S_BUSTYPE:
        process_sp_set_bustype(c);
        break;
    case S_CMD_O_SPIOP:
        process_sp_spiop(c);
        break;
    case S_CMD_S_SPI_FREQ:
        process_sp_set_spi_freq(c);
        break;

    default:
        break;
    }
}

void process_sp_char(uint8_t c)
{
    sp_check_timeout();
    switch (state)
    {
    case SP_WAIT_CMD:
        process_sp_command(c);
        break;
    case SP_WAIT_DATA:
        sp_mark_rx();
        process_sp_data(c);
        break;
    default:
        sp_reset_state();
        break;
    }
}
