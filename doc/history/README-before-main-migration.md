# Project status and firmware tracks

The existing clock is running **MicroPython v3.10.0** on the Pico 2 W.
The native **C / Pico SDK firmware is being developed in parallel** and
is **not yet a replacement** for the working clock.

| Directory | Purpose | State |
| --- | --- | --- |
| [`source/v3/`](../../source/v3/) | Working MicroPython clock, source of truth for behavior | Stable v3.10.0 |
| [`experiments/bluetooth_a2dp_nest/`](../../experiments/bluetooth_a2dp_nest/) | SBC/A2DP Source proof of concept on spare Pico 2 W | Tested with Nest Audio |
| [`firmware/pico2w_c/`](../../firmware/pico2w_c/) | Future integrated C clock | Preparing migration, not bootable yet |
| [`doc/C_MIGRATION_PLAN.md`](../C_MIGRATION_PLAN.md) | Porting matrix, pin map, verification and rollback gates | Maintained |

**Development safety:** do not flash the C experiment onto the working clock
or remove any working MicroPython modules as part of housekeeping. The
historical description below documents the original hardware and design;
its feature limitations may not reflect the current v3.10.0 firmware.

---

# Speaking Timer-Clock on Raspberry Pi Pico
Speaking Timer-Clock on Raspberry Pi Pico (H/W)

Timer-clock with voice communication (1 timer up to 240 minutes, 5 fixed time points) and MP3 player based on Raspberry Pi Pico board, 1602 display with I2C module, RTC Clock DS1302 clock module, DFPlayer Mini MP3 player, 2 encoders with a button and a pull-up resistor 1K each, 5 additional buttons.

One encoder adjusts the volume, and a button on the encoder turns the sound on/off. The second encoder is designed to set the required number of minutes of the timer, the button on the encoder starts/stops the timer. The timer is not too accurate yet - the accuracy is approximate, from one minute, for example, setting the timer for 5 minutes can work after 4 minutes, but not less, because there is no concept of "seconds" in the device. There are 5 additional buttons for various additional functions. The display shows messages in German, voice messages in Russian and German. Every hour the time is announced, every half hour there is a short signal, there are sounds imitating the striking of an ancient clock. There is a silence mode (silent / night mode) from 22.00 to 7.00. While there is no setting mode, all settings are performed in the source code.

The original page also repeated this description in Russian; the English description above retains the same historical information.
