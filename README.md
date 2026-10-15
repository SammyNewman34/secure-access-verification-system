# Secure Access Control + Verification System

A dual-MCU RFID access control system built to demonstrate real-time embedded
firmware, networking, and formal fault-injection verification — designed as a
portfolio project for embedded/firmware/RT/verification roles.

## Architecture
- **STM32 Nucleo-F446RE** (HAL, bare-metal): owns real-time RFID access logic,
  servo lock control, RTC timestamping, and buzzer/LCD feedback.
- **ESP32**: owns WiFi networking, MQTT publishing, and remote logging.
- **Python fault-injection harness**: pytest-based verification suite that
  exercises the system against written pass/fail specs (invalid UID spam,
  forced mid-transaction resets, WiFi kill, etc.) — the key differentiator
  of this project versus a typical RFID demo.

## Status
🚧 In progress — see commit history for current phase.

## Repo layout
- `/nucleo-firmware` — STM32CubeIDE HAL project
- `/esp32-firmware` — PlatformIO/Arduino project
- `/test-harness` — Python pytest fault-injection suite
- `/backend` — Flask + SQLite + Streamlit dashboard
- `/docs` — architecture notes, wiring diagrams, specs



**Status: Phase 1 complete. **
RC522 UID read (REQA + anticollision with BCC check), whitelist decision, and GRANTED/DENIED with UID on an LCD1602, running on real hardware. Next: servo lock, DS1307 timestamps, buzzer.

## Hardware wiring (Phase 1)

Nucleo-F446RE (STM32CubeIDE 2.2.0, HAL). SPI1 runs at 2.625 MHz (84 MHz APB2 / 32, mode 0).

### RC522 (SPI1, 3.3 V only)

| RC522 | Nucleo pin |
|---|---|
| SDA (CS) | PA4 (GPIO, software chip select) |
| SCK | PA5 (SPI1_SCK) |
| MISO | PA6 (SPI1_MISO) |
| MOSI | PA7 (SPI1_MOSI) |
| RST | PB0 (GPIO) |
| 3.3V | 3V3 (never 5V) |
| GND | GND |
| IRQ | not connected |

### LCD1602 (parallel, 4-bit mode, write-only)

| LCD pin | Name | Connection |
|---|---|---|
| 1 | VSS | GND |
| 2 | VDD | 5V |
| 3 | V0 | wiper of 10k pot (outer pins on GND and 5V) |
| 4 | RS | PA9 |
| 5 | RW | GND |
| 6 | E | PC7 |
| 11-14 | D4-D7 | PB5, PB10, PA8, PA10 |
| 15 / 16 | A / K | 5V / GND (backlight) |

D0-D3 are unconnected. The LCD needs 5 V for contrast and backlight (contrast did not work from 3.3 V); the 3.3 V logic signals from the Nucleo drive it reliably on this module.

### Serial
USART2 (PA2/PA3) via the ST-Link virtual COM port, 115200 8N1.

## Notes on the RC522 module

The module (from an AliExpress starter kit) differs from the NXP MFRC522 datasheet (Rev. 3.9): ModeReg reads 0x3D instead of the documented 0x3F reset value, and VersionReg reads 0x18. The firmware therefore does not require a specific version value. It treats 0x00/0xFF as a fault and verifies function by reading cards. See `docs/learning-log.md`, entries 8-9.

