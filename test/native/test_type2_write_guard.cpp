// Type 2 write guard tests — the one replace rule shared by every Type 2
// writer: blank and same-format write freely, other format and unknown data
// need an explicit confirmation, unreadable is never writable.
#include "Type2WriteGuard.h"
#include <cstdio>
#include <cstring>

static int failures = 0;
#define CHECK(cond, msg) do { if (!(cond)) { printf("  FAIL %s\n", msg); failures++; } \
                              else { printf("  PASS %s\n", msg); } } while (0)

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
    CHECK(type2FormatFromMime("application/json", 16, OT3D, JSON) == Type2Format::OpenSpool,
          "exact OpenSpool MIME is OpenSpool");
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

    if (failures == 0) printf("All type 2 write guard tests passed\n");
    else printf("%d test(s) FAILED\n", failures);
    return failures == 0 ? 0 : 1;
}
