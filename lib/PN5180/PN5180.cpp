// NAME: PN5180.cpp
//
// DESC: Implementation of PN5180 class.
//
// Copyright (c) 2018 by Andreas Trappmann. All rights reserved.
//
// This file is part of the PN5180 library for the Arduino environment.
//
// This library is free software; you can redistribute it and/or
// modify it under the terms of the GNU Lesser General Public
// License as published by the Free Software Foundation; either
// version 2.1 of the License, or (at your option) any later version.
//
// This library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
// Lesser General Public License for more details.
//
//#define DEBUG 1

#include <Arduino.h>
#include <SPI.h>
#include "PN5180.h"
#include "Debug.h"
#include "../../include/SharedSPIBus.h"
#if defined(BOARD_SHARED_SPI)
  #include "../../include/BoardPins.h"
#endif

// Use HSPI for PN5180 so VSPI is free for TFT display
// Bus choice is per-target: HSPI is the SPI2 peripheral on classic ESP32 but
// bus 1 = the SPI3 peripheral on ESP32-S3 — the same host LovyanGFX claims for
// the TFT (SPI3_HOST) on the S3-DevKitC. Two drivers arbitrating one SPI
// peripheral corrupts transactions (garbage transceiver states) and can hold
// the bus indefinitely, starving the scan task into a task_wdt reboot. Boards
// whose TFT owns SPI2 instead (S3-Zero) override PN5180_SPI_BUS in build flags.
#ifndef PN5180_SPI_BUS
  #if CONFIG_IDF_TARGET_ESP32S3
    #define PN5180_SPI_BUS FSPI  // bus 0 = SPI2 — matches the FSPI pin naming in BoardPins.h
  #elif CONFIG_IDF_TARGET_ESP32C3 || CONFIG_IDF_TARGET_ESP32C5 || CONFIG_IDF_TARGET_ESP32C6
    #define PN5180_SPI_BUS FSPI  // the C3/C5/C6 expose one general-purpose SPI host
  #else
    #define PN5180_SPI_BUS HSPI  // SPI2 on classic ESP32
  #endif
#endif
#if defined(BOARD_SHARED_SPI)
  // The TFT initializes Arduino's global SPI handle first when enabled. NFC
  // injects that same handle instead of constructing a second SPIClass that
  // would reset the single GP-SPI peripheral.
  #define pn5180_spi SharedSPIBus::bus()
#else
  static SPIClass pn5180_spi(PN5180_SPI_BUS);
#endif

class PN5180SPITransaction {
public:
  PN5180SPITransaction(SPIClass& spi, const SPISettings& settings)
      : guard_(), spi_(spi), active_(static_cast<bool>(guard_)) {
    if (active_) spi_.beginTransaction(settings);
  }

  ~PN5180SPITransaction() {
    if (active_) spi_.endTransaction();
  }

  explicit operator bool() const { return active_; }

private:
  SharedSPIBus::Guard guard_;
  SPIClass& spi_;
  bool active_;
};

// PN5180 1-Byte Direct Commands
// see 11.4.3.3 Host Interface Command List
#define PN5180_WRITE_REGISTER           (0x00)
#define PN5180_WRITE_REGISTER_OR_MASK   (0x01)
#define PN5180_WRITE_REGISTER_AND_MASK  (0x02)
#define PN5180_READ_REGISTER            (0x04)
#define PN5180_WRITE_EEPROM             (0x06)
#define PN5180_READ_EEPROM              (0x07)
#define PN5180_SEND_DATA                (0x09)
#define PN5180_READ_DATA                (0x0A)
#define PN5180_SWITCH_MODE              (0x0B)
#define PN5180_LOAD_RF_CONFIG           (0x11)
#define PN5180_RF_ON                    (0x16)
#define PN5180_RF_OFF                   (0x17)

uint8_t PN5180::readBuffer[508];

