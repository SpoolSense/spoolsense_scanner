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

// The Type 2 formats a writer can meet on a tag.
enum class Type2Format : uint8_t { TigerTag, OpenTag3D, OpenSpool, OtherNdef };

// Which format an NDEF media record belongs to, from its MIME type alone. Only
// an exact match names a format: a record of any other type is OtherNdef, so
// no writer can take an unknown record for its own and update it unasked.
inline Type2Format type2FormatFromMime(const char* mime, size_t mimeLen,
                                       const char* ot3dMime, const char* openSpoolMime) {
    if (mime != nullptr && ot3dMime != nullptr &&
        mimeLen == strlen(ot3dMime) && memcmp(mime, ot3dMime, mimeLen) == 0) {
        return Type2Format::OpenTag3D;
    }
    if (mime != nullptr && openSpoolMime != nullptr &&
        mimeLen == strlen(openSpoolMime) && memcmp(mime, openSpoolMime, mimeLen) == 0) {
        return Type2Format::OpenSpool;
    }
    return Type2Format::OtherNdef;
}

// A tag that already holds the writer's own format is an update; any other
// recognised or unrecognised record is another format.
inline Type2Content type2ContentForFormat(Type2Format found, Type2Format writer) {
    return (found != Type2Format::OtherNdef && found == writer) ? Type2Content::SameFormat
                                                               : Type2Content::OtherFormat;
}
