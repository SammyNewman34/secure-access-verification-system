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
## 8. RC522 clone: VersionReg and ModeReg differ from the datasheet

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

## 9. First UID read: REQA, ATQA and anticollision on the RC522

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