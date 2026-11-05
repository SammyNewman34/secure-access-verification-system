Learning Log

Running record of problems hit while bringing up the Nucleo-F446RE, how each was diagnosed, and what it taught me. Add a date to each entry when you commit it.

Format for each entry: Symptom ? Cause ? Fix ? Takeaway.

1. STM32CubeIDE 2.x has no built-in CubeMX
Symptom: The new-project wizard offered only Empty, C, C++, and CMake projects. No Board Selector, and a project made from "STM32CubeIDE Empty Project" had no .ioc, no Core/, and no Drivers/.
Cause: CubeIDE 2.0 and later removed CubeMX from the IDE. They are now separate tools.
Fix: Create the project in standalone STM32CubeMX (Board Selector, NUCLEO-F446RE), set Toolchain to STM32CubeIDE in Project Manager, generate, then import with File > Import > STM32CubeMX/STM32CubeIDE Project. Double-clicking the .ioc in CubeIDE opens it in CubeMX for later edits.
Takeaway: Tutorials go stale quickly. Check the tool version before trusting a workflow.
2. huart2 undeclared
Symptom: Compile error: huart2 undeclared.
Cause: USART2 was not enabled in CubeMX. CubeMX only generates the UART handle and init function for peripherals that are turned on.
Fix: Enable USART2 (Asynchronous, 115200, 8N1) in CubeMX, regenerate.
Takeaway: Generated code reflects the .ioc. A missing symbol often means a missing configuration, not a typo.
3. "Invalid storage class for function"
Symptom: Compile error on a static void uart_print(...) helper.
Cause: The function was pasted inside main(). C has no nested functions.
Fix: Move helpers into the USER CODE BEGIN 0 block above main().
Takeaway: Where code goes matters as much as what it says. USER CODE markers protect it from regeneration.
4. Button printed "released" continuously
Symptom: Polling the button and printing its level every loop flooded the terminal.
Cause: Reporting a level on every pass, not a change.
Fix: Keep the previous state and act only when it changes (edge detection). The button on PC13 is active low (0 = pressed).
Takeaway: Event detection beats level polling. Short poll delays catch quick taps, and very short delays can expose switch bounce.
5. USART1 enabled instead of USART2
Symptom: Wrong UART handle name in the generated code (huart1).
Cause: USART1 had been configured instead of USART2. Only USART2 (PA2/PA3) is wired to the ST-Link virtual COM port on this board.
Fix: Disable USART1, enable USART2, regenerate.
Takeaway: A handle name reveals which hardware instance you configured. Check the board schematic or user manual for which peripheral reaches the PC.
6. SPI1 would not enable in CubeMX (red stop sign)
Symptom: SPI1 mode reverted to Disable. Tooltip: "pin PA5 is reserved and/or PB3 mapped with SYS_JTDO-SWO".
Cause: SPI1_SCK can only go on PA5 or PB3. PA5 was reserved by the board's LED setup, and PB3 was claimed by SWO trace in the default debug config.
Fix: Set SYS > Debug to Serial Wire (releases PB3), free PA5 and assign it to SPI1_SCK, and confirm PA6/PA7 are MISO/MOSI.
Takeaway: Pins have a limited set of alternate functions, and a pin can serve only one function at a time. Planning pin assignments up front avoids this.
7. SPI loopback returned all zeros
Symptom: TX A5 3C 00 FF, RX 00 00 00 00, with HAL_SPI_TransmitReceive returning OK.
Diagnosis: Wiring and jumper checks passed. Printing the GPIO MODER and AFR registers showed PA5 in output mode (m=1) when it should be alternate function (m=2).
Cause: BSP_LED_Init(LED2) in main() ran after the SPI init and reconfigured PA5 as a plain GPIO output. This CubeMX version added ST's Nucleo BSP to the project, which also explains the unexpected LD1/BUTTON names and missing main.h macros.
Fix: Comment out the BSP LED init. Result: RX: A5 3C 00 FF, a clean match.
Takeaway: When two pieces of code configure the same pin, the last one wins. Reading peripheral registers directly shows what the hardware is actually doing. Note: I did not fully determine why a missing clock pin produced all-zero RX, so that part of the mechanism is unconfirmed.
Carry-forward rules for nucleo-firmware
Use Board Selector, but do not keep the BSP. Use plain HAL with my own pin labels (RC522_CS, RC522_RST).
Set SYS > Debug to Serial Wire before generating code.
PA5 is shared between the on-board LED and SPI1_SCK. SCK wins.
Verify with loopback or register reads before blaming the external chip.
Keep all custom code inside USER CODE blocks.

