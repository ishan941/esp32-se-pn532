#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_PN532.h>

// =========================================================================
// ESP32-S3 (N16R8) PIN SELECTION FOR PN532 (I2C)
// =========================================================================
// ⚠️ CRITICAL ESP32-S3 NOTICE:
// 1. ESP32-S3 DOES NOT HAVE GPIO 22, 23, 24, or 25! (They do not exist in silicon).
//    The old ESP32 pins (SDA=21, SCL=22) will NEVER work on ESP32-S3!
// 2. Pins GPIO 26 to 37 are internally reserved on N16R8 for Octal Flash & PSRAM.
//    DO NOT connect anything to GPIO 26 - 37!
//
// Recommended Safe I2C Pin Pairs for ESP32-S3:
//   Default:       SDA = GPIO 8, SCL = GPIO 9 (Standard ESP32-S3 DevKit pins)
//   Alternative 1: SDA = GPIO 4, SCL = GPIO 5
//   Alternative 2: SDA = GPIO 1, SCL = GPIO 2
//   Alternative 3: SDA = GPIO 17, SCL = GPIO 18
// =========================================================================
#define I2C_SDA_PIN    8   // Connect to PN532 SDA pin
#define I2C_SCL_PIN    9   // Connect to PN532 SCL pin

// For standard 4-wire I2C (VCC, GND, SDA, SCL), set IRQ and RESET to -1 (disabled)
#define PN532_IRQ_PIN   (-1)
#define PN532_RESET_PIN (-1)

// Dynamic pointer to avoid global constructor hardware calls before setup()
Adafruit_PN532 *nfc = nullptr;

bool scanI2C() {
  Serial.println("\n--- Step 1: Scanning I2C Bus ---");
  Serial.printf("I2C configured on SDA=GPIO %d, SCL=GPIO %d\n", I2C_SDA_PIN, I2C_SCL_PIN);
  
  int deviceCount = 0;
  bool pn532Found = false;

  for (byte addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    byte error = Wire.endTransmission();

    if (error == 0) {
      Serial.printf("  [✓] Device found at address 0x%02X (%d)", addr, addr);
      if (addr == 0x24) {
        Serial.println("  <-- PN532 NFC Module detected!");
        pn532Found = true;
      } else {
        Serial.println();
      }
      deviceCount++;
    } else if (error == 4) {
      Serial.printf("  [!] Error communicating with address 0x%02X\n", addr);
    }
  }

  if (deviceCount == 0) {
    Serial.println("  [✗] NO I2C DEVICES FOUND on the bus!");
  } else {
    Serial.printf("Total I2C devices found: %d\n", deviceCount);
  }
  Serial.println("--------------------------------\n");
  return pn532Found;
}

void printTroubleshootingTips() {
  Serial.println("==================================================");
  Serial.println("🚨 PN532 NOT DETECTED (Address 0x24 missing)!");
  Serial.println("Check these right now:");
  Serial.println("1. DIP SWITCHES (Most common!):");
  Serial.println("   - SW1 must be ON (1 / up)");
  Serial.println("   - SW2 must be OFF (0 / down)");
  Serial.println("   (Unplug and re-plug USB after flipping switches!)");
  Serial.printf("2. WIRING CHECK (ESP32-S3):\n");
  Serial.printf("   - PN532 SDA -> ESP32-S3 GPIO %d\n", I2C_SDA_PIN);
  Serial.printf("   - PN532 SCL -> ESP32-S3 GPIO %d\n", I2C_SCL_PIN);
  Serial.println("   - PN532 GND -> ESP32-S3 GND");
  Serial.println("   - PN532 VCC -> 3.3V (or 5V if module has onboard regulator)");
  Serial.println("3. MODULE POWER LED:");
  Serial.println("   - Is the red PWR LED on the PN532 lit?");
  Serial.println("==================================================");
}