PN5180::PN5180(uint8_t SSpin, uint8_t BUSYpin, uint8_t RSTpin,
               int8_t SCKpin, int8_t MISOpin, int8_t MOSIpin) {
  PN5180_NSS = SSpin;
  PN5180_BUSY = BUSYpin;
  PN5180_RST = RSTpin;
  PN5180_SCK = SCKpin;
  PN5180_MISO = MISOpin;
  PN5180_MOSI = MOSIpin;

  /*
   * 11.4.1 Physical Host Interface
   * The interface of the PN5180 to a host microcontroller is based on a SPI interface,
   * extended by signal line BUSY. The maximum SPI speed is 7 Mbps and fixed to CPOL
   * = 0 and CPHA = 0.
   */
  // Settings for PN5180: PN5180_SPI_HZ (7Mbps default), MSB first, SPI_MODE0
  // (CPOL=0, CPHA=0). Clock is overridable via build flag for bench diagnostics.
  PN5180_SPI_SETTINGS = SPISettings(PN5180_SPI_HZ, MSBFIRST, SPI_MODE0);
}

void PN5180::begin() {
  pinMode(PN5180_NSS, OUTPUT);
  pinMode(PN5180_BUSY, INPUT);
  pinMode(PN5180_RST, OUTPUT);

  digitalWrite(PN5180_NSS, HIGH); // disable
  digitalWrite(PN5180_RST, HIGH); // no reset

#if defined(BOARD_SHARED_SPI)
  if (!SharedSPIBus::begin(PN5180_SCK, PN5180_MISO, PN5180_MOSI,
                           PN5180_NSS, PIN_TFT_CS)) {
    Serial.println("PN5180: shared SPI initialization failed");
  }
#else
  if (PN5180_SCK >= 0 && PN5180_MISO >= 0 && PN5180_MOSI >= 0) {
    pn5180_spi.begin(PN5180_SCK, PN5180_MISO, PN5180_MOSI);
  } else {
    pn5180_spi.begin();
  }
#endif
  PN5180DEBUG(F("SPI pinout: "));
  PN5180DEBUG(F("SS=")); PN5180DEBUG(PN5180_NSS);
  PN5180DEBUG(F(", MOSI=")); PN5180DEBUG(PN5180_MOSI);
  PN5180DEBUG(F(", MISO=")); PN5180DEBUG(PN5180_MISO);
  PN5180DEBUG(F(", SCK=")); PN5180DEBUG(PN5180_SCK);
  PN5180DEBUG("\n");
}

void PN5180::end() {
  digitalWrite(PN5180_NSS, HIGH); // disable
#if !defined(BOARD_SHARED_SPI)
  pn5180_spi.end();
#endif
}

/*
 * WRITE_REGISTER - 0x00
 * This command is used to write a 32-bit value (little endian) to a configuration register.
 * The address of the register must exist. If the condition is not fulfilled, an exception is
 * raised.
 */
bool PN5180::writeRegister(uint8_t reg, uint32_t value) {
  uint8_t *p = (uint8_t*)&value;

#ifdef DEBUG
  PN5180DEBUG(F("Write Register 0x"));
  PN5180DEBUG(formatHex(reg));
  PN5180DEBUG(F(", value (LSB first)=0x"));
  for (int i=0; i<4; i++) {
    PN5180DEBUG(formatHex(p[i]));
  }
  PN5180DEBUG("\n");
#endif

  /*
  For all 4 byte command parameter transfers (e.g. register values), the payload
  parameters passed follow the little endian approach (Least Significant Byte first).
   */
  uint8_t buf[6] = { PN5180_WRITE_REGISTER, reg, p[0], p[1], p[2], p[3] };

  bool ok = transceiveCommand(buf, 6);

  return ok;
}

/*
 * WRITE_REGISTER_OR_MASK - 0x01
 * This command modifies the content of a register using a logical OR operation. The
 * content of the register is read and a logical OR operation is performed with the provided
 * mask. The modified content is written back to the register.
 * The address of the register must exist. If the condition is not fulfilled, an exception is
 * raised.
 */
