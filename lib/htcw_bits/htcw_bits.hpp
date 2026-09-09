#ifndef HTCW_BITS_HPP
#define HTCW_BITS_HPP
#include "htcw_endian.hpp"
#define HTCW_PACKED __attribute__((packed))
#define HTCW_PACK  // TODO: Add Microsoft pack pragmas here
#define HTCW_RESTORE_PACK
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace bits {
constexpr size_t get_word_size(size_t size) {
#if HTCW_MAX_WORD >= 64
    if (size > 64) return 0;
    if (size > 32) return 64;
#else
    if (size > 32) return 0;
#endif

    if (size > 16) return 32;
    if (size > 8) return 16;
    return 8;
}

constexpr size_t get_word_count(size_t size) {
    size_t result = 0;
    while (size > 0) {
        size_t ws = get_word_size(size);
        if (ws == 0)
            break;
        size -= ws;
        ++result;
    }
    return result;
}
constexpr size_t get_left_word(size_t size, size_t index) {
    size_t i = 0;
    while (size > 0 && i <= index) {
        size_t ws = get_word_size(size);
        if (ws == 0)
            return 0;
        if (index == i)
            return ws;
        size -= ws;
        ++i;
    }
    return 0;
}
constexpr size_t get_right_word(size_t size, size_t index) {
    size_t c = get_word_count(size);
    return get_left_word(size, c - index - 1);
    size_t i = 0;
    while (size > 0 && i <= index) {
        size_t ws = get_word_size(size);
        if (ws == 0)
            return 0;
        if (index == i)
            return ws;
        size -= ws;
        ++i;
    }
    return 0;
}
namespace helpers {
class dummy_unsigned {};
class dummy_signed {};
}  // namespace helpers
template <size_t BitWidth>
class int_helper {};
template <>
struct int_helper<8> {
    using type = int8_t;
};
template <>
struct int_helper<16> {
    using type = int16_t;
};
template <>
struct int_helper<32> {
    using type = int32_t;
};
#if HTCW_MAX_WORD >= 64
template <>
struct int_helper<64> {
    using type = int64_t;
};
template <size_t Width>
using intx = typename int_helper<Width>::type;
using int_max = typename int_helper<HTCW_MAX_WORD>::type;
#endif
template <size_t BitWidth>
class uint_helper {};
template <>
struct uint_helper<8> {
    using type = uint8_t;
};
template <>
struct uint_helper<16> {
    using type = uint16_t;
};
template <>
struct uint_helper<32> {
    using type = uint32_t;
};
#if HTCW_MAX_WORD >= 64
template <>
struct uint_helper<64> {
    using type = uint64_t;
};
#endif
template <size_t Width>
using uintx = typename uint_helper<Width>::type;
using uint_max = uint_helper<HTCW_MAX_WORD>::type;
template <size_t BitWidth>
class real_helper {};
template <>
struct real_helper<32> {
    using type = float;
};
#if HTCW_MAX_WORD >= 64
template <>
struct real_helper<64> {
    using type = double;
};
#endif
template <size_t Width>
using realx = typename real_helper<Width>::type;
using real_max = typename real_helper<HTCW_MAX_WORD>::type;
template <typename T>
struct signed_helper {
    using type = helpers::dummy_signed;
};
template <>
struct signed_helper<float> {
    using type = float;
};
template <>
struct signed_helper<double> {
    using type = double;
};
template <>
struct signed_helper<int8_t> {
    using type = int8_t;
};
template <>
struct signed_helper<uint8_t> {
    using type = int8_t;
};
template <>
struct signed_helper<int16_t> {
    using type = int16_t;
};
template <>
struct signed_helper<uint16_t> {
    using type = int16_t;
};
template <>
struct signed_helper<int32_t> {
    using type = int32_t;
};
template <>
struct signed_helper<uint32_t> {
    using type = int32_t;
};
#if HTCW_MAX_WORD >= 64
template <>
struct signed_helper<int64_t> {
    using type = int64_t;
};
template <>
struct signed_helper<uint64_t> {
    using type = int64_t;
};
#endif
template <typename T>
struct signed_helper_no_real {
    using type = helpers::dummy_signed;
};
template <>
struct signed_helper_no_real<int8_t> {
    using type = int8_t;
};
template <>
struct signed_helper_no_real<uint8_t> {
    using type = int8_t;
};
template <>
struct signed_helper_no_real<int16_t> {
    using type = int16_t;
};
template <>
struct signed_helper_no_real<uint16_t> {
    using type = int16_t;
};
template <>
struct signed_helper_no_real<int32_t> {
    using type = int32_t;
};
template <>
struct signed_helper_no_real<uint32_t> {
    using type = int32_t;
};
#if HTCW_MAX_WORD >= 64
template <>
struct signed_helper_no_real<int64_t> {
    using type = int64_t;
};
template <>
struct signed_helper_no_real<uint64_t> {
    using type = int64_t;
};
#endif

template <typename T>
using signedx = typename signed_helper<T>::type;
template <typename T>
using signedx_no_real = typename signed_helper_no_real<T>::type;

template <typename T>
struct unsigned_helper {
    using type = helpers::dummy_unsigned;
};
// template <> struct unsigned_helper<float> { using type = float; };
// template <> struct unsigned_helper<double> { using type = double; };
template <>
struct unsigned_helper<int8_t> {
    using type = uint8_t;
};
template <>
struct unsigned_helper<uint8_t> {
    using type = uint8_t;
};
template <>
struct unsigned_helper<int16_t> {
    using type = uint16_t;
};
template <>
struct unsigned_helper<uint16_t> {
    using type = uint16_t;
};
template <>
struct unsigned_helper<int32_t> {
    using type = uint32_t;
};
template <>
struct unsigned_helper<uint32_t> {
    using type = uint32_t;
};
#if HTCW_MAX_WORD >= 64
template <>
struct unsigned_helper<int64_t> {
    using type = uint64_t;
};
template <>
struct unsigned_helper<uint64_t> {
    using type = uint64_t;
};
#endif
template <typename T>
using unsignedx = typename unsigned_helper<T>::type;

template <typename T>
struct num_metrics {
    constexpr static const unsignedx<T> bit_mask = unsignedx<T>(~unsignedx<T>(0));
    constexpr static const bool is_signed = bool(T(-1) < T(0));
    constexpr static const T max = is_signed ? T(unsignedx<T>(unsignedx<T>(bit_mask << 1) >> 1)) : T(~T(0));
    constexpr static const T min = is_signed ? ~max : T(0);
    constexpr static const T zero = 0;
};
#if defined(NAN) && defined(INFINITIY)
template <>
struct num_metrics<float> {
    constexpr static const float bit_mask = NAN;
    constexpr static const bool is_signed = true;
    constexpr static const float max = INFINITY;
    constexpr static const float min = -INFINITY;
    constexpr static const float zero = 0.0;
};
template <>
struct num_metrics<double> {
    constexpr static const double bit_mask = NAN;
    constexpr static const bool is_signed = true;
    constexpr static const double max = INFINITY;
    constexpr static const double min = -INFINITY;
    constexpr static const double zero = 0.0;
};
#endif
template <size_t Width>
struct mask {
    using int_type = uintx<get_word_size(Width)>;

   private:
    constexpr static const int_type int_mask = ~int_type(0);

   public:
    constexpr static const int_type left = int_mask << ((sizeof(int_type) * 8 - Width) & int_mask);
    constexpr static const int_type right = int_mask >> (sizeof(int_type) * 8 - Width);
    constexpr static const int_type not_left = ~left;
    constexpr static const int_type not_right = ~right;
};

constexpr inline static void set_bits(void* bits, size_t offset_bits, size_t size_bits, bool value) {
    const size_t offset_bytes = offset_bits / 8;
    const size_t offset = offset_bits % 8;
    const size_t total_size_bytes = (offset_bits + size_bits + 7) / 8;
    const size_t overhang = (offset_bits + size_bits) % 8;
    uint8_t* pbegin = ((uint8_t*)bits) + offset_bytes;
    uint8_t* pend = ((uint8_t*)bits) + total_size_bytes;
    uint8_t* plast = pend - (pbegin != pend);

    const uint8_t maskL = 0 != offset ? (uint8_t)((uint8_t(0xFF >> offset))) : uint8_t(0xff);
    const uint8_t maskR = 0 != overhang ? (uint8_t) ~((uint8_t(0xFF >> overhang))) : uint8_t(0xFF);
    if (value) {
        if (pbegin == plast) {
            const uint8_t mask = maskL & maskR;
            *pbegin |= mask;
            return;
        }
        *pbegin |= maskL;
        *plast |= maskR;
        if (pbegin + 1 < plast) {
            const size_t len = (plast) - (pbegin + 1);
            if (0 != len && len <= total_size_bytes)
                memset(pbegin + 1, 0xFF, len);
        }
    } else {
        if (pbegin == plast) {
            const uint8_t mask = maskL & maskR;
            *pbegin &= ~mask;
            return;
        }
        *pbegin &= ~maskL;
        *plast &= ~maskR;

        if (pbegin + 1 < plast) {
            const size_t len = (plast) - (pbegin + 1);
            if (0 != len && len <= total_size_bytes)
                memset(pbegin + 1, 0, len);
        }
    }
}

template <size_t OffsetBits, size_t SizeBits, bool Value>
constexpr inline static void set_bits(void* bits) {
    if (0 == SizeBits)
        return;
    const size_t offset_bytes = OffsetBits / 8;
    const size_t offset = OffsetBits % 8;
    const size_t total_size_bytes = (OffsetBits + SizeBits + 7) / 8;
    const size_t overhang = (OffsetBits + SizeBits) % 8;
    uint8_t* pbegin = ((uint8_t*)bits) + offset_bytes;
    uint8_t* pend = ((uint8_t*)bits) + total_size_bytes;
    uint8_t* plast = pend - (pbegin != pend);

    const uint8_t maskL = 0 != offset ? (uint8_t)((uint8_t(0xFF >> offset))) : uint8_t(0xff);
    const uint8_t maskR = 0 != overhang ? (uint8_t) ~((uint8_t(0xFF >> overhang))) : uint8_t(0xFF);
    if (Value) {
        if (pbegin == plast) {
            const uint8_t mask = maskL & maskR;
            *pbegin |= mask;
            return;
        }
        *pbegin |= maskL;
        *plast |= maskR;
        if (pbegin + 1 < plast) {
            const size_t len = (plast) - (pbegin + 1);
            if (0 != len && len <= total_size_bytes)
                memset(pbegin + 1, 0xFF, len);
        }
    } else {
        if (pbegin == plast) {
            const uint8_t mask = maskL & maskR;
            *pbegin &= ~mask;
            return;
        }
        *pbegin &= ~maskL;
        *plast &= ~maskR;

        if (pbegin + 1 < plast) {
            const size_t len = (plast) - (pbegin + 1);
            if (0 != len && len <= total_size_bytes)
                memset(pbegin + 1, 0, len);
        }
    }
}
constexpr inline static void set_bits(size_t offset_bits, size_t size_bits, void* dst, const void* src) {
    const size_t offset_bytes = offset_bits / 8;
    const size_t offset = offset_bits % 8;
    const size_t total_size_bytes = (offset_bits + size_bits + 7) / 8;
    const size_t overhang = (offset_bits + size_bits) % 8;
    uint8_t* pbegin = ((uint8_t*)dst) + offset_bytes;
    uint8_t* psbegin = ((uint8_t*)src) + offset_bytes;
    uint8_t* pend = ((uint8_t*)dst) + total_size_bytes;
    uint8_t* plast = pend - (pbegin != pend);

    const uint8_t maskL = 0 != offset ? (uint8_t)((uint8_t(0xFF >> offset))) : uint8_t(0xff);
    const uint8_t maskR = 0 != overhang ? (uint8_t) ~((uint8_t(0xFF >> overhang))) : uint8_t(0xFF);
    if (pbegin == plast) {
        uint8_t v = *psbegin;
        const uint8_t mask = maskL & maskR;
        v &= mask;
        *pbegin &= ~mask;
        *pbegin |= v;
        return;
    }
    *pbegin &= ~maskL;
    *pbegin |= ((*psbegin) & maskL);
    *plast &= ~maskR;
    *plast |= ((*(psbegin + total_size_bytes - 1)) & maskR);
    if (pbegin + 1 < plast) {
        const size_t len = plast - (pbegin + 1);
        if (0 != len && len <= total_size_bytes)
            memcpy(pbegin + 1, psbegin + 1, len);
    }
}
template <size_t OffsetBits, size_t SizeBits>
constexpr inline static void set_bits(void* dst, const void* src) {
    const size_t offset_bytes = OffsetBits / 8;
    const size_t offset = OffsetBits % 8;
    const size_t total_size_bytes = (OffsetBits + SizeBits + 7) / 8;
    const size_t overhang = (OffsetBits + SizeBits) % 8;
    uint8_t* pbegin = ((uint8_t*)dst) + offset_bytes;
    uint8_t* psbegin = ((uint8_t*)src) + offset_bytes;
    uint8_t* pend = ((uint8_t*)dst) + total_size_bytes;
    uint8_t* plast = pend - (pbegin != pend);
    uint8_t* pslast = psbegin + total_size_bytes - 1;
    const uint8_t maskL = 0 != offset ? (uint8_t)((uint8_t(0xFF >> offset))) : uint8_t(0xff);
    const uint8_t maskR = 0 != overhang ? (uint8_t) ~((uint8_t(0xFF >> overhang))) : uint8_t(0xFF);
    if (pbegin == plast) {
        uint8_t v = *psbegin;
        const uint8_t mask = maskL & maskR;
        v &= mask;
        *pbegin = (*pbegin & uint8_t(~mask)) | v;
        return;
    }
    *pbegin = (*pbegin & ~maskL) | ((*psbegin) & maskL);

    *plast = (*plast & ~maskR) | ((*pslast) & maskR);
    if (pbegin + 1 < plast) {
        const size_t len = plast - (pbegin + 1);
        if (0 != len && len <= total_size_bytes)
            memcpy(pbegin + 1, psbegin + 1, len);
    }
}

template <size_t OffsetBits, size_t SizeBits, size_t Shift>
constexpr inline static void shift_left(void* bits) {
    if (nullptr == bits || 0 == SizeBits || 0 == Shift) {
        return;
    }
    // special case if we shift all the bits out
    if (Shift >= SizeBits) {
        set_bits<OffsetBits, SizeBits, false>(bits);
        return;
    }
    uint8_t* pbegin = ((uint8_t*)bits) + (OffsetBits / 8);
    const size_t offset = OffsetBits % 8;
    const size_t shift_bytes = Shift / 8;
    const size_t shift_bits = Shift % 8;
    const size_t overhang = (SizeBits + OffsetBits) % 8;
    // preserves left prior to offset
    const uint8_t left_mask = ((uint8_t)uint8_t(0xFF << (8 - offset)));
    // preserves right after overhang
    const uint8_t right_mask = 0 != overhang ? uint8_t(0xFF >> overhang) : 0;
    uint8_t* pend = pbegin + (size_t)((OffsetBits + SizeBits + 7) / 8);
    uint8_t* plast = pend - 1;
    uint8_t* psrc = pbegin + shift_bytes;
    uint8_t* pdst = pbegin;
    if (pbegin + 1 == pend) {
        // special case for a shift all within one byte
        uint8_t save_mask = left_mask | right_mask;
        uint8_t tmp = *pbegin;
        *pbegin = uint8_t(uint8_t(tmp << shift_bits) & ~save_mask) |
                  uint8_t(tmp & save_mask);
        return;
    }
    // preserve the ends so we can
    // fix them up later
    uint8_t left = *pbegin;
    uint8_t right = *(pend - 1);

    while (pdst != pend) {
        uint8_t src = psrc < pend ? *psrc : 0;
        uint8_t src2 = (psrc + 1) < pend ? *(psrc + 1) : 0;
        *pdst = (src << shift_bits) | (src2 >> (8 - shift_bits));
        ++psrc;
        ++pdst;
    }

    *pbegin = (left & left_mask) | uint8_t(*pbegin & ~left_mask);
    --pend;
    *plast = uint8_t(right & right_mask) | uint8_t(*plast & uint8_t(~right_mask));
};

constexpr static void shift_left(void* bits, size_t offset_bits, size_t size_bits, size_t shift) {
    if (nullptr == bits || 0 == size_bits || 0 == shift) {
        return;
    }
    // special case if we shift all the bits out
    if (shift >= size_bits) {
        set_bits(bits, offset_bits, size_bits, false);
        return;
    }
    uint8_t* pbegin = ((uint8_t*)bits) + (offset_bits / 8);
    const size_t offset = offset_bits % 8;
    const size_t shift_bytes = shift / 8;
    const size_t shift_bits = shift % 8;
    const size_t overhang = (size_bits + offset_bits) % 8;
    // preserves left prior to offset
    const uint8_t left_mask = ((uint8_t)uint8_t(0xFF << (8 - offset)));
    // preserves right after overhang
    const uint8_t right_mask = 0 != overhang ? uint8_t(0xFF >> overhang) : 0;
    uint8_t* pend = pbegin + (size_t)((offset_bits + size_bits + 7) / 8);
    uint8_t* plast = pend - 1;
    uint8_t* psrc = pbegin + shift_bytes;
    uint8_t* pdst = pbegin;
    if (pbegin + 1 == pend) {
        // special case for a shift all within one byte
        uint8_t save_mask = left_mask | right_mask;
        uint8_t tmp = *pbegin;
        *pbegin = uint8_t(uint8_t(tmp << shift_bits) & ~save_mask) |
                  uint8_t(tmp & save_mask);
        return;
    }
    // preserve the ends so we can
    // fix them up later
    uint8_t left = *pbegin;
    uint8_t right = *(pend - 1);

    while (pdst != pend) {
        uint8_t src = psrc < pend ? *psrc : 0;
        uint8_t src2 = (psrc + 1) < pend ? *(psrc + 1) : 0;
        *pdst = (src << shift_bits) | (src2 >> (8 - shift_bits));
        ++psrc;
        ++pdst;
    }

    *pbegin = (left & left_mask) | uint8_t(*pbegin & ~left_mask);
    --pend;
    *plast = uint8_t(right & right_mask) | uint8_t(*plast & uint8_t(~right_mask));
};

template <size_t OffsetBits, size_t SizeBits, size_t Shift>
constexpr inline static void shift_right(void* bits) {
    if (nullptr == bits || 0 == SizeBits || 0 == Shift) {
        return;
    }
    // special case if we shift all the bits out
    if (Shift >= SizeBits) {
        set_bits<OffsetBits, SizeBits, false>(bits);
        return;
    }
    uint8_t* pbegin = ((uint8_t*)bits) + (OffsetBits / 8);
    const size_t offset = OffsetBits % 8;
    const size_t shift_bytes = Shift / 8;
    const size_t shift_bits = Shift % 8;
    const size_t overhang = (SizeBits + OffsetBits) % 8;
    // preserves left prior to offset
    const uint8_t left_mask = ((uint8_t)uint8_t(0xFF << (8 - offset)));
    // preserves right after overhang
    const uint8_t right_mask = 0 != overhang ? uint8_t(0xFF >> overhang) : 0;
    uint8_t* pend = pbegin + (size_t)((OffsetBits + SizeBits + 7) / 8) - (OffsetBits / 8);
    uint8_t* plast = pend - 1;
    uint8_t* psrc = (pend - 1) - shift_bytes;
    uint8_t* pdst = pend - 1;
    if (pbegin + 1 == pend) {
        // special case for a shift all within one byte
        uint8_t save_mask = left_mask | right_mask;
        uint8_t tmp = *pbegin;
        *pbegin = uint8_t(uint8_t(tmp >> shift_bits) & ~save_mask) |
                  uint8_t(tmp & save_mask);
        return;
    }
    // preserve the ends so we can
    // fix them up later
    uint8_t left = *pbegin;
    uint8_t right = *psrc;

    while (pdst >= pbegin) {
        uint8_t src = psrc >= pbegin ? *psrc : 0;
        uint8_t src2 = (psrc - 1) >= pbegin ? *(psrc - 1) : 0;
        *pdst = (src >> shift_bits) | (src2 << (8 - shift_bits));
        --psrc;
        --pdst;
    }

    *pbegin = uint8_t(left & left_mask) | uint8_t(*pbegin & ~left_mask);
    *plast = uint8_t(right & right_mask) | uint8_t(*plast & uint8_t(~right_mask));
};
constexpr static void shift_right(void* bits, size_t offset_bits, size_t size_bits, size_t shift) {
    if (nullptr == bits || 0 == size_bits || 0 == shift) {
        return;
    }
    // special case if we shift all the bits out
    if (shift >= size_bits) {
        set_bits(bits, offset_bits, size_bits, false);
        return;
    }
    uint8_t* pbegin = ((uint8_t*)bits) + (offset_bits / 8);
    const size_t offset = offset_bits % 8;
    const size_t shift_bytes = shift / 8;
    const size_t shift_bits = shift % 8;
    const size_t overhang = (size_bits + offset_bits) % 8;
    // preserves left prior to offset
    const uint8_t left_mask = ((uint8_t)uint8_t(0xFF << (8 - offset)));
    // preserves right after overhang
    const uint8_t right_mask = 0 != overhang ? uint8_t(0xFF >> overhang) : 0;
    uint8_t* pend = pbegin + (size_t)((offset_bits + size_bits + 7) / 8) - (offset_bits / 8);
    uint8_t* plast = pend - 1;
    uint8_t* psrc = (pend - 1) - shift_bytes;
    uint8_t* pdst = pend - 1;
    if (pbegin + 1 == pend) {
        // special case for a shift all within one byte
        uint8_t save_mask = left_mask | right_mask;
        uint8_t tmp = *pbegin;
        *pbegin = uint8_t(uint8_t(tmp >> shift_bits) & ~save_mask) |
                  uint8_t(tmp & save_mask);
        return;
    }
    // preserve the ends so we can
    // fix them up later
    uint8_t left = *pbegin;
    uint8_t right = *psrc;

    while (pdst >= pbegin) {
        uint8_t src = psrc >= pbegin ? *psrc : 0;
        uint8_t src2 = (psrc - 1) >= pbegin ? *(psrc - 1) : 0;
        *pdst = (src >> shift_bits) | (src2 << (8 - shift_bits));
        --psrc;
        --pdst;
    }

    *pbegin = uint8_t(left & left_mask) | uint8_t(*pbegin & ~left_mask);
    *plast = uint8_t(right & right_mask) | uint8_t(*plast & uint8_t(~right_mask));
};
}  // namespace bits
#endif