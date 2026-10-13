# Access Control Spec (draft v0.1)

| ID | Requirement | Status |
|----|-------------|--------|
| AC-1 | A card whose 4-byte UID is in the whitelist shall produce GRANTED. | Implemented, tested manually |
| AC-2 | A card whose UID is not in the whitelist shall produce DENIED. | Implemented, tested manually |
| AC-3 | A UID shall only be accepted if its BCC check byte is valid. A bad BCC shall produce no decision (not GRANTED). | Implemented (anticoll returns 4), not yet tested |
| AC-4 | With no card present, no decision shall be output. | Implemented, observed |
| AC-5 | If the reader reports an error (ErrIRq) or a wrong-length reply, no GRANTED shall be output. | Implemented, not yet tested |
| AC-6 | A reader fault (VersionReg 0x00 or 0xFF, or register write/read-back failure) shall be detected and reported. | NOT implemented |
| AC-7 | Cards with 7- or 10-byte UIDs are out of scope and shall not be granted. | Needs checking: what does the current code do with them? |