bool PN5180::writeRegisterWithOrMask(uint8_t reg, uint32_t mask) {
  uint8_t *p = (uint8_t*)&mask;

#ifdef DEBUG
  PN5180DEBUG(F("Write Register 0x"));
  PN5180DEBUG(formatHex(reg));
  PN5180DEBUG(F(" with OR mask (LSB first)=0x"));
  for (int i=0; i<4; i++) {
    PN5180DEBUG(formatHex(p[i]));
  }
  PN5180DEBUG("\n");
#endif

  uint8_t buf[6] = { PN5180_WRITE_REGISTER_OR_MASK, reg, p[0], p[1], p[2], p[3] };

  bool ok = transceiveCommand(buf, 6);

  return ok;
}

/*
 * WRITE _REGISTER_AND_MASK - 0x02
 * This command modifies the content of a register using a logical AND operation. The
 * content of the register is read and a logical AND operation is performed with the provided
 * mask. The modified content is written back to the register.
 * The address of the register must exist. If the condition is not fulfilled, an exception is
 * raised.
 */
bool PN5180::writeRegisterWithAndMask(uint8_t reg, uint32_t mask) {
  uint8_t *p = (uint8_t*)&mask;

#ifdef DEBUG
  PN5180DEBUG(F("Write Register 0x"));
  PN5180DEBUG(formatHex(reg));
  PN5180DEBUG(F(" with AND mask (LSB first)=0x"));
  for (int i=0; i<4; i++) {
    PN5180DEBUG(formatHex(p[i]));
  }
  PN5180DEBUG("\n");
#endif

  uint8_t buf[6] = { PN5180_WRITE_REGISTER_AND_MASK, reg, p[0], p[1], p[2], p[3] };

  bool ok = transceiveCommand(buf, 6);

  return ok;
}

/*
 * READ_REGISTER - 0x04
 * This command is used to read the content of a configuration register. The content of the
 * register is returned in the 4 byte response.
 * The address of the register must exist. If the condition is not fulfilled, an exception is
 * raised.
 */
bool PN5180::readRegister(uint8_t reg, uint32_t *value) {
  PN5180DEBUG(F("Reading register 0x"));
  PN5180DEBUG(formatHex(reg));
  PN5180DEBUG(F("...\n"));

  uint8_t cmd[2] = { PN5180_READ_REGISTER, reg };

  *value = 0;  // never leave stale data on a failed transceive
  bool ok = transceiveCommand(cmd, 2, (uint8_t*)value, 4);
  if (!ok) {
    *value = 0;
    return false;
  }

  PN5180DEBUG(F("Register value=0x"));
  PN5180DEBUG(formatHex(*value));
  PN5180DEBUG("\n");

  return true;
}

/*
 * WRITE_EEPROM - 0x06
 */
 bool PN5180::writeEEPROM(uint8_t addr, uint8_t *data, int len) {
   if ((addr > 254) || ((addr+len) > 254)) {
     PN5180DEBUG(F("ERROR: Writing beyond addr 254!\n"));
     return false;
   }

   PN5180DEBUG(F("Writing to EEPROM at 0x"));
   PN5180DEBUG(formatHex(addr));
   PN5180DEBUG(F(", size="));
   PN5180DEBUG(len);
   PN5180DEBUG(F("...\n"));

   uint8_t buffer[len+2];
   buffer[0] = PN5180_WRITE_EEPROM;
   buffer[1] = addr;
   for (int i=0; i<len; i++) {
     buffer[2+i] = data[i];
   }

   bool ok = transceiveCommand(buffer, len+2);

   return ok;
 }

/*
 * READ_EEPROM - 0x07
 * This command is used to read data from EEPROM memory area. The field 'Address'
 * indicates the start address of the read operation. The field Length indicates the number
 * of bytes to read. The response contains the data read from EEPROM (content of the
 * EEPROM); The data is read in sequentially increasing order starting with the given
 * address.
 * EEPROM Address must be in the range from 0 to 254, inclusive. Read operation must
 * not go beyond EEPROM address 254. If the condition is not fulfilled, an exception is
 * raised.
 */