void setup() {
  // Initialize UART0 Serial at 115200 baud
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n\n==================================================");
  Serial.println("      ESP32-S3 (N16R8) PN532 NFC TEST BENCH       ");
  Serial.println("==================================================");
  Serial.printf("Chip Model: %s (Rev %d, Cores: %d)\n", ESP.getChipModel(), ESP.getChipRevision(), ESP.getChipCores());
  Serial.printf("Flash Size: %d MB\n", ESP.getFlashChipSize() / (1024 * 1024));
  Serial.printf("PSRAM Size: %d MB (Free: %d KB)\n", ESP.getPsramSize() / (1024 * 1024), ESP.getFreePsram() / 1024);
  Serial.flush();

  // Initialize I2C with ESP32-S3 specific pins
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  Wire.setClock(100000); // 100kHz standard I2C clock

  // Step 1: Scan I2C
  bool foundAtI2C = scanI2C();
  if (!foundAtI2C) {
    printTroubleshootingTips();
  }

  // Step 2: Initialize Adafruit PN532 driver
  Serial.println("--- Step 2: Initializing PN532 Driver ---");
  nfc = new Adafruit_PN532(PN532_IRQ_PIN, PN532_RESET_PIN, &Wire);
  nfc->begin();

  uint32_t versiondata = nfc->getFirmwareVersion();
  if (!versiondata) {
    Serial.println("  [✗] PN532 did not respond to firmware version query.");
    if (foundAtI2C) {
      Serial.println("  Tip: I2C device was detected at 0x24, but chip protocol timed out.");
      Serial.println("  Try power-cycling the board.");
    }
  } else {
    Serial.println("  [✓] PN532 FOUND AND RESPONDING!");
    Serial.printf("  Found chip PN5%02X\n", (versiondata >> 24) & 0xFF);
    Serial.printf("  Firmware ver: %d.%d\n", (versiondata >> 16) & 0xFF, (versiondata >> 8) & 0xFF);
    Serial.printf("  Supports: 0x%02X\n", versiondata & 0xFF);

    // Configure board to read RFID / NFC tags
    nfc->SAMConfig();
    Serial.println("  [✓] SAMConfigured for RFID/NFC reading.");
    Serial.println("\n>>> WAITING FOR NFC / RFID CARD TAP... <<<");
    Serial.println("Hold a card or key fob near the PN532 antenna...");
  }
  Serial.flush();
}

void loop() {
  if (!nfc) {
    delay(1000);
    return;
  }

  uint8_t success;
  uint8_t uid[] = {0, 0, 0, 0, 0, 0, 0}; // Buffer to store returned UID
  uint8_t uidLength;                     // Length of UID (4 or 7 bytes)

  // Wait for an ISO14443A card (Mifare, etc.) with timeout of 1000ms
  success = nfc->readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength, 1000);

  if (success) {
    Serial.println("\n----------------------------------------");
    Serial.println("🎉 [CARD DETECTED!]");
    Serial.printf("  UID Length: %d bytes\n", uidLength);
    Serial.print("  Card UID (HEX): ");
    for (uint8_t i = 0; i < uidLength; i++) {
      Serial.printf("%02X ", uid[i]);
    }
    Serial.println();

    // Print formatted UID string
    Serial.print("  Card UID (String): ");
    for (uint8_t i = 0; i < uidLength; i++) {
      if (uid[i] < 0x10) Serial.print("0");
      Serial.print(uid[i], HEX);
    }
    Serial.println();

    if (uidLength == 4) {
      Serial.println("  Card Type: MIFARE Classic / Standard 4-byte UID");
    } else if (uidLength == 7) {
      Serial.println("  Card Type: MIFARE Ultralight / NTAG2xx / 7-byte UID");
    }
    Serial.println("----------------------------------------");

    // Debounce delay
    delay(1500);
  } else {
    // Heartbeat every 4 seconds if idle
    static unsigned long lastTick = 0;
    if (millis() - lastTick > 4000) {
      lastTick = millis();
      Serial.println("[Heartbeat] Waiting for NFC card tap...");
    }
  }
}
