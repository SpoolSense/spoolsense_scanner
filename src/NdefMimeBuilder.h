#ifndef NDEF_MIME_BUILDER_H
#define NDEF_MIME_BUILDER_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

// Build one MIME media record inside an NDEF TLV and pad to a Type 2 page.
// Returns the padded byte count, or zero when the destination is too small.
static inline uint16_t buildNdefMimeTlv(const char* mimeType, const uint8_t* payload,
                                        uint16_t payloadLen, uint8_t* outBuf,
                                        uint16_t outBufSize) {
    if (mimeType == nullptr || payload == nullptr || outBuf == nullptr) return 0;
    size_t mimeSize = strlen(mimeType);
    if (mimeSize > UINT8_MAX) return 0;
    uint8_t mimeLen = (uint8_t)mimeSize;
    bool shortRecord = payloadLen <= UINT8_MAX;
    uint16_t recordLen = (uint16_t)(2 + (shortRecord ? 1 : 4) + mimeLen + payloadLen);
    bool longTlv = recordLen > 254;
    uint16_t totalSize = (uint16_t)(1 + (longTlv ? 3 : 1) + recordLen + 1);
    uint16_t paddedSize = (uint16_t)(totalSize + ((4 - (totalSize % 4)) % 4));
    if (paddedSize > outBufSize) return 0;

    uint16_t i = 0;
    outBuf[i++] = 0x03;
    if (longTlv) {
        outBuf[i++] = 0xFF;
        outBuf[i++] = (uint8_t)(recordLen >> 8);
        outBuf[i++] = (uint8_t)recordLen;
    } else {
        outBuf[i++] = (uint8_t)recordLen;
    }

    outBuf[i++] = (uint8_t)(0xC0 | 0x02 | (shortRecord ? 0x10 : 0));
    outBuf[i++] = mimeLen;
    if (shortRecord) {
        outBuf[i++] = (uint8_t)payloadLen;
    } else {
        outBuf[i++] = 0;
        outBuf[i++] = 0;
        outBuf[i++] = (uint8_t)(payloadLen >> 8);
        outBuf[i++] = (uint8_t)payloadLen;
    }
    memcpy(outBuf + i, mimeType, mimeLen);
    i += mimeLen;
    memcpy(outBuf + i, payload, payloadLen);
    i += payloadLen;
    outBuf[i++] = 0xFE;
    while (i < paddedSize) outBuf[i++] = 0;
    return paddedSize;
}

#endif
