#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

// What a Type 2 writer found on the tag it is about to write.
enum class Type2Content : uint8_t {
    Blank,        // every page the write may touch reads empty
    SameFormat,   // already holds this writer's own format — an update
    OtherFormat,  // a readable record of another format
    UnknownData,  // fully read, but nothing the scanner can interpret
    Unreadable,   // the read failed, or the tag's size is not known
};

// Blank and SameFormat lose nothing. OtherFormat and UnknownData destroy what
// is there, so they need the caller's explicit confirmation for this tag.
// Unreadable is never writable: nothing is known about what would be lost.
inline bool type2WriteAllowed(Type2Content content, bool confirmReplace) {
    switch (content) {
        case Type2Content::Blank:
        case Type2Content::SameFormat:
            return true;
        case Type2Content::OtherFormat:
        case Type2Content::UnknownData:
            return confirmReplace;
        case Type2Content::Unreadable:
        default:
            return false;
    }
}

// Short reason for the write-outcome record. The pages key on the
// "replace needed" prefix to offer the confirmation.
inline const char* type2RefusalReason(Type2Content content) {
    switch (content) {
        case Type2Content::OtherFormat: return "replace needed: other format";
        case Type2Content::UnknownData: return "replace needed: unknown data";
        default:                        return "tag unreadable";
    }
}

// The first user-memory window every Type 2 writer reads: pages 4-13.
static const uint16_t TYPE2_WINDOW_BYTES = 40;

// A reader can return the pages it got before a read failed part-way (PN5180
// does). The blank scan starts after this window, so a short window leaves
// pages nobody looked at, and "blank" from it proves nothing.
inline bool type2WindowComplete(uint16_t windowLen) {
    return windowLen >= TYPE2_WINDOW_BYTES;
}

// What an NDEF media record, or the raw window, identifies as.
enum class Type2Format : uint8_t { TigerTag, OpenTag3D, OpenSpool, JsonRecord, OtherNdef };

// What an NDEF media record is, from its MIME type alone. Only an exact match
// names anything: a record of any other type is OtherNdef, so no writer can
// take an unknown record for its own and update it unasked. The JSON MIME
// names a JsonRecord, never OpenSpool — every application writes JSON, and
// only the payload says whose it is (openSpoolProbeResult).
inline Type2Format type2FormatFromMime(const char* mime, size_t mimeLen,
                                       const char* ot3dMime, const char* jsonMime) {
    if (mime != nullptr && ot3dMime != nullptr &&
        mimeLen == strlen(ot3dMime) && memcmp(mime, ot3dMime, mimeLen) == 0) {
        return Type2Format::OpenTag3D;
    }
    if (mime != nullptr && jsonMime != nullptr &&
        mimeLen == strlen(jsonMime) && memcmp(mime, jsonMime, mimeLen) == 0) {
        return Type2Format::JsonRecord;
    }
    return Type2Format::OtherNdef;
}

// A tag that already holds the writer's own format is an update; any other
// recognised or unrecognised record is another format.
inline Type2Content type2ContentForFormat(Type2Format found, Type2Format writer) {
    bool named = found != Type2Format::OtherNdef && found != Type2Format::JsonRecord;
    return (named && found == writer) ? Type2Content::SameFormat
                                      : Type2Content::OtherFormat;
}

// Result of reading a JSON record's whole payload and giving it to the
// OpenSpool parser.
enum class OpenSpoolProbe : uint8_t { IsOpenSpool, NotOpenSpool, ShortRead };

//   declaredLen — payload length the record declares
//   gotLen      — payload bytes that came back
//   bufSize     — size of the buffer they were read into
//   parsed      — the OpenSpool parser accepted those bytes
// A payload larger than the buffer is one the scanner's own reader clips and
// then fails to parse, so to this firmware it is not OpenSpool. A parse is
// only trusted when every declared byte was read.
inline OpenSpoolProbe openSpoolProbeResult(uint32_t declaredLen, uint16_t gotLen,
                                           size_t bufSize, bool parsed) {
    if (declaredLen == 0 || declaredLen > bufSize) return OpenSpoolProbe::NotOpenSpool;
    if (gotLen < declaredLen) return OpenSpoolProbe::ShortRead;
    return parsed ? OpenSpoolProbe::IsOpenSpool : OpenSpoolProbe::NotOpenSpool;
}

// Only the OpenSpool writer can own a JSON record, and only a proven one.
inline Type2Content type2ContentForJsonRecord(Type2Format writer, OpenSpoolProbe probe) {
    if (writer != Type2Format::OpenSpool) return Type2Content::OtherFormat;
    switch (probe) {
        case OpenSpoolProbe::IsOpenSpool:  return Type2Content::SameFormat;
        case OpenSpoolProbe::NotOpenSpool: return Type2Content::OtherFormat;
        default:                           return Type2Content::Unreadable;
    }
}