bool PN5180::readEEprom(uint8_t addr, uint8_t *buffer, int len) {
  if ((addr > 254) || ((addr+len) > 254)) {
    PN5180DEBUG(F("ERROR: Reading beyond addr 254!\n"));
    return false;
  }

  PN5180DEBUG(F("Reading EEPROM at 0x"));
  PN5180DEBUG(formatHex(addr));
  PN5180DEBUG(F(", size="));
  PN5180DEBUG(len);
  PN5180DEBUG(F("...\n"));

  uint8_t cmd[3] = { PN5180_READ_EEPROM, addr, (uint8_t)len };

  bool eepromOk = transceiveCommand(cmd, 3, buffer, len);
  if (!eepromOk) return false;

#ifdef DEBUG
  PN5180DEBUG(F("EEPROM values: "));
  for (int i=0; i<len; i++) {
    PN5180DEBUG(formatHex(buffer[i]));
    PN5180DEBUG(" ");
  }
  PN5180DEBUG("\n");
#endif

  return true;
}

/*
 * SEND_DATA - 0x09
 * This command writes data to the RF transmission buffer and starts the RF transmission.
 * The parameter 'Number of valid bits in last Byte' indicates the exact number of bits to be
 * transmitted for the last byte (for non-byte aligned frames).
 * Precondition: Host shall configure the Transceiver by setting the register
 * SYSTEM_CONFIG.COMMAND to 0x3 before using the SEND_DATA command, as
 * the command SEND_DATA is only writing data to the transmission buffer and starts the
 * transmission but does not perform any configuration.
 * The size of 'Tx Data' field must be in the range from 0 to 260, inclusive (the 0 byte length
 * allows a symbol only transmission when the TX_DATA_ENABLE is cleared).'Number of
 * valid bits in last Byte' field must be in the range from 0 to 7. The command must not be
 * called during an ongoing RF transmission. Transceiver must be in 'WaitTransmit' state
 * with 'Transceive' command set. If the condition is not fulfilled, an exception is raised.
 */
bool PN5180::sendData(uint8_t *data, int len, uint8_t validBits) {
  if (len > 260) {
    PN5180DEBUG(F("ERROR: sendData with more than 260 bytes is not supported!\n"));
    return false;
  }

  uint8_t buffer[len+2];
  buffer[0] = PN5180_SEND_DATA;
  buffer[1] = validBits;
  for (int i=0; i<len; i++) {
    buffer[2+i] = data[i];
  }

  // Idle → Transceive: state machine needs a moment to transition to WaitTransmit
  writeRegisterWithAndMask(SYSTEM_CONFIG, 0xfffffff8);
  writeRegisterWithOrMask(SYSTEM_CONFIG, 0x00000003);

  // Stale IRQs from prior commands can cause false failures on the next transaction
  clearIRQStatus(0xffffffff);

  // Poll for WaitTransmit — transition takes microseconds but isn't instant.
  // Old code checked once and failed; this matches the NXP reference implementation.
  unsigned long deadline = millis() + 10;
  PN5180TransceiveStat transceiveState;
  do {
    transceiveState = getTransceiveState();
    if (transceiveState == PN5180_TS_WaitTransmit) break;
    delayMicroseconds(50);
  } while (millis() < deadline);

  if (transceiveState != PN5180_TS_WaitTransmit) {
    Serial.printf("PN5180: sendData FAILED - transceiver state=%d (expected %d=WaitTransmit)\n",
                  (int)transceiveState, (int)PN5180_TS_WaitTransmit);
    return false;
  }

  bool ok = transceiveCommand(buffer, len+2);

  return ok;
}

/*
 * READ_DATA - 0x0A
 * This command reads data from the RF reception buffer, after a successful reception.
 * The RX_STATUS register contains the information to verify if the reception had been
 * successful. The data is available within the response of the command. The host controls
 * the number of bytes to be read via the SPI interface.
 * The RF data had been successfully received. In case the instruction is executed without
 * preceding an RF data reception, no exception is raised but the data read back from the
 * reception buffer is invalid. If the condition is not fulfilled, an exception is raised.
 */
