# Access Control Spec (draft v2.0)

| ID | Requirement | Status |
|----|-------------|--------|
| AC-1 | A card whose 4-byte UID is in the whitelist shall produce GRANTED. | Implemented, tested manually |
| AC-2 | A card whose UID is not in the whitelist shall produce DENIED. | Implemented, tested manually |
| AC-3 | A UID shall only be accepted if its BCC check byte is valid. A bad BCC shall produce no decision (not GRANTED). | Implemented (anticoll returns 4), not yet tested |
| AC-4 | With no card present, no decision shall be output. | Implemented, observed |
| AC-5 | If the reader reports an error (ErrIRq) or a wrong-length reply, no GRANTED shall be output. | Implemented, not yet tested |
| AC-6 | A reader fault (VersionReg 0x00 or 0xFF, or register write/read-back failure) shall be detected and reported. | NOT implemented |
| AC-7 | Cards with 7- or 10-byte UIDs are out of scope and shall not be granted. | Needs checking: what does the current code do with them? |
| AC-8 | After a result is shown, the display shall return to idle within about 2 s of the last successful read. | Implemented, tested manually |
| AC-9 | A continuously presented card shall produce one decision output, not repeated outputs. | Implemented, tested manually |
| AC-10 | A whitelisted UID shall move the servo to UNLOCKED within 500 ms of the decision. | Implemented, observed manually, timing not measured |
| AC-11 | The lock shall return to LOCKED UNLOCK_HOLD_MS (5000 ms) after the last grant, even if the card is still present. | Implemented, tested manually (the card-still-present case is a known limitation, see below) |
| AC-12 | A DENIED decision shall never move the servo to UNLOCKED. | Implemented, tested manually |
| AC-13 | On boot the servo shall be commanded to LOCKED before any card decision. | Implemented, observed manually |
| AC-14 | A GRANTED decision shall sound the granted pattern (one 300 ms beep) and a DENIED decision the denied pattern (three 100 ms beeps). | Implemented, tested by ear, timing not measured |
| AC-15 | A new buzzer pattern shall replace the pattern in progress. | Implemented (observed as an audible merge of the two patterns) |
| AC-16 | A card presented while a buzzer pattern is playing shall still be read and decided. | Implemented, tested manually |
| AC-17 | At boot the antenna-enable bits of TxControlReg shall be checked, and a failure shall be reported on UART and LCD. | Implemented, NOT tested. An unpowered reader reading back 0xFF would pass this check, so AC-6 stays open. |
| AC-18 | An RTC read failure or invalid RTC value shall never change a grant/deny decision, and shall never produce a timestamp. | NOT implemented (RTC not in the access flow yet) |
| AC-19 | After an I2C failure the firmware shall recover the bus (bus clear) or report an RTC fault. | Partly: a manual bus-clear key exists; automatic recovery is NOT implemented |


## Known limitations
- A same card re-presented within 2 s does not re-trigger a decision.
- Only 4-byte UIDs are handled. One card at a time (a collision counts as a failure). The card is never selected or halted.
- The lock relocks UNLOCK_HOLD_MS after the last grant, even if the card is still on the reader.
- The servo has no position feedback. If its power is off, the firmware still reports UNLOCKED.
- LOCKED/UNLOCKED pulse widths (700 and 2300 µs) are placeholders until the servo is mounted on a lock.
- RFID polling can still block the loop for roughly 30 to 60 ms per poll (estimated from the timeouts in the code, not measured). LCD redraws block for a few tens of ms (not measured). The DS1307 diagnostic keys (`t`, `c`) block while they run.
- The DS1307 is intermittent (SDA stuck low), cause not identified, and it is not part of the access flow.
- A reader fault does not block grants. Whether it should is undecided.