// Whether the NDEF message in a Type 2 user-memory image is ONE media record
// and nothing else. Every NDEF writer here re-emits a single record followed
// by a terminator, which erases any other record, TLV or byte on the tag — so
// "my own format" is only an update when that record is all there is.
enum class NdefShape : uint8_t {
    SoleRecord,  // one MB|ME media record fills the NDEF TLV; a terminator follows
    Companions,  // other records, TLVs or bytes share the tag, or it is malformed
    TailUnseen,  // well-formed so far; the byte after the TLV is past this image
    ZeroTail,    // zeros follow the TLV: sole only if the rest of memory is zero
};

// What the first byte after the NDEF TLV says. A terminator ends the tag's
// data. The terminator is optional, so zero may be padding — or the NULL TLVs
// in front of something else.
inline NdefShape ndefTailShape(uint8_t byteAfterTlv) {
    if (byteAfterTlv == 0xFE) return NdefShape::SoleRecord;
    if (byteAfterTlv == 0x00) return NdefShape::ZeroTail;
    return NdefShape::Companions;
}

// data = user memory from page 4. *tlvEndOut = offset of the first byte after
// the NDEF TLV, valid for every result but Companions. Anything this cannot
// prove is Companions: the caller then asks before it writes.
inline NdefShape ndefMessageShape(const uint8_t* data, uint16_t len, uint16_t* tlvEndOut) {
    if (data == nullptr || tlvEndOut == nullptr) return NdefShape::Companions;
    uint16_t pos = 0;
    while (pos < len) {
        uint8_t tlvType = data[pos++];
        if (tlvType == 0x00) continue;
        if (tlvType == 0xFE || pos >= len) return NdefShape::Companions;

        uint32_t tlvLen = data[pos++];
        if (tlvLen == 0xFF) {
            if (pos + 2 > len) return NdefShape::Companions;
            tlvLen = ((uint32_t)data[pos] << 8) | data[pos + 1];
            pos += 2;
        }
        // Lock and memory control TLVs are the tag's own housekeeping.
        if (tlvType == 0x01 || tlvType == 0x02) {
            if (tlvLen > (uint32_t)(len - pos)) return NdefShape::Companions;
            pos += (uint16_t)tlvLen;
            continue;
        }
        if (tlvType != 0x03) return NdefShape::Companions;

        uint16_t valueStart = pos;
        if ((uint32_t)valueStart + 3 > len) return NdefShape::Companions;
        uint8_t flags = data[valueStart];
        // MB and ME set, CF and IL clear, TNF = media type.
        if ((flags & 0xE8) != 0xC0 || (flags & 0x07) != 0x02) return NdefShape::Companions;

        uint8_t typeLen = data[valueStart + 1];
        uint32_t headerLen;
        uint32_t payloadLen;
        if (flags & 0x10) {
            headerLen = 3;
            payloadLen = data[valueStart + 2];
        } else {
            if ((uint32_t)valueStart + 6 > len) return NdefShape::Companions;
            headerLen = 6;
            payloadLen = ((uint32_t)data[valueStart + 2] << 24) |
                         ((uint32_t)data[valueStart + 3] << 16) |
                         ((uint32_t)data[valueStart + 4] << 8) |
                         data[valueStart + 5];
        }
        if (payloadLen > 0xFFFF) return NdefShape::Companions;
        if (headerLen + typeLen + payloadLen != tlvLen) return NdefShape::Companions;

        uint32_t end = (uint32_t)valueStart + tlvLen;
        if (end > 0xFFFF) return NdefShape::Companions;
        *tlvEndOut = (uint16_t)end;
        return (end < len) ? ndefTailShape(data[end]) : NdefShape::TailUnseen;
    }
    return NdefShape::Companions;
}

// The shape, once the pages it could not see have been read.
enum class NdefExtent : uint8_t { WholeTag, HasCompanions, Unreadable };

// Companions turn an update into a replace; an unread tail makes it unreadable.
// A content that is not SameFormat already needs what it needs.
inline Type2Content type2ContentWithExtent(Type2Content content, NdefExtent extent) {
    if (content != Type2Content::SameFormat) return content;
    switch (extent) {
        case NdefExtent::WholeTag:      return Type2Content::SameFormat;
        case NdefExtent::HasCompanions: return Type2Content::OtherFormat;
        default:                        return Type2Content::Unreadable;
    }
}
