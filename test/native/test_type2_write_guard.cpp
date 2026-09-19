// Type 2 write guard tests — the one replace rule shared by every Type 2
// writer: blank and same-format write freely, other format and unknown data
// need an explicit confirmation, unreadable is never writable.
#include "Type2WriteGuard.h"
#include <cstdio>
#include <cstring>

static int failures = 0;
#define CHECK(cond, msg) do { if (!(cond)) { printf("  FAIL %s\n", msg); failures++; } \
                              else { printf("  PASS %s\n", msg); } } while (0)

// Builds a Type 2 user-memory image: optional leading bytes, one NDEF TLV
// holding one record, then the bytes that follow the TLV.
static uint16_t buildImage(uint8_t* out, uint16_t outSize,
                           const uint8_t* lead, uint16_t leadLen,
                           uint8_t flags, uint8_t typeLen, uint32_t payloadLen,
                           int tlvLenDelta,
                           const uint8_t* after, uint16_t afterLen) {
    memset(out, 0, outSize);
    uint16_t i = 0;
    for (uint16_t k = 0; k < leadLen; k++) out[i++] = lead[k];
    bool sr = (flags & 0x10) != 0;
    uint32_t recordLen = 2 + (sr ? 1 : 4) + typeLen + payloadLen;
    uint32_t tlvLen = recordLen + tlvLenDelta;
    out[i++] = 0x03;
    if (tlvLen > 254) { out[i++] = 0xFF; out[i++] = (uint8_t)(tlvLen >> 8); out[i++] = (uint8_t)tlvLen; }
    else out[i++] = (uint8_t)tlvLen;
    uint16_t valueStart = i;
    if (i < outSize) out[i++] = flags;
    if (i < outSize) out[i++] = typeLen;
    if (sr) { if (i < outSize) out[i++] = (uint8_t)payloadLen; }
    else {
        if (i + 4 <= outSize) {
            out[i++] = (uint8_t)(payloadLen >> 24); out[i++] = (uint8_t)(payloadLen >> 16);
            out[i++] = (uint8_t)(payloadLen >> 8);  out[i++] = (uint8_t)payloadLen;
        }
    }
    uint32_t end = valueStart + tlvLen;
    for (uint32_t k = i; k < end && k < outSize; k++) out[k] = 0x41;  // type + payload bytes
    for (uint16_t k = 0; k < afterLen; k++) if (end + k < outSize) out[end + k] = after[k];
    return (uint16_t)end;
}