8. RC522 clone: VersionReg and ModeReg differ from the datasheet

**What happened:** After wiring the RC522 and reading registers over SPI1, VersionReg (0x37) returned 0x18 and ModeReg (0x11) returned 0x3D. The module came from an AliExpress Arduino starter pack.

**Evidence:**
- Datasheet (NXP MFRC522 Rev. 3.9), Table 55: ModeReg reset value is 0x3F. Measured: 0x3D. The only differing bit is bit 1, part of CRCPreset[1:0] (datasheet 11 = preset 0xFFFF, this chip 01 = preset 0x6363).
- The value was the same after the RST pin pulse and after a soft reset (CommandReg = 0x0F), so it is this chip's reset value, not a timing artifact.
- CommandReg (0x01) = 0x20 and TxControlReg (0x14) = 0x80 matched the datasheet.
- A full dump of registers 0x00-0x3F read back stable, with no stuck-at-0x00 or all-0xFF pattern.

**Not yet verified:**
- What 0x18 means in VersionReg (Table 131, p. 66, not yet checked).
- The maximum SPI clock.
- Whether the soft reset command actually executed, or the registers were already at defaults. The test would be to write a non-default value, soft reset, and confirm it returns to default.

**Decision:** Don't hard-fail on VersionReg being 0x91/0x92. Treat "version is not 0x00/0xFF and write/read-back passes" as healthy, log the actual value, and verify function by reading a card. The pass/fail that matters is whether the chip returns a UID reliably.

**Lesson:** A datasheet describes the genuine part. Cheap modules can differ, so measure the chip and compare against the document instead of assuming. Keep the "unverified" list in the entry until each item is closed.

9. First UID read: REQA, ATQA and anticollision on the RC522

**What happened:** Using the Transceive command over SPI1, the RC522 answered a REQA with an ATQA and returned a UID from both tags in the kit. The white card and the blue keyfob each gave a stable UID that passed the BCC check.

**Evidence:**
- Datasheet command table: Transceive = 1100 (0x0C), SoftReset = 1111 (0x0F). Both matched the values used.
- ComIrqReg reset value 0x14 matched the datasheet (register dump, 0x04).
- Datasheet Table 47 (BitFramingReg): StartSend is bit 7, TxLastBits is bits 2:0. Table 34 (ErrorReg): CollErr is bit 3, ProtocolErr is bit 0.
- FIFOLevelReg (Table 41): bit 7 is FlushBuffer, and the level is bits 6:0, so the level is masked with 0x7F.
- No card in range: timeout with ErrorReg = 0x00. This is the correct "nobody there" result.
- REQA with the white card on the antenna: ATQA = 04 00.
- Anticollision (0x93 0x20): white card UID = 01 17 37 02, blue keyfob UID = 1A B6 6C 06. Both passed the BCC check (BCC = UID0 ^ UID1 ^ UID2 ^ UID3). Each UID was stable across repeated reads.

**Observation, cause not confirmed:** While only REQA was running, a card on the antenna alternated between ATQA and a timeout. That pattern went away once the anticollision step ran. My hypothesis is that a card that has answered REQA moves out of the IDLE state and ignores the next REQA. That has not been proven.

**Not yet verified:**
- The cause of the alternating timeout pattern above.
- Whether the 30 ms wait for a response is a sensible limit or just a working one.
- Whether the printed UIDs match what a phone NFC reader shows (byte order and format can differ between tools).
- Still open from entry 8: what 0x18 means in VersionReg (Table 131) and the maximum SPI clock.

**Known limitations (for the written spec):**
- Only 4-byte UIDs (cascade level 1) are handled. Cards with 7- or 10-byte UIDs are not supported.
- One card at a time. A collision is treated as a failure.
- The card is never selected or halted.

