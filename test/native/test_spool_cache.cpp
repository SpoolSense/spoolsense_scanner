// Spool cache record/emitter tests: CachedSpool -> Spoolman-shaped JSON with
// the exact field list fillFromSpoolman (SharedJS.h) and renderSpoolRow
// (ReaderHTML.h) read. Escaping, zero-value, and truncation behavior.
#include "../../src/SpoolCacheJson.h"
#include <cstdio>
#include <cstring>

static int failures = 0;
#define CHECK(cond, msg) do { if (!(cond)) { printf("  FAIL %s\n", msg); failures++; } \
                              else { printf("  PASS %s\n", msg); } } while (0)

static CachedSpool makeFull() {
    CachedSpool s;
    memset(&s, 0, sizeof(s));
    s.id = 7;
    s.remaining_g = 812.5f;
    s.initial_g = 1000.0f;
    s.density = 1.24f;
    s.diameter = 1.75f;
    s.extruder_temp = 215;
    s.bed_temp = 60;
    strcpy(s.vendor, "Prusament");
    strcpy(s.material, "PLA");
    strcpy(s.name, "Galaxy Black");
    strcpy(s.color_hex, "1A2B3C");
    return s;
}

int main() {
    printf("=== Spool cache JSON tests ===\n");

    // 1. Full record emits every field, Spoolman-shaped.
    CachedSpool full = makeFull();
    char out[512];
    size_t n = spoolCacheEmitJson(full, out, sizeof(out));
    const char* golden =
        "{\"id\":7,\"remaining_weight\":812.5,\"filament\":{\"name\":\"Galaxy Black\","
        "\"material\":\"PLA\",\"color_hex\":\"1A2B3C\",\"weight\":1000,\"density\":1.24,"
        "\"diameter\":1.75,\"settings_extruder_temp\":215,\"settings_bed_temp\":60,"
        "\"vendor\":{\"name\":\"Prusament\"}}}";
    CHECK(n == strlen(golden) && memcmp(out, golden, n + 1) == 0,
          "full record matches the golden Spoolman-shaped JSON");

    // 2. Vendor/name containing " and \ emit escaped; control char (\n) escapes.
    CachedSpool esc = makeFull();
    strcpy(esc.name, "A\"B\\C\nD");
    n = spoolCacheEmitJson(esc, out, sizeof(out));
    CHECK(n > 0 && strstr(out, "\"name\":\"A\\\"B\\\\C\\u000aD\"") != nullptr,
          "quote, backslash and newline escapes in name");

    // 3. Zero/empty numeric fields emit 0 values.
    CachedSpool zero;
    memset(&zero, 0, sizeof(zero));
    zero.id = 1;
    n = spoolCacheEmitJson(zero, out, sizeof(out));
    const char* zeroGolden =
        "{\"id\":1,\"remaining_weight\":0,\"filament\":{\"name\":\"\",\"material\":\"\","
        "\"color_hex\":\"\",\"weight\":0,\"density\":0,\"diameter\":0,"
        "\"settings_extruder_temp\":0,\"settings_bed_temp\":0,\"vendor\":{\"name\":\"\"}}}";
    CHECK(n == strlen(zeroGolden) && memcmp(out, zeroGolden, n + 1) == 0,
          "zero record emits 0 values and empty strings");

    // 4. Truncation: outSize smaller than needed returns 0, NUL at out[0].
    char small[16];
    memset(small, 'X', sizeof(small));
    n = spoolCacheEmitJson(full, small, sizeof(small));
    CHECK(n == 0 && small[0] == '\0', "overflow returns 0 with NUL at out[0]");

    if (failures == 0) printf("All spool cache tests passed\n");
    else printf("%d test(s) FAILED\n", failures);
    return failures == 0 ? 0 : 1;
}
