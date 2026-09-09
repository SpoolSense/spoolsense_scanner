#ifndef HTCW_ENDIAN_HPP
#define HTCW_ENDIAN_HPP
#include <stddef.h>
#include <stdint.h>
#ifndef HTCW_MAX_WORD
#define HTCW_MAX_WORD 64
#endif
#if !defined(HTCW_LITTLE_ENDIAN) && !defined(HTCW_BIG_ENDIAN)
#ifdef ARDUINO_ARCH_STM32
#define HTCW_LITTLE_ENDIAN
#endif
#if defined(ARDUINO_ARDUINO_NANO33BLE) || defined(ARDUINO_ARCH_MBED_RP2040) || defined(ARDUINO_ARCH_RP2040)
#define HTCW_LITTLE_ENDIAN
#endif
#if defined(ARDUINO_ARCH_SAMD) && defined(SEEED_GROVE_UI_WIRELESS)
#define HTCW_LITTLE_ENDIAN
#endif
#if defined(ARDUINO_ARCH_AVR)
#define HTCW_LITTLE_ENDIAN
#endif
#if defined(ARDUINO_ARCH_ESP8266)
#define HTCW_LITTLE_ENDIAN
#endif
#if defined(ARDUINO_ARCH_ESP32)
#define HTCW_LITTLE_ENDIAN
#endif
#endif
#if !defined(HTCW_LITTLE_ENDIAN) && !defined(HTCW_BIG_ENDIAN)
#ifdef ESP_PLATFORM
#define HTCW_LITTLE_ENDIAN
#endif
#ifdef _WIN32_WINNT
#define HTCW_LITTLE_ENDIAN
#endif
#endif
#if !defined(HTCW_LITTLE_ENDIAN) && !defined(HTCW_BIG_ENDIAN) && defined(__arm__)
#define HTCW_LITTLE_ENDIAN
#endif
#if !defined(HTCW_LITTLE_ENDIAN) && !defined(HTCW_BIG_ENDIAN) && defined(_M_X64)
#define HTCW_LITTLE_ENDIAN
#endif
namespace bits {
enum struct endian_mode {
    none = 0,
    big_endian = 1,
    little_endian = 2
};
// true if the platform is little endian
constexpr static endian_mode endianness() {
#ifdef HTCW_BIG_ENDIAN
    return endian_mode::big_endian;
#else //if defined(HTCW_LITTLE_ENDIAN)
    return endian_mode::little_endian;
#endif
}
// swaps byte order
constexpr static inline uint16_t swap(uint16_t value) {
    return ((value & 0xFF00) >> 8) | ((value & 0x00FF) << 8);
}
// swaps byte order
constexpr static inline uint32_t swap(uint32_t value) {
#if defined(__llvm__) || (defined(__GNUC__) && !defined(__ICC))
    return __builtin_bswap32(value);
#else
    return ((value >> 24) & 0xff) |       // move byte 3 to byte 0
           ((value << 8) & 0xff0000) |    // move byte 1 to byte 2
           ((value >> 8) & 0xff00) |      // move byte 2 to byte 1
           ((value << 24) & 0xff000000);  // byte 0 to byte 3
#endif
}
#if HTCW_MAX_WORD >= 64
// swaps byte order
constexpr static inline uint64_t swap(uint64_t value) {
#if defined(__llvm__) || (defined(__GNUC__) && !defined(__ICC))
    return __builtin_bswap64(value);
#else
    uint64_t x = (uint64_t)value;
    x = (x & 0x00000000FFFFFFFF) << 32 | (x & 0xFFFFFFFF00000000) >> 32;
    x = (x & 0x0000FFFF0000FFFF) << 16 | (x & 0xFFFF0000FFFF0000) >> 16;
    x = (x & 0x00FF00FF00FF00FF) << 8 | (x & 0xFF00FF00FF00FF00) >> 8;
    return x;
#endif
#endif
}
// swaps byte order (no-op to resolve ambiguous overload)
constexpr static inline uint8_t swap(uint8_t value) {
    return value;
}

// swaps byte order
constexpr static inline int16_t swap(int16_t value) {
    return (int16_t)swap(uint16_t(value));
}
// swaps byte order
constexpr static inline int32_t swap(int32_t value) {
    return (int32_t)swap(uint32_t(value));
}
#if HTCW_MAX_WORD >= 64
// swaps byte order
constexpr static inline int64_t swap(int64_t value) {
    return (int64_t)swap(uint64_t(value));
}
#endif

// swaps byte order (no-op to resolve ambiguous overload)
constexpr static inline int8_t swap(int8_t value) {
    return value;
}

template <size_t SizeBytes>
constexpr static inline void swap_inline(void* data) {
    switch (SizeBytes) {
        case 0:
        case 1:
            return;
        case 2:
            *((uint16_t*)data) = swap(*(uint16_t*)data);
            return;
        case 4:
            *((uint32_t*)data) = swap(*(uint32_t*)data);
            return;
#if HTCW_MAX_WORD >= 64
        case 8:
            *((uint64_t*)data) = swap(*(uint64_t*)data);
            return;
#endif
    }
    for (size_t low = 0, high = SizeBytes - 1; low < high; low++, high--) {
        size_t tmp = ((uint8_t*)data)[low];
        ((uint8_t*)data)[low] = ((uint8_t*)data)[high];
        ((uint8_t*)data)[high] = tmp;
    }
}
constexpr static inline uint16_t from_le(uint16_t value) {
    return endianness() == endian_mode::big_endian ? swap(value) : value;
}
constexpr static inline uint32_t from_le(uint32_t value) {
    return endianness() == endian_mode::big_endian ? swap(value) : value;
}
#if HTCW_MAX_WORD >= 64
constexpr static inline uint64_t from_le(uint64_t value) {
    return endianness() == endian_mode::big_endian ? swap(value) : value;
}
#endif
constexpr static inline uint8_t from_le(uint8_t value) {
    return value;
}

constexpr static inline uint16_t from_be(uint16_t value) {
    return endianness() == endian_mode::little_endian ? swap(value) : value;
}
constexpr static inline uint32_t from_be(uint32_t value) {
    return endianness() == endian_mode::little_endian ? swap(value) : value;
}
#if HTCW_MAX_WORD >= 64
constexpr static inline uint64_t from_be(uint64_t value) {
    return endianness() == endian_mode::little_endian ? swap(value) : value;
}
#endif
constexpr static inline uint8_t from_be(uint8_t value) {
    return value;
}

constexpr static inline int16_t from_le(int16_t value) {
    return endianness() == endian_mode::big_endian ? swap(value) : value;
}
constexpr static inline int32_t from_le(int32_t value) {
    return endianness() == endian_mode::big_endian ? swap(value) : value;
}
#if HTCW_MAX_WORD >= 64
constexpr static inline int64_t from_le(int64_t value) {
    return endianness() == endian_mode::big_endian ? swap(value) : value;
}
#endif
constexpr static inline int8_t from_le(int8_t value) {
    return value;
}

constexpr static inline int16_t from_be(int16_t value) {
    return endianness() == endian_mode::little_endian ? swap(value) : value;
}
constexpr static inline int32_t from_be(int32_t value) {
    return endianness() == endian_mode::little_endian ? swap(value) : value;
}
#if HTCW_MAX_WORD >= 64
constexpr static inline int64_t from_be(int64_t value) {
    return endianness() == endian_mode::little_endian ? swap(value) : value;
}
#endif
constexpr static inline int8_t from_be(int8_t value) {
    return value;
}
}  // namespace bits
#endif  // HTCW_ENDIAN_HPP