**Lesson:** Build the exchange in small steps. REQA alone, then anticollision, with a check (BCC) that proves the data arrived intact.

10. LCD stuck on the result screen; fixed with a small state machine

**What happened:** After GRANTED/DENIED appeared on the LCD, it stayed there when the card was removed. Only pressing RESET restored "ACCESS SYSTEM / READY".

**Cause:** The code only drew the display on a successful read. A failed read (card gone) did nothing, and the idle screen was only drawn once at boot.

**Fix:** Two states (IDLE, SHOWING). The display redraws only when the UID changes or when leaving idle, and returns to idle after 2 s with no successful read (RESULT_HOLD_MS). This also removed the flicker from redrawing every loop pass.

**Observed:** Held card prints once. Removing the card returns to idle after about 2 s. Swapping cards updates immediately.

**Not yet verified:**
- Behavior under rapid re-presentation of the same card (known limitation, see spec).
- The earlier alternating ATQA/timeout pattern (entry 9) has not been re-examined now that selection behavior changed.
- LCD init timings come from memory of the standard HD44780 sequence and were not checked against the module's datasheet. They work, but margins are unproven.

**Also this phase:**
- LCD1602 wired in 4-bit mode (RS, E, D4-D7). PA8 triggers a harmless-looking CubeMX RCC/MCO1 warning because it's mapped as GPIO. MCO1 was disabled and the display works.
- Contrast needs 5V on the pot. It did not work from 3.3V.

**Lesson:** If a display is only written on events, something has to own the "return to idle" transition. Make the states explicit.

11. Servo stumbled on 3.3 V

**Symptom:** The SG90 sometimes tried several times before it moved.
**Cause (likely):** I had wired its power to 3.3 V instead of 5 V. After moving it to the 5 V power module, the stumbling stopped. Every measurement taken on 3.3 V is void, so I redid them.
**Measured on 5 V (pulse widths in µs, tuned over UART with a temporary keyboard control):**
- Low end: the servo felt like it was fighting the stop between 550 and 500 µs. The last value where it still moved was 540 µs.
- High end: the last value where it still moved was 2480 µs. I have not checked whether this is a mechanical stop or the servo's electronics ignoring the pulse.
- Operating range: 650 to 2380 µs (about 100 µs margin from each end, a rule of thumb). `servo_set()` clamps to this range.
- LOCKED = 700 µs and UNLOCKED = 2300 µs are placeholders until the servo is mounted on the lock.
**Lesson:** Measure the supply before trusting any result, and discard data taken on the wrong supply.

12. I blamed the servo power module for an I2C failure, and I was wrong

**Symptom:** The DS1307 scan found nothing while the servo power module was unplugged. Plugging the module in made it work.
**First explanation:** "The I2C bus needs the servo module powered."
**What disproved it:** The failure came back with the module plugged in, and the DS1307 also worked for a while with it unplugged.
**What I take from it:** A change that coincides with a fix isn't necessarily the cause. Plugging in the module bumped the breadboard, which may have changed a contact or the battery seating, but that is a guess.
**Lesson:** Change one thing at a time, and keep notes of every other thing I touched.

13. DS1307 intermittent I2C failure

**Symptom:** After working for a few minutes, reads failed. First `err=0x04` (acknowledge failure), later `status=2` (HAL_BUSY) and `err=0x200`/`0x220`. (Error code meanings decoded from memory, still to be checked against `stm32f4xx_hal_i2c.h`.)
**Evidence:**
- A diagnostic key printing pin levels and I2C status registers showed `SCL=1 SDA=0 SR2=0x0003` during the failure.
- SDA read 0 V at the module pin, and still 0 V with the SDA wire unplugged from the Nucleo. Something on the module side was holding the line low (the DS1307 is the prime suspect, not proven).
- A Nucleo reset did not clear it.
- A bus-clear routine (peripheral released, SCL pulsed by hand) needed 1 clock pulse to release SDA. After that the scan and reads worked again with no power cycle.
- One read returned `FF FF FF` for date, month and year while `HAL_I2C_Mem_Read` reported OK. A successful status does not mean valid data.
- The date registers changed from 19/08/20 to all zeros during testing. I don't know why.
**Current state:** Intermittent. The lockup recurred after a cold start, and the cause is not identified. The battery reseat was a hypothesis and did not hold. Parked; the diagnostic keys remain in the firmware.
**Tools added:** `b` (print bus state), `c` (bus clear), `t` (100 reads with OK, failed and invalid counts), `s` (bus scan), `d` (raw register dump).
**Lesson:** Print the error codes and pin states before theorizing, and treat a good run as weak evidence for a fault that appears after minutes.
**Still to do:** Set the time, validate every read, and make recovery automatic with a counter.