uint8_t * PN5180::readData(int len, uint8_t *buffer /* = NULL */) {
  if (len > 508) {
    Serial.println(F("*** FATAL: Reading more than 508 bytes is not supported!"));
    return 0L;
  }
  if (NULL == buffer) {
    buffer = readBuffer;
  }

  PN5180DEBUG(F("Reading Data (len="));
  PN5180DEBUG(len);
  PN5180DEBUG(F(")...\n"));

  uint8_t cmd[2] = { PN5180_READ_DATA, 0x00 };

  bool readOk = transceiveCommand(cmd, 2, buffer, len);
  if (!readOk) return 0L;

#ifdef DEBUG
  PN5180DEBUG(F("Data read: "));
  for (int i=0; i<len; i++) {
    PN5180DEBUG(formatHex(buffer[i]));
    PN5180DEBUG(" ");
  }
  PN5180DEBUG("\n");
#endif

  return readBuffer;
}

/*
 * LOAD_RF_CONFIG - 0x11
 * Parameter 'Transmitter Configuration' must be in the range from 0x0 - 0x1C, inclusive. If
 * the transmitter parameter is 0xFF, transmitter configuration is not changed.
 * Field 'Receiver Configuration' must be in the range from 0x80 - 0x9C, inclusive. If the
 * receiver parameter is 0xFF, the receiver configuration is not changed. If the condition is
 * not fulfilled, an exception is raised.
 * The transmitter and receiver configuration shall always be configured for the same
 * transmission/reception speed. No error is returned in case this condition is not taken into
 * account.
 *
 * Transmitter: RF   Protocol          Speed     Receiver: RF    Protocol    Speed
 * configuration                       (kbit/s)  configuration               (kbit/s)
 * byte (hex)                                    byte (hex)
 * ----------------------------------------------------------------------------------------------
 * ->0D              ISO 15693 ASK100  26        8D              ISO 15693   26
 *   0E              ISO 15693 ASK10   26        8E              ISO 15693   53
 */
bool PN5180::loadRFConfig(uint8_t txConf, uint8_t rxConf) {
  PN5180DEBUG(F("Load RF-Config: txConf="));
  PN5180DEBUG(formatHex(txConf));
  PN5180DEBUG(F(", rxConf="));
  PN5180DEBUG(formatHex(rxConf));
  PN5180DEBUG("\n");

  uint8_t cmd[3] = { PN5180_LOAD_RF_CONFIG, txConf, rxConf };

  bool ok = transceiveCommand(cmd, 3);

  return ok;
}

/*
 * MFC_AUTHENTICATE - 0x0C
 * MIFARE Classic authentication using the PN5180 internal Crypto1 engine.
 * Returns true if authentication succeeded (response byte == 0).
 */
bool PN5180::mfcAuthenticate(const uint8_t *key, uint8_t keyType, uint8_t blockNo, const uint8_t *uid) {
  // PN5180 host command 0x0C: [cmd][key(6)][keyType(0x60/0x61)][block][uid(4)]
  uint8_t cmd[13];
  cmd[0] = 0x0C;
  memcpy(&cmd[1], key, 6);
  cmd[7] = keyType;
  cmd[8] = blockNo;
  memcpy(&cmd[9], uid, 4);

  uint8_t response = 0xFF;
  bool ok = transceiveCommand(cmd, 13, &response, 1);

  if (!ok || response != 0x00) {
    Serial.printf("PN5180: mfcAuthenticate block %d failed (ok=%d resp=0x%02X)\n", blockNo, ok, response);
    writeRegisterWithAndMask(SYSTEM_CONFIG, 0xFFFFFFBF);  // clear Crypto1
    writeRegisterWithAndMask(SYSTEM_CONFIG, 0xFFFFFFF8);  // force idle
    clearIRQStatus(0x000FFFFF);
  }
  return ok && (response == 0x00);
}

