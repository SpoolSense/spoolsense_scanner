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