## 14. Lock state machine: card decision drives the servo and buzzer

**Goal:** Turn a card read into a physical action.
**What I built:** A two-state lock (LOCKED / UNLOCKED) with `lock_set()` as the only place that moves the servo for access logic. GRANTED unlocks and plays one beep, and the lock returns to LOCKED after 5 s (a non-blocking timer check with `HAL_GetTick()`). DENIED beeps three times and the servo stays put.
**Verified manually:** boot goes to LOCKED, keyfob gives DENIED with no servo movement, white card unlocks and relocks at about 5 s, a held card gives one decision.
**Design decisions:** `is_authorized()` is called once and the result reused, so the UART line, LCD and servo can't disagree. The servo is commanded before the LCD redraw so it reacts at once.
**Not verified / limits:** the relock timer isn't paused while the card is present, and there is no position feedback.
**Idea I decided not to build yet:** a DENIED card relocking immediately while the lock is open. It's safer, but a stranger's card could close the lock on the legitimate user. Parked as a decision for later.

## 15. Making the firmware non-blocking

**Problem:** `HAL_Delay` in the buzzer code made the card reader deaf for the length of each beep, and one loop delay set the pace for everything.
**What I changed (in two steps):**
1. The buzzer became a small pattern player (`buzzer_start()` and `buzzer_update()`) that checks `HAL_GetTick()` each loop pass.
2. I removed the blanket 100 ms loop delay. The card reader polls on its own 100 ms timer, and the buzzer and relock check run every pass.
**Verified:** a card tapped during a granted beep is read and shown immediately (before, the reader was deaf during the beep).
**Observed:** starting a new pattern while one is playing merges audibly with the old one. I think this is because step 0 is always ON, so the pin never goes low, but I didn't test further. It's now spec behavior (AC-15).
**Not fixed:** a reader poll can still block for roughly 30 to 60 ms (my estimate from the timeouts, not measured), and LCD redraws are bit-banged with 1 ms delays.
**Lesson:** Each task asks "is it time yet?" instead of waiting, and a blocking call hides in many places (timeouts, delays inside helpers), not only in `HAL_Delay` calls I wrote myself.

## 16. Generated driver files were never committed

**Symptom:** `git status` showed five untracked `stm32f4xx_hal_i2c*` and `stm32f4xx_ll_i2c.h` files in `Drivers/`, plus modified `hal_conf.h` and `hal_msp.c`, after I had already pushed the "I2C" commit.
**Cause:** I had been staging only `main.c` and the `.ioc` by name, so the files CubeMX generated when I enabled I2C never went in. My local build worked because the files were on disk.
**Impact:** a fresh clone of the repo wouldn't have built.
**Fix:** staged all the generated files by name in a later commit.
**Lesson:** After every CubeMX regeneration, run `git status` and look at the untracked files too, not only the ones I edited. (I haven't tested a fresh clone yet, and that's the real check.)
**Smaller lesson:** Git's pager (`less`) looked like a hang. `q` leaves it, and `git config --global core.pager cat` stops it happening.

## 17. Boot banner and reader check

**What I added:** On reset the LCD shows "ACCESS SYSTEM / STARTING...", UART prints a banner with the build date and time, and one short beep plays. Then the idle screen appears. The antenna check prints (and shows on the LCD) only when TxControl doesn't read back with the antenna bits set.
**Why:** The build time tells me which firmware is actually running (I once flashed the wrong project), and a boot beep tells me a reset happened.
**Not verified:** I haven't tested the fault path with the reader unpowered. An unpowered reader reading back 0xFF would pass this check, so reader fault detection (AC-6) still needs a VersionReg check.