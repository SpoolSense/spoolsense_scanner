/**
 * OpenPrintTag Adafruit PN532 HAL Adapter
 *
 * Provides NFC HAL implementation for Adafruit_PN532 Arduino library.
 * Wraps mifareultralight_ReadPage/WritePage into opt_nfc_hal_t callbacks.
 */

#ifndef OPENPRINTTAG_ADAFRUIT_PN532_H
#define OPENPRINTTAG_ADAFRUIT_PN532_H

#include "openprinttag_lib.h"
#include <Adafruit_PN532.h>
#include "../../include/SharedSPIBus.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Read a page from NTAG using Adafruit_PN532.
 *
 * @param ctx   Adafruit_PN532 pointer
 * @param page  Page number to read
 * @param buf   Output buffer (4 bytes)
 * @return OPT_OK on success, OPT_ERR_NFC_READ on failure
 */
static inline opt_error_t opt_adafruit_pn532_read_page(void *ctx, uint8_t page, uint8_t *buf) {
    SharedSPIBus::Guard spiGuard;
    if (!spiGuard) return OPT_ERR_NFC_READ;
    Adafruit_PN532 *pn532 = (Adafruit_PN532 *)ctx;
    /* ntag2xx_ReadPage reads 4 bytes, returns count (non-zero = success).
     * Not mifareultralight_ReadPage: that one refuses pages >= 64, and OPT
     * spans 78 pages — same radio command, only the range guard differs. */
    return pn532->ntag2xx_ReadPage(page, buf) ? OPT_OK : OPT_ERR_NFC_READ;
}

/**
 * Write a page to NTAG using Adafruit_PN532.
 *
 * @param ctx   Adafruit_PN532 pointer
 * @param page  Page number to write
 * @param data  Data to write (4 bytes)
 * @return OPT_OK on success, OPT_ERR_NFC_WRITE on failure
 */
static inline opt_error_t opt_adafruit_pn532_write_page(void *ctx, uint8_t page, const uint8_t *data) {
    SharedSPIBus::Guard spiGuard;
    if (!spiGuard) return OPT_ERR_NFC_WRITE;
    Adafruit_PN532 *pn532 = (Adafruit_PN532 *)ctx;
    /* ntag2xx_WritePage returns true on success (allows pages 4..225; the
     * mifareultralight variant refused pages >= 64, too small for OPT) */
    return pn532->ntag2xx_WritePage(page, (uint8_t *)data) ? OPT_OK : OPT_ERR_NFC_WRITE;
}

/**
 * Create a HAL structure for Adafruit_PN532.
 *
 * @param pn532  Initialized Adafruit_PN532 instance
 * @return HAL structure ready for use with opt_read_from_nfc/opt_write_to_nfc
 *
 * Example usage:
 *   Adafruit_PN532 pn532(SS_PIN, &SPI);
 *   pn532.begin();
 *   pn532.SAMConfig();
 *
 *   opt_nfc_hal_t hal = opt_create_adafruit_pn532_hal(&pn532);
 *
 *   opt_tag_t tag;
 *   opt_init(&tag);
 *   opt_read_from_nfc(&tag, &hal, 4, 50);
 *   opt_parse_ndef(&tag);
 */
static inline opt_nfc_hal_t opt_create_adafruit_pn532_hal(Adafruit_PN532 *pn532) {
    opt_nfc_hal_t hal = {
        .read_page = opt_adafruit_pn532_read_page,
        .write_page = opt_adafruit_pn532_write_page,
        .is_present = NULL,
        .user_ctx = pn532
    };
    return hal;
}

#ifdef __cplusplus
}
#endif

#endif /* OPENPRINTTAG_ADAFRUIT_PN532_H */
