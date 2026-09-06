// NAME: PN5180.h
//
// DESC: NFC Communication with NXP Semiconductors PN5180 module for Arduino.
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
#ifndef PN5180_H
#define PN5180_H

#include <SPI.h>

// PN5180 datasheet 11.4.1: SPI maximum is 7 Mbps, fixed CPOL=0 / CPHA=0. The
// default stays at the maximum; override with -DPN5180_SPI_HZ=<hz> for bench
// diagnostics on marginal wiring (shared-bus breadboard rigs, long jumpers).
#ifndef PN5180_SPI_HZ
#define PN5180_SPI_HZ 7000000UL
#endif
#if PN5180_SPI_HZ < 1 || PN5180_SPI_HZ > 7000000
#error "PN5180_SPI_HZ must be 1..7000000 — the PN5180 datasheet maximum is 7 Mbps"
#endif

// PN5180 Registers
#define SYSTEM_CONFIG       (0x00)
#define IRQ_ENABLE          (0x01)
#define IRQ_STATUS          (0x02)
#define IRQ_CLEAR           (0x03)
#define TRANSCEIVE_CONTROL  (0x04)
#define TIMER1_RELOAD       (0x0c)
#define TIMER1_CONFIG       (0x0f)
#define RX_WAIT_CONFIG      (0x11)
#define CRC_RX_CONFIG       (0x12)
#define RX_STATUS           (0x13)
#define RX_BYTES_RECEIVED_MASK (0x1FF)
#define CRC_TX_CONFIG       (0x19)
#define RF_STATUS           (0x1d)
#define SYSTEM_STATUS       (0x24)
#define TEMP_CONTROL        (0x25)
#define AGC_REF_CONFIG      (0x26)
#define DPC_CONFIG          (0x27)
#define EMD_CONTROL         (0x28)
#define ANT_CONTROL         (0x29)

// PN5180 EEPROM Addresses.
// Prefixed: the unprefixed FIRMWARE_VERSION collided with the application's
// release-string macro (-DFIRMWARE_VERSION="x.y.z"), warning on every build and
// leaving whichever definition won to include order. Any use of the app macro
// after this header would silently have compiled as the EEPROM address 0x12.
#define DIE_IDENTIFIER          (0x00)
#define PN5180_PRODUCT_VERSION  (0x10)
#define PN5180_FIRMWARE_VERSION (0x12)
#define PN5180_EEPROM_VERSION   (0x14)
#define IRQ_PIN_CONFIG          (0x1A)

enum PN5180TransceiveStat {
  PN5180_TS_Idle = 0,
  PN5180_TS_WaitTransmit = 1,
  PN5180_TS_Transmitting = 2,
  PN5180_TS_WaitReceive = 3,
  PN5180_TS_WaitForData = 4,
  PN5180_TS_Receiving = 5,
  PN5180_TS_LoopBack = 6,
  PN5180_TS_RESERVED = 7
};

// PN5180 IRQ_STATUS
#define RX_IRQ_STAT         (1<<0)  // End of RF rececption IRQ
#define TX_IRQ_STAT         (1<<1)  // End of RF transmission IRQ
#define IDLE_IRQ_STAT       (1<<2)  // IDLE IRQ
#define RFOFF_DET_IRQ_STAT  (1<<6)  // RF Field OFF detection IRQ
#define RFON_DET_IRQ_STAT   (1<<7)  // RF Field ON detection IRQ
#define TX_RFOFF_IRQ_STAT   (1<<8)  // RF Field OFF in PCD IRQ
#define TX_RFON_IRQ_STAT    (1<<9)  // RF Field ON in PCD IRQ
#define RX_SOF_DET_IRQ_STAT (1<<14) // RF SOF Detection IRQ
#define GENERAL_ERROR_IRQ_STAT (1<<17) // General error IRQ (protocol/parameter errors)

class PN5180 {
private:
  uint8_t PN5180_NSS;   // active low
  uint8_t PN5180_BUSY;
  uint8_t PN5180_RST;
  int8_t PN5180_SCK;
  int8_t PN5180_MISO;
  int8_t PN5180_MOSI;

  SPISettings PN5180_SPI_SETTINGS;
  static uint8_t readBuffer[508];

public:
  PN5180(uint8_t SSpin, uint8_t BUSYpin, uint8_t RSTpin,
         int8_t SCKpin = -1, int8_t MISOpin = -1, int8_t MOSIpin = -1);

  void begin();
  void end();

  /*
   * PN5180 direct commands with host interface
   */
public:
  /* cmd 0x00 */
  bool writeRegister(uint8_t reg, uint32_t value);
  /* cmd 0x01 */
  bool writeRegisterWithOrMask(uint8_t addr, uint32_t mask);
  /* cmd 0x02 */
  bool writeRegisterWithAndMask(uint8_t addr, uint32_t mask);

  /* cmd 0x04 */
  bool readRegister(uint8_t reg, uint32_t *value);

  /* cmd 0x06 */
  bool writeEEPROM(uint8_t addr, uint8_t *data, int len);

  /* cmd 0x07 */
  bool readEEprom(uint8_t addr, uint8_t *buffer, int len);

  /* cmd 0x09 */
  bool sendData(uint8_t *data, int len, uint8_t validBits = 0);
  /* cmd 0x0a */
  uint8_t * readData(int len, uint8_t *buffer = NULL);

  /* cmd 0x0C */
  bool mfcAuthenticate(const uint8_t *key, uint8_t keyType, uint8_t blockNo, const uint8_t *uid);

  /* cmd 0x11 */
  bool loadRFConfig(uint8_t txConf, uint8_t rxConf);

  /* cmd 0x16 */
  bool setRF_on();
  /* cmd 0x17 */
  bool setRF_off();

  /*
   * Helper functions
   */
public:
  bool reset();

  uint32_t getIRQStatus();
  bool clearIRQStatus(uint32_t irqMask);

  PN5180TransceiveStat getTransceiveState();

  /*
   * Private methods, called within an SPI transaction
   */
public:
  // Clear the bus-wedged fail-fast latch after a hardware reset
  static void clearBusWedged();

  // Read the bus-wedged fail-fast latch state (for diagnostics).
  static bool isBusWedged();

  // Sink for the BUSY-handshake timeout lines (the wedge-latch sites in
  // transceiveCommand). The library stays application-agnostic: with no sink
  // installed the lines go to Serial as before; the application may install
  // one to mirror them elsewhere (e.g. a web log ring). The sink owns the
  // whole line — the library does not also print. `phase` names the
  // handshake, e.g. "BUSY-LOW pre-send".
  typedef void (*TimeoutLogSink)(const char* phase);
  static void setTimeoutLogSink(TimeoutLogSink sink);

private:
  bool transceiveCommand(uint8_t *sendBuffer, size_t sendBufferLen, uint8_t *recvBuffer = 0, size_t recvBufferLen = 0);

};

#endif /* PN5180_H */