/*
 * RF_ON - 0x16
 * This command is used to switch on the internal RF field. If enabled the TX_RFON_IRQ is
 * set after the field is switched on.
 */
bool PN5180::setRF_on() {
  PN5180DEBUG(F("Set RF ON\n"));

  uint8_t cmd[2] = { PN5180_RF_ON, 0x00 };

  bool rfOk = transceiveCommand(cmd, 2);
  if (!rfOk) return false;

  {
    unsigned long t = millis();
    while (0 == (TX_RFON_IRQ_STAT & getIRQStatus())) { // wait for RF field to set up
      // TX_RFON is a TRANSITION interrupt — commanding RF on while the field
      // is already energized produces no edge and no IRQ. Check the live
      // field state before calling it a failure: an already-on field is
      // success (this fired as a false "TIMEOUT TX_RFON_IRQ" on every idle
      // scan cycle once the timeout logs became always-on).
      uint32_t rfStatus = 0;
      if (readRegister(RF_STATUS, &rfStatus) && (rfStatus & (1UL << 20))) {
        return true;  // TX_RF_STATUS: field is on
      }
      if (millis() - t > 500) {
        Serial.println("PN5180: TIMEOUT TX_RFON_IRQ");
        return false;
      }
    }
  }
  clearIRQStatus(TX_RFON_IRQ_STAT);
  return true;
}

/*
 * RF_OFF - 0x17
 * This command is used to switch off the internal RF field. If enabled, the TX_RFOFF_IRQ
 * is set after the field is switched off.
 */
bool PN5180::setRF_off() {
  PN5180DEBUG(F("Set RF OFF\n"));

  uint8_t cmd[2] { PN5180_RF_OFF, 0x00 };

  bool rfOffOk = transceiveCommand(cmd, 2);
  if (!rfOffOk) return false;

  {
    unsigned long t = millis();
    while (0 == (TX_RFOFF_IRQ_STAT & getIRQStatus())) { // wait for RF field to shut down
      // Mirror of setRF_on: no edge when the field was already off
      uint32_t rfStatus = 0;
      if (readRegister(RF_STATUS, &rfStatus) && !(rfStatus & (1UL << 20))) {
        return true;  // field is off
      }
      if (millis() - t > 500) {
        Serial.println("PN5180: TIMEOUT TX_RFOFF_IRQ");
        return false;
      }
    }
  }
  clearIRQStatus(TX_RFOFF_IRQ_STAT);
  return true;
}

//---------------------------------------------------------------------------------------------

/*
11.4.3.1 A Host Interface Command consists of either 1 or 2 SPI frames depending whether the
host wants to write or read data from the PN5180. An SPI Frame consists of multiple
bytes.

All commands are packed into one SPI Frame. An SPI Frame consists of multiple bytes.
No NSS toggles allowed during sending of an SPI frame.

For all 4 byte command parameter transfers (e.g. register values), the payload
parameters passed follow the little endian approach (Least Significant Byte first).

Direct Instructions are built of a command code (1 Byte) and the instruction parameters
(max. 260 bytes). The actual payload size depends on the instruction used.
Responses to direct instructions contain only a payload field (no header).
All instructions are bound to conditions. If at least one of the conditions is not fulfilled, an exception is
raised. In case of an exception, the IRQ line of PN5180 is asserted and corresponding interrupt
status register contain information on the exception.
*/

/*
 * A Host Interface Command consists of either 1 or 2 SPI frames depending whether the
 * host wants to write or read data from the PN5180. An SPI Frame consists of multiple
 * bytes.
 * All commands are packed into one SPI Frame. An SPI Frame consists of multiple bytes.
 * No NSS toggles allowed during sending of an SPI frame.
 * For all 4 byte command parameter transfers (e.g. register values), the payload
 * parameters passed follow the little endian approach (Least Significant Byte first).
 * The BUSY line is used to indicate that the system is BUSY and cannot receive any data
 * from a host. Recommendation for the BUSY line handling by the host:
 * 1. Assert NSS to Low
 * 2. Perform Data Exchange
 * 3. Wait until BUSY is high
 * 4. Deassert NSS
 * 5. Wait until BUSY is low
 * If there is a parameter error, the IRQ is set to ACTIVE and a GENERAL_ERROR_IRQ is set.
 */
