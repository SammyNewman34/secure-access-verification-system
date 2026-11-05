# Secure Access Control + Verification System

A dual-MCU RFID access control system built to demonstrate real-time embedded
firmware, networking, and formal fault-injection verification, designed as a
portfolio project for embedded/firmware/RT/verification roles.

## Architecture
- **STM32 Nucleo-F446RE** (HAL, bare-metal): owns real-time RFID access logic,
  servo lock control, and buzzer/LCD feedback. RTC timestamping is planned.
- **ESP32**: owns WiFi networking, MQTT publishing, and remote logging (planned).
- **Python fault-injection harness**: pytest-based verification suite that
  exercises the system against written pass/fail specs (invalid UID spam,
  forced mid-transaction resets, WiFi kill, etc.). This is the key differentiator
  of the project versus a typical RFID demo (planned).

## Status
**Phase 1 complete:** RC522 UID read (REQA + anticollision with BCC check), whitelist decision, GRANTED/DENIED on an LCD1602.

**Phase 2 in progress:** card decisions drive a servo "lock" (GRANTED unlocks, relocks after 5 s) and a buzzer (non-blocking). The LCD shows the result, and a boot banner reports the build. 
Each decision is logged over UART with a timestamp from the DS1307, falling back to an uptime stamp if the RTC read fails. The DS1307 had intermittent I2C lockups (cause not identified), so it is treated as best-effort and never affects a grant/deny decision. Next: Phase 3 (UART protocol to the ESP32).

## Repo layout
- `/nucleo-firmware` - STM32CubeIDE HAL project
- `/esp32-firmware` - PlatformIO/Arduino project
- `/test-harness` - Python pytest fault-injection suite
- `/backend` - Flask + SQLite + Streamlit dashboard
- `/docs` - architecture notes, wiring diagrams, specs (`access-spes.md`, `learning_log_v1.md`)

## Hardware wiring

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

### Servo, buzzer, DS1307 (Phase 2)

| Part | Nucleo pin | Notes |
|---|---|---|
| SG90 servo signal | PA0 (TIM2_CH1, 50 Hz PWM) | Servo power from a separate 5 V module, grounds shared with the Nucleo. Never from 3.3 V. |
| Active buzzer | PC0 via 1 kΩ and a 2N2222 (NPN) | Buzzer + on 3.3 V, - to the collector, emitter to GND. Checked by ear and by touch (transistor stays cool); current not measured. |
| DS1307 SCL | PB8 (I2C1_SCL) | Module has 4.7 kΩ pull-ups to VCC |
| DS1307 SDA | PB9 (I2C1_SDA) | PB8/PB9 are listed as 5 V tolerant (FT) in the STM32F446 datasheet |
| DS1307 VCC / GND | Nucleo 5 V / GND | Module powered from 5 V, so the I2C lines idle at about 4.7 V |

### Serial
USART2 (PA2/PA3) via the ST-Link virtual COM port, 115200 8N1.

## Notes
- Servo measured safe range: 650 to 2380 µs. The LOCKED/UNLOCKED values (700 and 2300 µs) are placeholders until the servo is mounted (learning log 11).
- DS1307: intermittent I2C lockup (SDA stuck low) observed and recovered with a bus clear. Cause not identified; the RTC is not in the access flow (learning log 13).
- RC522 module (from an AliExpress starter kit): ModeReg reads 0x3D instead of the documented 0x3F reset value, and VersionReg reads 0x18 (NXP MFRC522 datasheet Rev. 3.9). The firmware does not require a specific version value and verifies function by reading cards. A VersionReg 0x00/0xFF fault check is specified (AC-6) but not implemented yet. See `docs/learning_log_v1.md`, entries 8-9.
- RTC is set from the build time (accuracy limited by the build-to-set delay; drift not measured). Wall-clock time is expected to come from the ESP32 (NTP) in a later phase.
- Specs: `docs/access-specs.md`.