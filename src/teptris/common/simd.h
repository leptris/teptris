/* common/simd.h — length-guarded SIMD scan kernels (leptris lineage).
 *
 * Techniques ported from libleptris's simd_text framework:
 * - vceqq/_mm_cmpeq + horizontal-nonzero early exit (find)
 * - widen-then-add population counts (vpaddlq_u16/vaddvq_u16; the raw
 *   byte add would truncate mod 256 at density >= 2)
 * - NEON movemask emulation: narrow mask >> 7, widen, shift each lane
 *   by its own index (vector shift), horizontal add — weights sum to
 *   the exact bitmask because every bit appears exactly once
 * - chunk only while a full vector remains; scalar tail; never read
 *   past len
 *
 * ISA selection is baseline-only (NEON is architectural on aarch64,
 * SSE2 on x86-64), so no runtime dispatch and no extra compile flags.
 * Kernels return the first matching OFFSET, or len when nothing
 * matched — callers test against len, never -1. */
#ifndef TEPTRIS_COMMON_SIMD_H
#define TEPTRIS_COMMON_SIMD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>


#if defined(_MSC_VER)
#include <intrin.h>
static inline unsigned tep_ctz32(unsigned x)
{
    unsigned long i;
    _BitScanForward(&i, x);
    return (unsigned)i;
}
#else
static inline unsigned tep_ctz32(unsigned x) { return (unsigned)__builtin_ctz(x); }
#endif

#if defined(__aarch64__)
#define TEPTRIS_SIMD_NEON 1
#include <arm_neon.h>
#elif defined(__SSE2__) || defined(_M_X64) || defined(_M_AMD64)
#define TEPTRIS_SIMD_SSE2 1
#include <emmintrin.h>
#endif

/* NEON: 16-bit mask from a byte-compare vector (leptris trick). */


/* String-content stop scan: first offset where p[i] is q or e (either
 * may be 0 to disable) or a control byte (c < 0x20, tab allowed) or
 * 0x7F. len when absent. This is the combined terminator/validation
 * scan for basic and literal strings and for comment bodies — one
 * vector pass replaces per-byte quote/backslash/control checks. */
static inline size_t tep_simd_str_stop(const char *p, size_t len, char q,
                                       char e)
{
    size_t i = 0;
#if defined(TEPTRIS_SIMD_NEON)
    const uint8x16_t kq = vdupq_n_u8((uint8_t)q);
    const uint8x16_t ke = vdupq_n_u8((uint8_t)e);
    const uint8x16_t tab = vdupq_n_u8('\t');
    const uint8x16_t lo = vdupq_n_u8(0x20);
    const uint8x16_t del = vdupq_n_u8(0x7F);
    for (; len - i >= 16; i += 16) {
        uint8x16_t v = vld1q_u8((const uint8_t *)(p + i));
        uint8x16_t ctrl = vandq_u8(vcltq_u8(v, lo), vmvnq_u8(vceqq_u8(v, tab)));
        uint8x16_t m = vorrq_u8(ctrl, vceqq_u8(v, del));
        if (q != 0) {
            m = vorrq_u8(m, vceqq_u8(v, kq));
        }
        if (e != 0) {
            m = vorrq_u8(m, vceqq_u8(v, ke));
        }
        if (vmaxvq_u8(m) != 0) {
            uint8_t bytes[16];
            vst1q_u8(bytes, m);
            for (int k = 0; k < 16; k++) {
                if (bytes[k]) {
                    return i + (size_t)k;
                }
            }
        }
    }
#elif defined(TEPTRIS_SIMD_SSE2)
    const __m128i zero = _mm_setzero_si128();
    const __m128i kq = _mm_set1_epi8(q);
    const __m128i ke = _mm_set1_epi8(e);
    const __m128i sp = _mm_set1_epi8(' ');
    const __m128i tab = _mm_set1_epi8('\t');
    const __m128i k20 = _mm_set1_epi8(0x20);
    const __m128i del = _mm_set1_epi8((char)0x7F);
    for (; len - i >= 16; i += 16) {
        __m128i v = _mm_loadu_si128((const __m128i *)(p + i));
        __m128i below = _mm_cmpeq_epi8(_mm_subs_epu8(v, k20), zero); /* c<=0x20 */
        __m128i ctrl = _mm_andnot_si128(
            _mm_or_si128(_mm_cmpeq_epi8(v, sp), _mm_cmpeq_epi8(v, tab)), below);
        __m128i m = _mm_or_si128(ctrl, _mm_cmpeq_epi8(v, del));
        if (q != 0) {
            m = _mm_or_si128(m, _mm_cmpeq_epi8(v, kq));
        }
        if (e != 0) {
            m = _mm_or_si128(m, _mm_cmpeq_epi8(v, ke));
        }
        unsigned mask = (unsigned)_mm_movemask_epi8(m);
        if (mask != 0) {
            return i + tep_ctz32(mask);
        }
    }
#endif
    for (; i < len; i++) {
        unsigned char c = (unsigned char)p[i];
        if (c == q || c == e || (c < 0x20 && c != '\t') || c == 0x7F) {
            return i;
        }
    }
    return len;
}

#endif /* TEPTRIS_COMMON_SIMD_H */