// Once a BUSY handshake times out, the PN5180 state machine is wedged and
// every further transaction will also grind to its full timeout. detectTag
// chains 15-20 transactions, so grinding turns one scan iteration into 30s+
// and starves the scan task's watchdog (bench 2026-07-06). Fail everything
// fast instead until a hardware reset clears the flag — recovery then happens
// within one 50ms scan cycle.
static volatile bool pn5180_busWedged = false;

static PN5180::TimeoutLogSink pn5180_timeoutSink = nullptr;

void PN5180::setTimeoutLogSink(TimeoutLogSink sink) {
  pn5180_timeoutSink = sink;
}

// One line per handshake timeout, routed through the installed sink so the
// application can mirror it into its web log (#293).
static void logBusyTimeout(const char* phase) {
  if (pn5180_timeoutSink) {
    pn5180_timeoutSink(phase);
  } else {
    Serial.print("PN5180: TIMEOUT ");
    Serial.println(phase);
  }
}

void PN5180::clearBusWedged() {
  pn5180_busWedged = false;
}

bool PN5180::isBusWedged() {
  return pn5180_busWedged;
}

bool PN5180::transceiveCommand(uint8_t *sendBuffer, size_t sendBufferLen, uint8_t *recvBuffer, size_t recvBufferLen) {
  if (pn5180_busWedged) return false;

  // Hold the board-level recursive guard for the complete logical command,
  // including all bounded BUSY waits. This prevents the TFT or PN532 from
  // changing host settings between the command and response frames.
  PN5180SPITransaction transaction(pn5180_spi, PN5180_SPI_SETTINGS);
  if (!transaction) {
    Serial.println("PN5180: shared SPI lock timeout");
    return false;
  }
#ifdef DEBUG
  PN5180DEBUG(F("Sending SPI frame: '"));
  for (size_t i=0; i<sendBufferLen; i++) {
    if (i>0) PN5180DEBUG(" ");
    PN5180DEBUG(formatHex(sendBuffer[i]));
  }
  PN5180DEBUG("'\n");
#endif

  // 0.
  {
    unsigned long t = millis();
    while (LOW != digitalRead(PN5180_BUSY)) { // wait until busy is low
      if (millis() - t > 1000) {
        logBusyTimeout("BUSY-LOW pre-send");
        pn5180_busWedged = true;
        return false;
      }
    }
  }
  // 1.
  digitalWrite(PN5180_NSS, LOW); delay(2);
  // 2.
  for (size_t i=0; i<sendBufferLen; i++) {
    pn5180_spi.transfer(sendBuffer[i]);
  }
  // 3.
  {
    unsigned long t = millis();
    while(HIGH != digitalRead(PN5180_BUSY)) {  // wait until BUSY is high
      if (millis() - t > 1000) {
        logBusyTimeout("BUSY-HIGH post-send");
        digitalWrite(PN5180_NSS, HIGH);
        pn5180_busWedged = true;
        return false;
      }
    }
  }
  // 4.
  digitalWrite(PN5180_NSS, HIGH); delay(1);
  // 5.
  {
    unsigned long t = millis();
    while (LOW != digitalRead(PN5180_BUSY)) { // wait until BUSY is low
      if (millis() - t > 1000) {
        logBusyTimeout("BUSY-LOW post-send");
        pn5180_busWedged = true;
        return false;
      }
    }
  }

  // check, if write-only
  //
  if ((0 == recvBuffer) || (0 == recvBufferLen)) return true;
  PN5180DEBUG(F("Receiving SPI frame...\n"));

  // 1.
  digitalWrite(PN5180_NSS, LOW); delay(2);
  // 2.
  for (size_t i=0; i<recvBufferLen; i++) {
    recvBuffer[i] = pn5180_spi.transfer(0xff);
  }
  // 3.
  {
    unsigned long t = millis();
    while(HIGH != digitalRead(PN5180_BUSY)) {  // wait until BUSY is high
      if (millis() - t > 1000) {
        logBusyTimeout("BUSY-HIGH post-recv");
        pn5180_busWedged = true;
        digitalWrite(PN5180_NSS, HIGH);
        return false;
      }
    }
  }
  // 4.
  digitalWrite(PN5180_NSS, HIGH); delay(1);
  // 5.
  {
    unsigned long t = millis();
    while(LOW != digitalRead(PN5180_BUSY)) {  // wait until BUSY is low
      if (millis() - t > 1000) {
        logBusyTimeout("BUSY-LOW post-recv");
        pn5180_busWedged = true;
        return false;
      }
    }
  }

#ifdef DEBUG
  PN5180DEBUG(F("Received: "));
  for (size_t i=0; i<recvBufferLen; i++) {
    if (i > 0) PN5180DEBUG(" ");
    PN5180DEBUG(formatHex(recvBuffer[i]));
  }
  PN5180DEBUG("'\n");
#endif

  return true;
}

