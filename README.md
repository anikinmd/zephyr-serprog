# zephyr-serprog

A [Zephyr RTOS](https://zephyrproject.org/) based firmware that turns any Zephyr-supported board with USB and SPI into a [flashrom](https://flashrom.org/)-compatible SPI programmer using the serprog protocol.

## Supported Boards

- **STM32 BlackPill (blackpill_f411ce)** — supported out of the box

## Building

Make sure you have a working [Zephyr development environment](https://docs.zephyrproject.org/latest/develop/getting_started/index.html) set up.

```bash
west build -b blackpill_f411ce
west flash
```

## Usage

Once flashed, the board exposes a USB CDC-ACM serial device. Use it with flashrom's serprog programmer:

```bash
# Read flash contents
flashrom -p serprog:dev=/dev/ttyACM0:115200 -r dump.bin

# Write flash contents
flashrom -p serprog:dev=/dev/ttyACM0:115200 -w firmware.bin

# Verify flash contents
flashrom -p serprog:dev=/dev/ttyACM0:115200 -v firmware.bin
```

> **Note:** The baudrate (115200 in the examples above) is required by flashrom's syntax but is meaningless for USB CDC-ACM, the actual transfer speed is determined by USB. You can specify any value.

## Adding a New Board

Adding support for a new board is straightforward. You only need a devicetree overlay that:

1. Aliases the SPI bus to use as `serprog-spi`
2. Defines a USB CDC-ACM UART on `zephyr_udc0`
3. Configures the SPI bus with appropriate pins, chip-select GPIO, and clock frequency

Create an overlay file at `boards/<board_name>.overlay`:

```dts

/ {
    aliases {
        serprog-spi = &<spi_bus>;       /* e.g. &spi1 */
    };
};

&zephyr_udc0 {
    cdc_acm_uart0 {
        compatible = "zephyr,cdc-acm-uart";
        label = "Zephyr USB CDC-ACM";
    };
};

&<spi_bus> {                            /* e.g. &spi1 */
    pinctrl-0 = <&...>;                 /* SCK, MISO, MOSI pin control nodes */
    cs-gpios = <&... GPIO_ACTIVE_LOW>;  /* chip-select GPIO */
    pinctrl-names = "default";
    clock-frequency = <25000000>;       /* adjust as needed */
    status = "okay";

    serprog_flash: serprog-flash@0 {
        compatible = "zephyr,spi-device";
        reg = <0>;
    };
};
```

Then build for your board:

```bash
west build -b <board_name>
```

## License

Apache-2.0