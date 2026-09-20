#include <cstdint>
#include <cstdio>
#include <cstring>
#include "NdefMimeBuilder.h"

static int failures = 0;
#define CHECK(c, name) do { if (c) std::printf("  PASS  %s\n", name); else { std::printf("  FAIL  %s\n", name); failures++; } } while (0)

int main() {
    uint8_t payload[500];
    uint8_t out[532];
    std::memset(payload, 0xA5, sizeof(payload));

    uint16_t n = buildNdefMimeTlv("application/opentag3d", payload, 224, out, sizeof(out));
    CHECK(n == 252, "224-byte payload builds 252-byte TLV");
    CHECK(n / 4 == 63, "224-byte payload occupies 63 pages");
    CHECK(out[0] == 0x03 && out[1] == 248, "224-byte record uses short TLV length");
    CHECK(out[2] == 0xD2 && out[4] == 224, "224-byte record uses SR payload length");
    CHECK(out[250] == 0xFE && out[251] == 0, "224-byte TLV terminator and padding");

    n = buildNdefMimeTlv("application/opentag3d", payload, 500, out, sizeof(out));
    CHECK(n == 532, "500-byte payload builds 532-byte TLV");
    CHECK(n / 4 == 133, "500-byte payload occupies 133 pages");
    CHECK(out[0] == 0x03 && out[1] == 0xFF && out[2] == 0x02 && out[3] == 0x0F,
          "500-byte record uses 527-byte extended TLV length");
    CHECK(out[4] == 0xC2 && out[6] == 0 && out[7] == 0 && out[8] == 1 && out[9] == 0xF4,
          "500-byte record uses four-byte payload length");
    CHECK(out[531] == 0xFE, "500-byte TLV terminator is final padded byte");

    CHECK(buildNdefMimeTlv("application/opentag3d", payload, 500, out, 531) == 0,
          "one-byte-short destination is rejected");
    std::printf("%s: %d failure(s)\n", failures ? "FAILED" : "OK", failures);
    return failures ? 1 : 0;
}
