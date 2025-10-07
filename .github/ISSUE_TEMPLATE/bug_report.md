# [BUG] RC522 not detecting cards

## Problem Description
RC522 RFID reader is not detecting any cards when they are placed near the reader.

## Verification Steps
- [ ] Cards are MIFARE Classic 1K (verified working via NFC Tools)
- [ ] Hardware connections checked
- [ ] Power supply verified (3.3V for RC522)
- [ ] Ground connections verified

## Suspected Issues
- Wrong SS/RST pin configuration
- Incorrect SPI frequency settings
- SPI communication initialization problems

## Next Steps
1. Test minimal UID-only code
2. Verify wiring connections
3. Check SPI speed configuration
4. Test with different RC522 module if available

## Hardware Configuration
- ESP32 Pin → RC522 Pin
- GPIO 23 → MOSI
- GPIO 19 → MISO  
- GPIO 18 → SCK
- GPIO 5 → SS/SDA
- GPIO 21 → RST
- 3.3V → VCC
- GND → GND

## Software Configuration
- ESP-IDF Version: v5.x
- SPI Host: SPI2_HOST
- SPI Frequency: Default (1MHz)

## Additional Notes
This issue needs to be resolved before implementing the full access control system functionality.
