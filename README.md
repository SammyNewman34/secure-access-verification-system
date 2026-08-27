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