int main() {
    printf("=== Type 2 write guard tests ===\n");

    // Blank and SameFormat lose nothing — allowed either way.
    CHECK(type2WriteAllowed(Type2Content::Blank, false),
          "blank without confirmation is allowed");
    CHECK(type2WriteAllowed(Type2Content::Blank, true),
          "blank with confirmation is allowed");
    CHECK(type2WriteAllowed(Type2Content::SameFormat, false),
          "same format without confirmation is allowed");
    CHECK(type2WriteAllowed(Type2Content::SameFormat, true),
          "same format with confirmation is allowed");

    // OtherFormat and UnknownData destroy what is there — confirmation only.
    CHECK(!type2WriteAllowed(Type2Content::OtherFormat, false),
          "other format without confirmation is refused");
    CHECK(type2WriteAllowed(Type2Content::OtherFormat, true),
          "other format with confirmation is allowed");
    CHECK(!type2WriteAllowed(Type2Content::UnknownData, false),
          "unknown data without confirmation is refused");
    CHECK(type2WriteAllowed(Type2Content::UnknownData, true),
          "unknown data with confirmation is allowed");

    // Unreadable is never writable, even with a confirmation.
    CHECK(!type2WriteAllowed(Type2Content::Unreadable, false),
          "unreadable without confirmation is refused");
    CHECK(!type2WriteAllowed(Type2Content::Unreadable, true),
          "unreadable with confirmation is still refused");

    // Reasons: the pages key on the "replace needed" prefix to offer the
    // confirmation; everything else reads as a failed read.
    CHECK(strncmp(type2RefusalReason(Type2Content::OtherFormat), "replace needed", 14) == 0,
          "other format reason starts with 'replace needed'");
    CHECK(strncmp(type2RefusalReason(Type2Content::UnknownData), "replace needed", 14) == 0,
          "unknown data reason starts with 'replace needed'");
    CHECK(strcmp(type2RefusalReason(Type2Content::Unreadable), "tag unreadable") == 0,
          "unreadable reason is 'tag unreadable'");
    CHECK(strcmp(type2RefusalReason(Type2Content::Blank), "tag unreadable") == 0,
          "blank default reason is 'tag unreadable'");

    // type2FormatFromMime — only an EXACT MIME match names a format. The bug
    // this pins: a record whose type merely differs in length from the
    // OpenSpool MIME was taken for OpenSpool and overwritten unasked.
    const char* OT3D = "application/opentag3d";
    const char* JSON = "application/json";
    CHECK(type2FormatFromMime("application/opentag3d", 21, OT3D, JSON) == Type2Format::OpenTag3D,
          "exact OpenTag3D MIME is OpenTag3D");
    CHECK(type2FormatFromMime("application/json", 16, OT3D, JSON) == Type2Format::JsonRecord,
          "the JSON MIME names a JSON record, not OpenSpool: any application writes JSON");
    CHECK(type2FormatFromMime("text/plain", 10, OT3D, JSON) == Type2Format::OtherNdef,
          "a MIME of a different length is another format, not OpenSpool");
    CHECK(type2FormatFromMime("application/jsox", 16, OT3D, JSON) == Type2Format::OtherNdef,
          "a MIME of the same length but other bytes is another format");
    CHECK(type2FormatFromMime("application/json-ld", 19, OT3D, JSON) == Type2Format::OtherNdef,
          "a MIME that only starts with the OpenSpool MIME is another format");
    CHECK(type2FormatFromMime("application/json", 11, OT3D, JSON) == Type2Format::OtherNdef,
          "a truncated MIME length is another format");
    CHECK(type2FormatFromMime(nullptr, 16, OT3D, JSON) == Type2Format::OtherNdef,
          "a null MIME is another format");
    CHECK(type2FormatFromMime("", 0, OT3D, JSON) == Type2Format::OtherNdef,
          "an empty MIME is another format");

    // type2ContentForFormat — same format is an update, everything else is not.
    CHECK(type2ContentForFormat(Type2Format::OpenSpool, Type2Format::OpenSpool) == Type2Content::SameFormat,
          "OpenSpool on the tag, OpenSpool writer: same format");
    CHECK(type2ContentForFormat(Type2Format::OpenTag3D, Type2Format::OpenSpool) == Type2Content::OtherFormat,
          "OpenTag3D on the tag, OpenSpool writer: other format");
    CHECK(type2ContentForFormat(Type2Format::OpenTag3D, Type2Format::TigerTag) == Type2Content::OtherFormat,
          "OpenTag3D on the tag, TigerTag writer: other format");
    CHECK(type2ContentForFormat(Type2Format::TigerTag, Type2Format::TigerTag) == Type2Content::SameFormat,
          "TigerTag on the tag, TigerTag writer: same format");
    CHECK(type2ContentForFormat(Type2Format::OtherNdef, Type2Format::OpenSpool) == Type2Content::OtherFormat,
          "an unknown record is never the writer's own format");
    CHECK(type2ContentForFormat(Type2Format::OtherNdef, Type2Format::OtherNdef) == Type2Content::OtherFormat,
          "OtherNdef never counts as a match, even against itself");
    CHECK(type2ContentForFormat(Type2Format::JsonRecord, Type2Format::OpenSpool) == Type2Content::OtherFormat,
          "a JSON record nobody parsed is not the OpenSpool writer's own format");
    CHECK(type2ContentForFormat(Type2Format::JsonRecord, Type2Format::JsonRecord) == Type2Content::OtherFormat,
          "JsonRecord never counts as a match, even against itself");

    // type2WindowComplete — a reader can return the pages it got before a read
    // failed part-way. The blank scan starts after the window, so a short
    // window leaves pages nobody looked at and proves nothing.
    CHECK(type2WindowComplete(40), "the full 10-page window is complete");
    CHECK(!type2WindowComplete(39), "one byte short is not complete");
    CHECK(!type2WindowComplete(16), "a 4-page prefix is not complete");
    CHECK(!type2WindowComplete(4), "a 1-page prefix is not complete");
    CHECK(!type2WindowComplete(0), "a failed read is not complete");

    // openSpoolProbeResult — OpenSpool is proven by the whole payload parsing
    // as OpenSpool, never by the MIME type.
    CHECK(openSpoolProbeResult(120, 120, 256, true) == OpenSpoolProbe::IsOpenSpool,
          "whole payload read and parsed as OpenSpool");
    CHECK(openSpoolProbeResult(120, 120, 256, false) == OpenSpoolProbe::NotOpenSpool,
          "whole payload read, not OpenSpool JSON");
    CHECK(openSpoolProbeResult(120, 80, 256, false) == OpenSpoolProbe::ShortRead,
          "part of the payload came back: nothing is known");
    CHECK(openSpoolProbeResult(120, 80, 256, true) == OpenSpoolProbe::ShortRead,
          "a parse of a short read is not trusted");
    CHECK(openSpoolProbeResult(120, 0, 256, false) == OpenSpoolProbe::ShortRead,
          "no payload bytes came back");
    CHECK(openSpoolProbeResult(0, 0, 256, false) == OpenSpoolProbe::NotOpenSpool,
          "an empty JSON record is not OpenSpool");
    CHECK(openSpoolProbeResult(400, 256, 256, false) == OpenSpoolProbe::NotOpenSpool,
          "a payload larger than the reader's buffer is not an OpenSpool this firmware reads");
    CHECK(openSpoolProbeResult(400, 256, 256, true) == OpenSpoolProbe::NotOpenSpool,
          "a parse of a clipped payload is not trusted");

    // type2ContentForJsonRecord — only the OpenSpool writer can own a JSON record.
    CHECK(type2ContentForJsonRecord(Type2Format::OpenSpool, OpenSpoolProbe::IsOpenSpool) == Type2Content::SameFormat,
          "OpenSpool writer, proven OpenSpool: same format");
    CHECK(type2ContentForJsonRecord(Type2Format::OpenSpool, OpenSpoolProbe::NotOpenSpool) == Type2Content::OtherFormat,
          "OpenSpool writer, another application's JSON: other format");
    CHECK(type2ContentForJsonRecord(Type2Format::OpenSpool, OpenSpoolProbe::ShortRead) == Type2Content::Unreadable,
          "OpenSpool writer, payload not fully read: unreadable");
    CHECK(type2ContentForJsonRecord(Type2Format::TigerTag, OpenSpoolProbe::IsOpenSpool) == Type2Content::OtherFormat,
          "TigerTag writer, OpenSpool on the tag: other format");
    CHECK(type2ContentForJsonRecord(Type2Format::OpenTag3D, OpenSpoolProbe::NotOpenSpool) == Type2Content::OtherFormat,
          "OpenTag3D writer, JSON on the tag: other format");


    // ndefMessageShape — a writer that re-emits ONE record erases every other
    // record, TLV and byte on the tag, so "my format" is only an update when
    // that record is provably everything there is.
    {
        uint8_t img[40]; uint16_t end = 0; uint16_t got = 0;
        const uint8_t term[] = {0xFE};
        const uint8_t zero[] = {0x00};
        const uint8_t tlv2[] = {0x03, 0x05};
        const uint8_t prop[] = {0xFD, 0x02};

        end = buildImage(img, sizeof(img), nullptr, 0, 0xD2, 4, 6, 0, term, 1);
        CHECK(ndefMessageShape(img, sizeof(img), &got) == NdefShape::SoleRecord && got == end,
              "one MB|ME record, terminator right after: sole record");

        buildImage(img, sizeof(img), nullptr, 0, 0x92, 4, 6, 12, term, 1);
        CHECK(ndefMessageShape(img, sizeof(img), &got) == NdefShape::Companions,
              "first record without ME: another record follows");

        buildImage(img, sizeof(img), nullptr, 0, 0x52, 4, 6, 0, term, 1);
        CHECK(ndefMessageShape(img, sizeof(img), &got) == NdefShape::Companions,
              "record without MB is not the first record");

        buildImage(img, sizeof(img), nullptr, 0, 0xD2, 4, 6, 9, term, 1);
        CHECK(ndefMessageShape(img, sizeof(img), &got) == NdefShape::Companions,
              "MB|ME record shorter than its TLV: bytes share the message");

        buildImage(img, sizeof(img), nullptr, 0, 0xD2, 4, 6, -1, term, 1);
        CHECK(ndefMessageShape(img, sizeof(img), &got) == NdefShape::Companions,
              "record longer than its TLV is malformed, never sole");

        buildImage(img, sizeof(img), nullptr, 0, 0xDA, 4, 6, 0, term, 1);
        CHECK(ndefMessageShape(img, sizeof(img), &got) == NdefShape::Companions,
              "IL flag set: header has an ID field this parser does not read");

        buildImage(img, sizeof(img), nullptr, 0, 0xF2, 4, 6, 0, term, 1);
        CHECK(ndefMessageShape(img, sizeof(img), &got) == NdefShape::Companions,
              "chunked record is never sole");

        buildImage(img, sizeof(img), nullptr, 0, 0xD1, 4, 6, 0, term, 1);
        CHECK(ndefMessageShape(img, sizeof(img), &got) == NdefShape::Companions,
              "a well-known-type record is not a media record");

        end = buildImage(img, sizeof(img), nullptr, 0, 0xD2, 4, 6, 0, zero, 1);
        CHECK(ndefMessageShape(img, sizeof(img), &got) == NdefShape::ZeroTail && got == end,
              "zero after the TLV: sole only if the rest of memory is zero");

        buildImage(img, sizeof(img), nullptr, 0, 0xD2, 4, 6, 0, tlv2, 2);
        CHECK(ndefMessageShape(img, sizeof(img), &got) == NdefShape::Companions,
              "a second NDEF TLV after the first");

        buildImage(img, sizeof(img), nullptr, 0, 0xD2, 4, 6, 0, prop, 2);
        CHECK(ndefMessageShape(img, sizeof(img), &got) == NdefShape::Companions,
              "a proprietary TLV after the message");

        const uint8_t lockCtl[] = {0x01, 0x03, 0xA0, 0x0C, 0x34};
        end = buildImage(img, sizeof(img), lockCtl, sizeof(lockCtl), 0xD2, 4, 6, 0, term, 1);
        CHECK(ndefMessageShape(img, sizeof(img), &got) == NdefShape::SoleRecord && got == end,
              "a lock control TLV ahead of the message is tag housekeeping");

        const uint8_t nulls[] = {0x00, 0x00, 0x00};
        end = buildImage(img, sizeof(img), nulls, sizeof(nulls), 0xD2, 4, 6, 0, term, 1);
        CHECK(ndefMessageShape(img, sizeof(img), &got) == NdefShape::SoleRecord && got == end,
              "NULL TLVs ahead of the message are padding");

        const uint8_t propLead[] = {0xFD, 0x02, 0xAA, 0xBB};
        buildImage(img, sizeof(img), propLead, sizeof(propLead), 0xD2, 4, 6, 0, term, 1);
        CHECK(ndefMessageShape(img, sizeof(img), &got) == NdefShape::Companions,
              "a proprietary TLV ahead of the message is someone's data");

        // An OpenTag3D-sized record: 224-byte payload, long TLV, long record.
        end = buildImage(img, sizeof(img), nullptr, 0, 0xD2, 21, 224, 0, term, 1);
        CHECK(ndefMessageShape(img, sizeof(img), &got) == NdefShape::TailUnseen && got == end && end == 2 + 3 + 21 + 224,
              "record runs past the window: the byte after it was not seen");

        end = buildImage(img, sizeof(img), nullptr, 0, 0xC2, 21, 300, 0, term, 1);
        CHECK(ndefMessageShape(img, sizeof(img), &got) == NdefShape::TailUnseen && got == end,
              "long-form record whose length fills the TLV");

        buildImage(img, sizeof(img), nullptr, 0, 0xC2, 21, 300, 40, term, 1);
        CHECK(ndefMessageShape(img, sizeof(img), &got) == NdefShape::Companions,
              "long-form record shorter than its TLV");

        memset(img, 0, sizeof(img)); img[0] = 0xFE;
        CHECK(ndefMessageShape(img, sizeof(img), &got) == NdefShape::Companions,
              "no NDEF TLV at all is never a sole record");
        memset(img, 0, sizeof(img));
        CHECK(ndefMessageShape(img, sizeof(img), &got) == NdefShape::Companions,
              "an empty window is never a sole record");
        CHECK(ndefMessageShape(nullptr, 40, &got) == NdefShape::Companions,
              "null data is never a sole record");
        memset(img, 0, sizeof(img)); img[38] = 0x03; img[39] = 0x0B;
        CHECK(ndefMessageShape(img, sizeof(img), &got) == NdefShape::Companions,
              "a record header cut off by the window end is never sole");
    }

    CHECK(ndefTailShape(0xFE) == NdefShape::SoleRecord, "terminator after the TLV: sole");
    CHECK(ndefTailShape(0x00) == NdefShape::ZeroTail, "zero after the TLV: needs the zero scan");
    CHECK(ndefTailShape(0x03) == NdefShape::Companions, "another TLV after the TLV: companions");
    CHECK(ndefTailShape(0xFD) == NdefShape::Companions, "proprietary TLV after the TLV: companions");

    // type2ContentWithExtent — companions turn an update into a replace.
    CHECK(type2ContentWithExtent(Type2Content::SameFormat, NdefExtent::WholeTag) == Type2Content::SameFormat,
          "own format and nothing else: update");
    CHECK(type2ContentWithExtent(Type2Content::SameFormat, NdefExtent::HasCompanions) == Type2Content::OtherFormat,
          "own format plus other data: replace needs confirmation");
    CHECK(type2ContentWithExtent(Type2Content::SameFormat, NdefExtent::Unreadable) == Type2Content::Unreadable,
          "own format, rest of tag unread: unreadable");
    CHECK(type2ContentWithExtent(Type2Content::OtherFormat, NdefExtent::WholeTag) == Type2Content::OtherFormat,
          "extent never upgrades another format");
    CHECK(type2ContentWithExtent(Type2Content::Unreadable, NdefExtent::WholeTag) == Type2Content::Unreadable,
          "extent never upgrades unreadable");

    if (failures == 0) printf("All type 2 write guard tests passed\n");
    else printf("%d test(s) FAILED\n", failures);
    return failures == 0 ? 0 : 1;
}