/*
 * Reset NFC device
 */
bool PN5180::reset() {
  digitalWrite(PN5180_RST, LOW);  // at least 10us required
  delay(10);
  digitalWrite(PN5180_RST, HIGH); // 2ms to ramp up required
  delay(10);
  clearBusWedged();  // fresh chip state — allow transactions again

  unsigned long t = millis();
  while (0 == (IDLE_IRQ_STAT & getIRQStatus())) { // wait for system to start up
    if (millis() - t > 1000) {
      Serial.println("PN5180: TIMEOUT IDLE_IRQ after reset");
      return false;
    }
  }

  clearIRQStatus(0xffffffff); // clear all flags
  return true;
}

/**
 * @name  getInterrrupt
 * @desc  read interrupt status register and clear interrupt status
 */
uint32_t PN5180::getIRQStatus() {
  PN5180DEBUG(F("Read IRQ-Status register...\n"));

  uint32_t irqStatus;
  readRegister(IRQ_STATUS, &irqStatus);

  PN5180DEBUG(F("IRQ-Status=0x"));
  PN5180DEBUG(formatHex(irqStatus));
  PN5180DEBUG("\n");

  return irqStatus;
}

bool PN5180::clearIRQStatus(uint32_t irqMask) {
  PN5180DEBUG(F("Clear IRQ-Status with mask=x"));
  PN5180DEBUG(formatHex(irqMask));
  PN5180DEBUG("\n");

  return writeRegister(IRQ_CLEAR, irqMask);
}

/*
 * Get TRANSCEIVE_STATE from RF_STATUS register
 */
#ifdef DEBUG
extern void showIRQStatus(uint32_t);
#endif

PN5180TransceiveStat PN5180::getTransceiveState() {
  PN5180DEBUG(F("Get Transceive state...\n"));

  uint32_t rfStatus;
  if (!readRegister(RF_STATUS, &rfStatus)) {
#ifdef DEBUG
    showIRQStatus(getIRQStatus());
#endif
    PN5180DEBUG(F("ERROR reading RF_STATUS register.\n"));
    return PN5180TransceiveStat(0);
  }

  /*
   * TRANSCEIVE_STATEs:
   *  0 - idle
   *  1 - wait transmit
   *  2 - transmitting
   *  3 - wait receive
   *  4 - wait for data
   *  5 - receiving
   *  6 - loopback
   *  7 - reserved
   */
  uint8_t state = ((rfStatus >> 24) & 0x07);
  PN5180DEBUG(F("TRANSCEIVE_STATE=0x"));
  PN5180DEBUG(formatHex(state));
  PN5180DEBUG("\n");

  return PN5180TransceiveStat(state);
}
