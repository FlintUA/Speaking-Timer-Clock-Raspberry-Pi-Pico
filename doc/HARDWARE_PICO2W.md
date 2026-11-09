# Current Pico 2 W pin map

This table matches the C firmware on main and the configuration tested on the assembled clock. GP means GPIO; pin means the physical board pin. The original [Pico schematic](../schematic/shematic.png) does not show the SPI card or relocated encoder. See [hardware test evidence](HARDWARE_TEST_EVIDENCE.md).

| Signal | GPIO | Physical pin |
| --- | --- | --- |
| LCD I2C SDA / SCL | GP0 / GP1 | 1 / 2 |
| DS1302 CLK / DIO / CE | GP2 / GP5 / GP4 | 4 / 7 / 6 |
| SD SPI1 DO / MISO | GP8 | 11 |
| SD SPI1 CS | GP9 | 12 |
| SD SPI1 CLK / SCK | GP10 | 14 |
| SD SPI1 DI / MOSI | GP11 | 15 |
| Timer encoder DT / CLK | GP12 / GP13 | 16 / 17 |
| Volume encoder DT / CLK | GP14 / GP15 | 19 / 20 |
| Pico TX -> DFPlayer RX | GP16 | 21 |
| Pico RX <- DFPlayer TX | GP17 | 22 |
| DFPlayer BUSY | GP18 | 24 |
| Timer encoder button | GP19 | 25 |
| Volume encoder button | GP20 | 26 |
| Alarm button | GP21 | 27 |
| Mode button | GP22 | 29 |
| Minus button | GP26 | 31 |
| Settings / plus button | GP27 | 32 |
| Timer-mode button | GP28 | 34 |
| External LCD illumination PWM | GP6 | 9 |

Buttons connect GPIO to common ground; the code enables internal pull-ups. To free SPI pins, the timer encoder rotation signals moved: DT from GP11 to GP12, CLK from GP10 to GP13. The audit records user confirmation that the relocated encoder works. Encoder buttons remain GP19 (timer) and GP20 (volume). The volume button was tested separately: ON/OFF appears in USB logs and on the LCD.

The connected external SD module is rated for 3.3 V: VCC -> 3V3(OUT), pin 36; GND -> common ground. SPI follows the table above and has passed hardware tests for SDHC/SDXC initialization, sector reads, FAT32, directories and MP3 files. DFPlayer retains its own card and analog output. The separate Pico FAT32 card contains copies of numbered MP3 folders, such as 07/001.mp3. Later tests confirmed Helix decoding and Bluetooth playback.

This table documents signal connections. A complete RTC, LCD and DFPlayer power schematic still requires comparison with the installed modules. GP6 provides active-high PWM for a separate LCD illumination LED through a transistor or MOSFET switching stage. The LED uses a current-limiting resistor and a common ground with the Pico. The user confirmed that switching and brightness adjustment work without audible interference. See [hardware test evidence](HARDWARE_TEST_EVIDENCE.md#external-lcd-illumination-acceptance).

Sources: [controls.c](../firmware/pico2w_c/drivers/controls.c), [main.c](../firmware/pico2w_c/main.c), [sd_spi.c](../firmware/pico2w_c/drivers/sd_spi.c), [dfplayer.c](../firmware/pico2w_c/drivers/dfplayer.c).
