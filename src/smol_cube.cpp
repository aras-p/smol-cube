#include "smol_cube.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>
#include <charconv>

#ifdef __APPLE__
// As of Xcode 15, C++17 from_chars for floats does not exist yet on macOS libraries :(
#define NO_FLOAT_FROM_CHARS_HERE
#endif


// --------------------------------------------------------------------------
// Float <-> Half conversion functions
// Based on Fabian Giesen's blog post https://fgiesen.wordpress.com/2012/03/28/half-to-float-done-quic/
// and public domain gists:
// https://gist.github.com/rygorous/2156668
// https://gist.github.com/rygorous/2144712
// https://gist.github.com/rygorous/4d9e9e88cab13c703773dc767a23575f

#if defined(__ARM_NEON)
#  define HALF_USES_NEON_CONVERSION
#  include <arm_neon.h>
#endif
#if (defined(__x86_64__) || defined(_M_X64))
#  if defined(__AVX2__)
#    define HALF_USES_X64_F16C_CONVERSION
#  else
#    define HALF_USES_X64_SSE2_CONVERSION
#  endif
#  include <immintrin.h>
#endif

static uint16_t float_to_half(float v)
{
#if defined(HALF_USES_NEON_CONVERSION)
    float16x4_t h4 = vcvt_f16_f32(vdupq_n_f32(v));
    float16_t h = vget_lane_f16(h4, 0);
    return *(uint16_t*)&h;
#else
    // float_to_half_fast3_rtne from https://gist.github.com/rygorous/2156668
    union FP32 {
        uint32_t u;
        float f;
    };
    FP32 f;
    f.f = v;
    FP32 f32infty = { 255 << 23 };
    FP32 f16max = { (127 + 16) << 23 };
    FP32 denorm_magic = { ((127 - 15) + (23 - 10) + 1) << 23 };
    uint32_t sign_mask = 0x80000000u;
    uint16_t o = { 0 };

    uint32_t sign = f.u & sign_mask;
    f.u ^= sign;

    // NOTE all the integer compares in this function can be safely
    // compiled into signed compares since all operands are below
    // 0x80000000. Important if you want fast straight SSE2 code
    // (since there's no unsigned PCMPGTD).
    if (f.u >= f16max.u)
    {
        // result is Inf or NaN (all exponent bits set)
        o = (f.u > f32infty.u) ? 0x7e00 : 0x7c00; // NaN->qNaN and Inf->Inf
    }
    else
    {
        // (De)normalized number or zero
        if (f.u < (113 << 23))
        {
            // Resulting FP16 is subnormal or zero.
            // Use a magic value to align our 10 mantissa bits at the bottom of
            // the float. as long as FP addition is round-to-nearest-even this
            // just works.
            f.f += denorm_magic.f;

            // and one integer subtract of the bias later, we have our final float!
            o = f.u - denorm_magic.u;
        }
        else
        {
            uint32_t mant_odd = (f.u >> 13) & 1; // resulting mantissa is odd

            // update exponent, rounding bias part 1
            f.u += (uint32_t(15 - 127) << 23) + 0xfff;
            // rounding bias part 2
            f.u += mant_odd;
            // take the bits!
            o = f.u >> 13;
        }
    }
    o |= sign >> 16;
    return o;
#endif
}

static float half_to_float(uint16_t v)
{
#if defined(HALF_USES_NEON_CONVERSION)
    uint16x4_t v4 = vdup_n_u16(v);
    float16x4_t h4 = vreinterpret_f16_u16(v4);
    float32x4_t f4 = vcvt_f32_f16(h4);
    return vgetq_lane_f32(f4, 0);
#else
    // half_to_float_fast4 from https://gist.github.com/rygorous/2144712
    union FP32
    {
        uint32_t u;
        float f;
    };
    constexpr FP32 magic = { 113 << 23 };
    constexpr uint32_t shifted_exp = 0x7c00 << 13; // exponent mask after shift
    FP32 o;

    o.u = (v & 0x7fff) << 13;         // exponent/mantissa bits
    uint32_t exp = shifted_exp & o.u; // just the exponent
    o.u += (127 - 15) << 23;          // exponent adjust

    // handle exponent special cases
    if (exp == shifted_exp) // Inf/NaN?
    {
        o.u += (128 - 16) << 23; // extra exp adjust
    }
    else if (exp == 0)   // Zero/Denormal?
    {
        o.u += 1 << 23;  // extra exp adjust
        o.f -= magic.f;  // renormalize
    }

    o.u |= (v & 0x8000) << 16; // sign bit
    return o.f;
#endif
}

#ifdef HALF_USES_X64_SSE2_CONVERSION
// Float->half conversion with round-to-nearest-even, SSE2+.
// leaves half-floats in 32-bit lanes (sign extended).
static inline __m128i F32_to_F16_4x(const __m128& f)
{
    const __m128 mask_sign = _mm_set1_ps(-0.0f);
    // all FP32 values >=this round to +inf
    const __m128i c_f16max = _mm_set1_epi32((127 + 16) << 23);
    const __m128i c_nanbit = _mm_set1_epi32(0x200);
    const __m128i c_nanlobits = _mm_set1_epi32(0x1ff);
    const __m128i c_infty_as_fp16 = _mm_set1_epi32(0x7c00);
    // smallest FP32 that yields a normalized FP16
    const __m128i c_min_normal = _mm_set1_epi32((127 - 14) << 23);
    const __m128i c_subnorm_magic = _mm_set1_epi32(((127 - 15) + (23 - 10) + 1) << 23);
    // adjust exponent and add mantissa rounding
    const __m128i c_normal_bias = _mm_set1_epi32(0xfff - ((127 - 15) << 23));

    __m128 justsign = _mm_and_ps(f, mask_sign);
    __m128 absf = _mm_andnot_ps(mask_sign, f); // f & ~mask_sign
    // the cast is "free" (extra bypass latency, but no throughput hit)
    __m128i absf_int = _mm_castps_si128(absf);
    __m128 b_isnan = _mm_cmpunord_ps(absf, absf);              // is this a NaN?
    __m128i b_isregular = _mm_cmpgt_epi32(c_f16max, absf_int); // (sub)normalized or special?
    __m128i nan_payload = _mm_and_si128(_mm_srli_epi32(absf_int, 13), c_nanlobits); // payload bits for NaNs
    __m128i nan_quiet = _mm_or_si128(nan_payload, c_nanbit); // and set quiet bit
    __m128i nanfinal = _mm_and_si128(_mm_castps_si128(b_isnan), nan_quiet);
    __m128i inf_or_nan = _mm_or_si128(nanfinal, c_infty_as_fp16); // output for specials

    __m128i b_issub = _mm_cmpgt_epi32(c_min_normal, absf_int); // subnormal?

    // "result is subnormal" path
    __m128 subnorm1 = _mm_add_ps(absf, _mm_castsi128_ps(c_subnorm_magic)); // magic value to round output mantissa
    __m128i subnorm2 = _mm_sub_epi32(_mm_castps_si128(subnorm1), c_subnorm_magic); // subtract out bias

    // "result is normal" path
    __m128i mantoddbit = _mm_slli_epi32(absf_int, 31 - 13); // shift bit 13 (mantissa LSB) to sign
    __m128i mantodd = _mm_srai_epi32(mantoddbit, 31);       // -1 if FP16 mantissa odd, else 0

    __m128i round1 = _mm_add_epi32(absf_int, c_normal_bias);
    // if mantissa LSB odd, bias towards rounding up (RTNE)
    __m128i round2 = _mm_sub_epi32(round1, mantodd);
    __m128i normal = _mm_srli_epi32(round2, 13); // rounded result

    // combine the two non-specials
    __m128i nonspecial = _mm_or_si128(_mm_and_si128(subnorm2, b_issub), _mm_andnot_si128(b_issub, normal));

    // merge in specials as well
    __m128i joined = _mm_or_si128(_mm_and_si128(nonspecial, b_isregular), _mm_andnot_si128(b_isregular, inf_or_nan));

    __m128i sign_shift = _mm_srai_epi32(_mm_castps_si128(justsign), 16);
    __m128i result = _mm_or_si128(joined, sign_shift);

    return result;
}

// Half->float conversion, SSE2+. Input in 32-bit lanes.
static inline __m128 F16_to_F32_4x(const __m128i& h)
{
    const __m128i mask_nosign = _mm_set1_epi32(0x7fff);
    const __m128 magic_mult = _mm_castsi128_ps(_mm_set1_epi32((254 - 15) << 23));
    const __m128i was_infnan = _mm_set1_epi32(0x7bff);
    const __m128 exp_infnan = _mm_castsi128_ps(_mm_set1_epi32(255 << 23));
    const __m128i was_nan = _mm_set1_epi32(0x7c00);
    const __m128i nan_quiet = _mm_set1_epi32(1 << 22);

    __m128i expmant = _mm_and_si128(mask_nosign, h);
    __m128i justsign = _mm_xor_si128(h, expmant);
    __m128i shifted = _mm_slli_epi32(expmant, 13);
    __m128 scaled = _mm_mul_ps(_mm_castsi128_ps(shifted), magic_mult);
    __m128i b_wasinfnan = _mm_cmpgt_epi32(expmant, was_infnan);
    __m128i sign = _mm_slli_epi32(justsign, 16);
    __m128 infnanexp = _mm_and_ps(_mm_castsi128_ps(b_wasinfnan), exp_infnan);
    __m128i b_wasnan = _mm_cmpgt_epi32(expmant, was_nan);
    __m128i nanquiet = _mm_and_si128(b_wasnan, nan_quiet);
    __m128 infnandone = _mm_or_ps(infnanexp, _mm_castsi128_ps(nanquiet));

    __m128 sign_inf = _mm_or_ps(_mm_castsi128_ps(sign), infnandone);
    __m128 result = _mm_or_ps(scaled, sign_inf);

    return result;
}
#endif  // HALF_USES_X64_SSE2_CONVERSION

static void float_to_half(const float* src, uint16_t* dst, size_t length)
{
    size_t i = 0;
#if defined(HALF_USES_X64_F16C_CONVERSION)
    for (; i + 7 < length; i += 8)
    {
        __m256 src8 = _mm256_loadu_ps(src);
        __m128i h8 = _mm256_cvtps_ph(src8, _MM_FROUND_TO_NEAREST_INT);
        _mm_storeu_epi32(dst, h8);
        src += 8;
        dst += 8;
    }
#elif defined(HALF_USES_X64_SSE2_CONVERSION)
    for (; i + 3 < length; i += 4)
    {
        __m128 src4 = _mm_loadu_ps(src);
        __m128i h4 = F32_to_F16_4x(src4);
        __m128i h4_packed = _mm_packs_epi32(h4, h4);
        _mm_storeu_si64(dst, h4_packed);
        src += 4;
        dst += 4;
    }
#elif defined(HALF_USES_NEON_CONVERSION)
    for (; i + 3 < length; i += 4)
    {
        float32x4_t src4 = vld1q_f32(src);
        float16x4_t h4 = vcvt_f16_f32(src4);
        vst1_f16((float16_t*)dst, h4);
        src += 4;
        dst += 4;
    }
#endif
    for (; i < length; i++) // scalar tail
    {
        *dst++ = float_to_half(*src++);
    }
}

static void half_to_float(const uint16_t* src, float* dst, size_t length)
{
    size_t i = 0;
#if defined(HALF_USES_X64_F16C_CONVERSION)
    for (; i + 7 < length; i += 8)
    {
        __m128i src8 = _mm_loadu_epi32(src);
        __m256 f8 = _mm256_cvtph_ps(src8);
        _mm256_storeu_ps(dst, f8);
        src += 8;
        dst += 8;
    }
#elif defined(HALF_USES_X64_SSE2_CONVERSION)
    for (; i + 3 < length; i += 4)
    {
        __m128i src4 = _mm_loadu_si64(src);
        src4 = _mm_unpacklo_epi16(src4, src4);
        __m128 f4 = F16_to_F32_4x(src4);
        _mm_storeu_ps(dst, f4);
        src += 4;
        dst += 4;
    }
#elif defined(HALF_USES_NEON_CONVERSION)
    for (; i + 3 < length; i += 4)
    {
        float16x4_t src4 = vld1_f16((const float16_t*)src);
        float32x4_t f4 = vcvt_f32_f16(src4);
        vst1q_f32(dst, f4);
        src += 4;
        dst += 4;
    }
#endif
    for (; i < length; i++)  // scalar tail
    {
        *dst++ = half_to_float(*src++);
    }
}

// --------------------------------------------------------------------------
// Tiny SIMD utility

#if defined(__x86_64__) || defined(_M_X64)
#	define CPU_ARCH_X64 1
#	include <emmintrin.h> // sse2
#	include <tmmintrin.h> // sse3
#	include <smmintrin.h> // sse4.1
#elif defined(__aarch64__) || defined(_M_ARM64)
#	define CPU_ARCH_ARM64 1
#	include <arm_neon.h>
#else
#   error Unsupported platform (SSE/NEON required)
#endif

#if CPU_ARCH_X64
typedef __m128i Bytes16;
inline Bytes16 SimdZero() { return _mm_setzero_si128(); }
inline Bytes16 SimdSet1(uint8_t v) { return _mm_set1_epi8(v); }
inline Bytes16 SimdLoad(const void* ptr) { return _mm_loadu_si128((const __m128i*)ptr); }
inline void SimdStore(void* ptr, Bytes16 x) { _mm_storeu_si128((__m128i*)ptr, x); }

template<int lane> inline uint8_t SimdGetLane(Bytes16 x) { return _mm_extract_epi8(x, lane); }
template<int lane> inline Bytes16 SimdSetLane(Bytes16 x, uint8_t v) { return _mm_insert_epi8(x, v, lane); }
template<int index> inline Bytes16 SimdConcat(Bytes16 hi, Bytes16 lo) { return _mm_alignr_epi8(hi, lo, index); }

inline Bytes16 SimdAdd(Bytes16 a, Bytes16 b) { return _mm_add_epi8(a, b); }
inline Bytes16 SimdSub(Bytes16 a, Bytes16 b) { return _mm_sub_epi8(a, b); }

inline Bytes16 SimdShuffle(Bytes16 x, Bytes16 table) { return _mm_shuffle_epi8(x, table); }

inline Bytes16 SimdPrefixSum(Bytes16 x)
{
    // Sklansky-style sum from https://gist.github.com/rygorous/4212be0cd009584e4184e641ca210528
    x = _mm_add_epi8(x, _mm_slli_epi64(x, 8));
    x = _mm_add_epi8(x, _mm_slli_epi64(x, 16));
    x = _mm_add_epi8(x, _mm_slli_epi64(x, 32));
    x = _mm_add_epi8(x, _mm_shuffle_epi8(x, _mm_setr_epi8(-1, -1, -1, -1, -1, -1, -1, -1, 7, 7, 7, 7, 7, 7, 7, 7)));
    return x;
}

#elif CPU_ARCH_ARM64
typedef uint8x16_t Bytes16;
inline Bytes16 SimdZero() { return vdupq_n_u8(0); }
inline Bytes16 SimdSet1(uint8_t v) { return vdupq_n_u8(v); }
inline Bytes16 SimdLoad(const void* ptr) { return vld1q_u8((const uint8_t*)ptr); }
inline void SimdStore(void* ptr, Bytes16 x) { vst1q_u8((uint8_t*)ptr, x); }

template<int lane> inline uint8_t SimdGetLane(Bytes16 x) { return vgetq_lane_u8(x, lane); }
template<int lane> inline Bytes16 SimdSetLane(Bytes16 x, uint8_t v) { return vsetq_lane_u8(v, x, lane); }
template<int index> inline Bytes16 SimdConcat(Bytes16 hi, Bytes16 lo) { return vextq_u8(lo, hi, index); }

inline Bytes16 SimdAdd(Bytes16 a, Bytes16 b) { return vaddq_u8(a, b); }
inline Bytes16 SimdSub(Bytes16 a, Bytes16 b) { return vsubq_u8(a, b); }

inline Bytes16 SimdShuffle(Bytes16 x, Bytes16 table) { return vqtbl1q_u8(x, table); }

inline Bytes16 SimdPrefixSum(Bytes16 x)
{
    // Kogge-Stone-style like commented out part of https://gist.github.com/rygorous/4212be0cd009584e4184e641ca210528
    Bytes16 zero = vdupq_n_u8(0);
    x = vaddq_u8(x, vextq_u8(zero, x, 16 - 1));
    x = vaddq_u8(x, vextq_u8(zero, x, 16 - 2));
    x = vaddq_u8(x, vextq_u8(zero, x, 16 - 4));
    x = vaddq_u8(x, vextq_u8(zero, x, 16 - 8));
    return x;
}

#endif

// --------------------------------------------------------------------------
// "Bytedelta" filter, see
// https://aras-p.info/blog/2023/03/01/Float-Compression-7-More-Filtering-Optimization/
// https://www.blosc.org/posts/bytedelta-enhance-compression-toolset/

static void FilterByteDelta(const uint8_t* src, uint8_t* dst, int channels, size_t dataElems)
{
    for (int ich = 0; ich < channels; ++ich)
    {
        uint8_t prev = 0;
        const uint8_t* srcPtr = src + ich;
        size_t ip = 0;

        // SIMD loop, 16 bytes at a time
        Bytes16 prev16 = SimdSet1(prev);
        for (; ip < dataElems / 16; ++ip)
        {
            // gather 16 bytes from source data
            Bytes16 v = SimdZero();
            v = SimdSetLane<0>(v, *srcPtr); srcPtr += channels;
            v = SimdSetLane<1>(v, *srcPtr); srcPtr += channels;
            v = SimdSetLane<2>(v, *srcPtr); srcPtr += channels;
            v = SimdSetLane<3>(v, *srcPtr); srcPtr += channels;
            v = SimdSetLane<4>(v, *srcPtr); srcPtr += channels;
            v = SimdSetLane<5>(v, *srcPtr); srcPtr += channels;
            v = SimdSetLane<6>(v, *srcPtr); srcPtr += channels;
            v = SimdSetLane<7>(v, *srcPtr); srcPtr += channels;
            v = SimdSetLane<8>(v, *srcPtr); srcPtr += channels;
            v = SimdSetLane<9>(v, *srcPtr); srcPtr += channels;
            v = SimdSetLane<10>(v, *srcPtr); srcPtr += channels;
            v = SimdSetLane<11>(v, *srcPtr); srcPtr += channels;
            v = SimdSetLane<12>(v, *srcPtr); srcPtr += channels;
            v = SimdSetLane<13>(v, *srcPtr); srcPtr += channels;
            v = SimdSetLane<14>(v, *srcPtr); srcPtr += channels;
            v = SimdSetLane<15>(v, *srcPtr); srcPtr += channels;
            // delta from previous
            Bytes16 delta = SimdSub(v, SimdConcat<15>(v, prev16));
            SimdStore(dst, delta);
            prev16 = v;
            dst += 16;
        }
        prev = SimdGetLane<15>(prev16);

        // any trailing leftover
        for (ip = ip * 16; ip < dataElems; ++ip)
        {
            uint8_t v = *srcPtr;
            *dst = v - prev;
            prev = v;

            srcPtr += channels;
            dst += 1;
        }
    }
}

static void UnFilterByteDelta(const uint8_t* src, uint8_t* dst, int channels, size_t dataElems)
{
    // "d" case: combined delta+unsplit; SIMD prefix sum delta, unrolled scattered writes into destination
    for (int ich = 0; ich < channels; ++ich)
    {
        uint8_t prev = 0;
        uint8_t* dstPtr = dst + ich;
        size_t ip = 0;

        // SIMD loop, 16 bytes at a time
        Bytes16 prev16 = SimdSet1(prev);
        Bytes16 hibyte = SimdSet1(15);
        for (; ip < dataElems / 16; ++ip)
        {
            // load 16 bytes of filtered data
            Bytes16 v = SimdLoad(src);
            // un-delta via prefix sum
            prev16 = SimdAdd(SimdPrefixSum(v), SimdShuffle(prev16, hibyte));
            // scattered write into destination
            *dstPtr = SimdGetLane<0>(prev16); dstPtr += channels;
            *dstPtr = SimdGetLane<1>(prev16); dstPtr += channels;
            *dstPtr = SimdGetLane<2>(prev16); dstPtr += channels;
            *dstPtr = SimdGetLane<3>(prev16); dstPtr += channels;
            *dstPtr = SimdGetLane<4>(prev16); dstPtr += channels;
            *dstPtr = SimdGetLane<5>(prev16); dstPtr += channels;
            *dstPtr = SimdGetLane<6>(prev16); dstPtr += channels;
            *dstPtr = SimdGetLane<7>(prev16); dstPtr += channels;
            *dstPtr = SimdGetLane<8>(prev16); dstPtr += channels;
            *dstPtr = SimdGetLane<9>(prev16); dstPtr += channels;
            *dstPtr = SimdGetLane<10>(prev16); dstPtr += channels;
            *dstPtr = SimdGetLane<11>(prev16); dstPtr += channels;
            *dstPtr = SimdGetLane<12>(prev16); dstPtr += channels;
            *dstPtr = SimdGetLane<13>(prev16); dstPtr += channels;
            *dstPtr = SimdGetLane<14>(prev16); dstPtr += channels;
            *dstPtr = SimdGetLane<15>(prev16); dstPtr += channels;
            src += 16;
        }
        prev = SimdGetLane<15>(prev16);

        // any trailing leftover
        for (ip = ip * 16; ip < dataElems; ++ip)
        {
            uint8_t v = *src + prev;
            prev = v;
            *dstPtr = v;
            src += 1;
            dstPtr += channels;
        }
    }
}

// --------------------------------------------------------------------------
// File parsing

// chunks:
// - u32: FOURCC
// - u64: data_size
// - u8[data_size]: data
//
// header: SML1
// - no data
//
// meta title: Titl
// - data is the title
// meta comment: Comm
// - data is the comment
// meta domain: Domn
// - u32: channels (e.g. 3 for RGB)
// - f32[channels]: min range
// - f32[channels]: max range
// LUT/image: ALut
// - u32: channels (e.g. 3 for RGB)
// - u32: dimension (1=1D, 2=2D, 3=3D)
// - u32: data type (0=float)
// - u32: filter (0=none, 1=bytedelta)
// - u32x3: dimensions x, y, z
// - data

struct smcube_lut
{
    int channels = 3; // 3=RGB, 4=RGBA
    int dimension = 3; // 1=1D, 2=2D, 3=3D
    smcube_data_type data_type = smcube_data_type::Float32;
    int size_x = 1;
    int size_y = 1;
    int size_z = 1;
    void* data = nullptr;
};

size_t smcube_data_type_get_size(smcube_data_type type)
{
    switch (type) {
    case smcube_data_type::Float32: return 4;
    case smcube_data_type::Float16: return 2;
    default: assert(false); return 0;
    }
}

struct smcube_file_alut_header
{
    uint32_t channels;
    uint32_t dimension;
    uint32_t data_type;
    uint32_t filter;
    uint32_t size_x;
    uint32_t size_y;
    uint32_t size_z;
};
static_assert(sizeof(smcube_file_alut_header) == 28, "Unexpected smcube_file_alut_header size");

enum class smcube_data_filter : uint32_t
{
    None = 0,
    ByteDelta,
    FilterCount
};

struct smcube_luts
{
    uint8_t* file_data = nullptr;
    size_t file_data_size = 0;
    std::string title;
    std::string comment;
    std::vector<smcube_lut> luts;
};

bool smcube_save_to_file_smcube(const char* path, const smcube_luts* luts, smcube_save_flags flags)
{
    if (path == nullptr || luts == nullptr)
        return false;

    FILE* f = fopen(path, "wb");
    if (f == nullptr)
        return false;

    fwrite("SML1", 1, 4, f);
    if (!luts->title.empty())
    {
        uint64_t len = luts->title.size();
        fwrite("Titl", 1, 4, f);
        fwrite(&len, sizeof(len), 1, f);
        fwrite(luts->title.data(), 1, len, f);
    }
    if (!luts->comment.empty())
    {
        uint64_t len = luts->comment.size();
        fwrite("Comm", 1, 4, f);
        fwrite(&len, sizeof(len), 1, f);
        fwrite(luts->comment.data(), 1, len, f);
    }
    const bool use_filter = flags & smcube_save_flag_FilterData;
    const bool use_float16 = flags & smcube_save_flag_ConvertToFloat16;
    const bool use_rgba = flags & smcube_save_flag_ExpandTo4Channels;

    for (const smcube_lut& lut : luts->luts)
    {
        fwrite("ALut", 1, 4, f);
        uint64_t data_item_len = lut.channels * smcube_data_type_get_size(lut.data_type);
        const uint64_t data_items = lut.size_x * lut.size_y * lut.size_z;
        smcube_file_alut_header head;
        head.channels = lut.channels;
        head.dimension = lut.dimension;
        head.data_type = uint32_t(lut.data_type);
        head.filter = use_filter ? uint32_t(smcube_data_filter::ByteDelta) : uint32_t(smcube_data_filter::None);
        head.size_x = lut.size_x;
        head.size_y = lut.size_y;
        head.size_z = lut.size_z;

        const uint8_t* data = (const uint8_t*)lut.data;

        uint8_t* data_fp16 = nullptr;
        uint8_t* data_rgba = nullptr;
        if (use_float16 && lut.data_type == smcube_data_type::Float32)
        {
            head.data_type = uint32_t(smcube_data_type::Float16);
            data_item_len = head.channels * smcube_data_type_get_size((smcube_data_type)head.data_type);
            data_fp16 = new uint8_t[data_item_len * data_items];
            float_to_half((const float*)data, (uint16_t*)data_fp16, data_items * head.channels);
            data = data_fp16;
        }
        if (use_rgba && head.channels == 3)
        {
            head.channels = 4;
            size_t prev_data_item_len = data_item_len;
            data_item_len = head.channels * smcube_data_type_get_size((smcube_data_type)head.data_type);
            data_rgba = new uint8_t[data_item_len * data_items];
            const uint8_t* src = data;
            uint8_t* dst = data_rgba;
            for (int i = 0; i < data_items; ++i)
            {
                memcpy(dst, src, prev_data_item_len);
                memset(dst + prev_data_item_len, 0, data_item_len - prev_data_item_len);
                src += prev_data_item_len;
                dst += data_item_len;
            }
            data = data_rgba;
        }

        const uint64_t data_size = data_item_len * data_items;
        const uint64_t chunk_len = sizeof(smcube_file_alut_header) + data_size;
        fwrite(&chunk_len, sizeof(chunk_len), 1, f);
        fwrite(&head, sizeof(head), 1, f);

        if (use_filter) {
            uint8_t* filtered_data = new uint8_t[data_size];
            FilterByteDelta(data, filtered_data, int(data_item_len), data_items);
            fwrite(filtered_data, 1, data_size, f);
            delete[] filtered_data;
        }
        else {
            fwrite(data, 1, data_size, f);
        }
        delete[] data_fp16;
        delete[] data_rgba;
    }

    fclose(f);
    return true;
}

const char* smcube_get_title(const smcube_luts* handle)
{
    if (handle == nullptr)
        return "";
    return handle->title.c_str();
}

const char* smcube_get_comment(const smcube_luts* handle)
{
    if (handle == nullptr)
        return "";
    return handle->comment.c_str();
}

size_t smcube_get_count(const smcube_luts* handle)
{
    if (handle == nullptr)
        return 0;
    return handle->luts.size();
}

smcube_lut smcube_get_lut(const smcube_luts* handle, size_t index)
{
    if (handle == nullptr || index >= handle->luts.size())
        return smcube_lut();
    return handle->luts[index];
}

int smcube_lut_get_channels(const smcube_luts* handle, size_t index)
{
    if (handle == nullptr || index >= handle->luts.size())
        return 0;
    return handle->luts[index].channels;
}
int smcube_lut_get_dimension(const smcube_luts* handle, size_t index)
{
    if (handle == nullptr || index >= handle->luts.size())
        return 0;
    return handle->luts[index].dimension;
}
smcube_data_type smcube_lut_get_data_type(const smcube_luts* handle, size_t index)
{
    if (handle == nullptr || index >= handle->luts.size())
        return smcube_data_type::Float32;
    return handle->luts[index].data_type;
}
int smcube_lut_get_size_x(const smcube_luts* handle, size_t index)
{
    if (handle == nullptr || index >= handle->luts.size())
        return 0;
    return handle->luts[index].size_x;
}
int smcube_lut_get_size_y(const smcube_luts* handle, size_t index)
{
    if (handle == nullptr || index >= handle->luts.size())
        return 0;
    return handle->luts[index].size_y;
}
int smcube_lut_get_size_z(const smcube_luts* handle, size_t index)
{
    if (handle == nullptr || index >= handle->luts.size())
        return 0;
    return handle->luts[index].size_z;
}
const void* smcube_lut_get_data(const smcube_luts* handle, size_t index)
{
    if (handle == nullptr || index >= handle->luts.size())
        return 0;
    return handle->luts[index].data;
}

static size_t lut_get_data_size(const smcube_lut& lut)
{
    size_t item_size = smcube_data_type_get_size(lut.data_type) * lut.channels;
    size_t item_count = 1;
    if (lut.dimension >= 1) item_count *= lut.size_x;
    if (lut.dimension >= 2) item_count *= lut.size_y;
    if (lut.dimension >= 3) item_count *= lut.size_z;
    return item_count * item_size;
}

size_t smcube_lut_get_data_size(const smcube_luts* handle, size_t index)
{
    if (handle == nullptr || index >= handle->luts.size())
        return 0;
    return lut_get_data_size(handle->luts[index]);
}

static bool str_ends_with(const char* str, const char* suffix)
{
    size_t str_len = strlen(str);
    size_t suffix_len = strlen(suffix);
    if (suffix_len > str_len)
        return false;
    return memcmp(str + str_len - suffix_len, suffix, suffix_len) == 0;
}

smcube_luts* smcube_load_from_file(const char* path)
{
    if (str_ends_with(path, ".cube"))
        return smcube_load_from_file_resolve_cube(path);
    if (str_ends_with(path, ".smcube"))
        return smcube_load_from_file_smcube(path);
    return nullptr;
}

smcube_luts* smcube_load_from_file_smcube(const char* path)
{
    if (path == nullptr)
        return nullptr;

    FILE* f = fopen(path, "rb");
    if (f == nullptr)
        return nullptr;

    fseek(f, 0, SEEK_END);
    int file_size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (file_size < 4)
    {
        fclose(f);
        return nullptr;
    }

    smcube_luts* luts = new smcube_luts();
    luts->file_data_size = file_size;
    luts->file_data = new uint8_t[file_size];
    fread(luts->file_data, 1, file_size, f);
    fclose(f);

    if (memcmp(luts->file_data, "SML1", 4) != 0)
    {
        smcube_free(luts);
        return nullptr;
    }
    // parse chunks
    size_t offset = 4;
    while (offset + 12 < luts->file_data_size)
    {
        // get and validate chunk length
        uint64_t chunk_len;
        memcpy(&chunk_len, luts->file_data + offset + 4, 8);
        if (offset + 12 + chunk_len > luts->file_data_size)
        {
            smcube_free(luts);
            return nullptr;
        }
        if (memcmp(luts->file_data + offset, "Titl", 4) == 0 && chunk_len > 0)
        {
            const char* str_ptr = (const char*)luts->file_data + offset + 12;
            luts->title = std::string(str_ptr, str_ptr + chunk_len);
        }
        if (memcmp(luts->file_data + offset, "Comm", 4) == 0 && chunk_len > 0)
        {
            const char* str_ptr = (const char*)luts->file_data + offset + 12;
            luts->comment = std::string(str_ptr, str_ptr + chunk_len);
        }
        if (memcmp(luts->file_data + offset, "ALut", 4) == 0 && chunk_len > sizeof(smcube_file_alut_header))
        {
            smcube_file_alut_header head;
            memcpy(&head, luts->file_data + offset + 12, sizeof(head));

            // validate lut header
            if (head.channels < 1 || head.channels > 4 ||
                head.dimension < 1 || head.dimension > 3 ||
                head.data_type >= uint32_t(smcube_data_type::DataTypeCount) ||
                head.filter >= uint32_t(smcube_data_filter::FilterCount) ||
                head.size_x > 65536 || head.size_y > 65536 || head.size_z > 65536)
            {
                smcube_free(luts);
                return nullptr;
            }

            smcube_lut lut;
            lut.channels = head.channels;
            lut.dimension = head.dimension;
            lut.data_type = smcube_data_type(head.data_type);
            lut.size_x = head.size_x;
            lut.size_y = head.size_y;
            lut.size_z = head.size_z;
            size_t lut_data_size = lut_get_data_size(lut);
            if (chunk_len - sizeof(smcube_file_alut_header) != lut_data_size)
            {
                smcube_free(luts);
                return nullptr;
            }

            // point to file data
            lut.data = luts->file_data + offset + 12 + sizeof(smcube_file_alut_header);

            // un-filter data if needed
            if (head.filter == uint32_t(smcube_data_filter::ByteDelta))
            {
                size_t lut_item_size = smcube_data_type_get_size(lut.data_type) * lut.channels;
                uint8_t* tmp = new uint8_t[lut_data_size];
                UnFilterByteDelta((const uint8_t*)lut.data, tmp, int(lut_item_size), lut_data_size / lut_item_size);
                memcpy(lut.data, tmp, lut_data_size);
                delete[] tmp;
            }

            // append to luts array
            luts->luts.push_back(lut);
        }

        offset += 12 + chunk_len;
    }

    return luts;
}

void smcube_free(smcube_luts* handle)
{
    if (handle)
        delete[] handle->file_data;
    delete handle;
}

#if !defined(NO_FLOAT_FROM_CHARS_HERE)
static inline bool is_whitespace(char c)
{
    return c <= ' ';
}

static const char* skip_whitespace(const char* p, const char* end)
{
    while (p < end && is_whitespace(*p))
        ++p;
    return p;
}

static const char* skip_plus(const char* p, const char* end)
{
    if (p < end && *p == '+')
        ++p;
    return p;
}

static const char* parse_float(const char* p, const char* end, float fallback, float& dst, bool& valid)
{
    p = skip_whitespace(p, end);
    p = skip_plus(p, end);
    std::from_chars_result res = std::from_chars(p, end, dst);
    if (res.ec == std::errc::invalid_argument || res.ec == std::errc::result_out_of_range) {
        dst = fallback;
        valid = false;
    }
    else if (res.ptr < end && !is_whitespace(*res.ptr)) {
        // If there are trailing non-space characters, it is not a valid number
        dst = fallback;
        valid = false;
        return p;
    }
    return res.ptr;
}

static bool parse_3floats(const char* p, const char* end, float* dst)
{
    valid = true;
    for (int i = 0; i < count; ++i)
        p = parse_float(p, end, 0.0f, dst[i], valid);
    return valid;
}

#else

static bool parse_3floats(const char* p, const char* end, float* dst)
{
    int count = sscanf(p, "%f %f %f", dst+0, dst+1, dst+2);
    return count == 3;
}

#endif


// Resolve .cube file format notes:
// https://resolve.cafe/developers/luts/

smcube_luts* smcube_load_from_file_resolve_cube(const char* path)
{
    if (path == nullptr)
        return nullptr;

    FILE* f = fopen(path, "rb");
    if (f == nullptr)
        return nullptr;

    char buf[1000] = {};
    std::string title;

    // read header
    int dim_3d = 0, dim_1d = 0;
    while (true) {
        char* res = fgets(buf, sizeof(buf)-1, f);
        if (!res)
            break;
        if (buf[0] >= '+' && buf[0] <= '9') // line starts with a number: header is done
            break;
        int tmp;
        if (1 == sscanf(buf, "LUT_1D_SIZE %i", &tmp))
        {
            dim_1d = tmp;
        }
        if (1 == sscanf(buf, "LUT_3D_SIZE %i", &tmp))
        {
            dim_3d = tmp;
        }
        if (strncmp(buf, "TITLE ", 6) == 0)
        {
            title = buf + 6;
            while (!title.empty() && title.back() <= ' ')
                title.pop_back(); // strip trailing newlines/spaces
            // strip quotes at start/end
            if (!title.empty() && title.front() == '"')
                title = title.substr(1);
            if (!title.empty() && title.back() == '"')
                title.pop_back();
        }

        //@TODO: comment
    }

    // validate header
    if (dim_1d < 0 || dim_1d > 65536 || dim_3d < 0 || dim_3d > 4096 || (dim_1d == 0 && dim_3d == 0))
    {
        fclose(f);
        return nullptr;
    }

    // allocate memory for the data
    size_t floats_1d = dim_1d > 0 ? dim_1d * 3 : 0;
    size_t floats_3d = dim_3d > 0 ? dim_3d * dim_3d * dim_3d * 3 : 0;

    smcube_luts* luts = new smcube_luts();
    luts->title = title;
    luts->file_data_size = (floats_1d + floats_3d) * sizeof(float);
    luts->file_data = new uint8_t[luts->file_data_size];

    smcube_lut lut1d, lut3d;
    if (dim_1d > 0)
    {
        lut1d.channels = 3;
        lut1d.dimension = 1;
        lut1d.data_type = smcube_data_type::Float32;
        lut1d.size_x = dim_1d;
        lut1d.size_y = 1;
        lut1d.size_z = 1;
        lut1d.data = luts->file_data;
        luts->luts.push_back(lut1d);
    }
    if (dim_3d > 0)
    {
        lut3d.channels = 3;
        lut3d.dimension = 3;
        lut3d.data_type = smcube_data_type::Float32;
        lut3d.size_x = dim_3d;
        lut3d.size_y = dim_3d;
        lut3d.size_z = dim_3d;
        lut3d.data = luts->file_data + floats_1d * sizeof(float);
        luts->luts.push_back(lut3d);
    }

    // read data
    size_t read_1d = 0, read_3d = 0;
    while (true) {
        // note: first data line is already in buf
        float xyz[3];
        bool xyzvalid = parse_3floats(buf, buf + strlen(buf), xyz);
        if (xyzvalid)
        {
            if (dim_1d > 0 && read_1d < dim_1d)
            {
                ((float*)lut1d.data)[read_1d * 3 + 0] = xyz[0];
                ((float*)lut1d.data)[read_1d * 3 + 1] = xyz[1];
                ((float*)lut1d.data)[read_1d * 3 + 2] = xyz[2];
                ++read_1d;
            }
            else if (dim_3d > 0 && read_3d < dim_3d * dim_3d * dim_3d)
            {
                ((float*)lut3d.data)[read_3d * 3 + 0] = xyz[0];
                ((float*)lut3d.data)[read_3d * 3 + 1] = xyz[1];
                ((float*)lut3d.data)[read_3d * 3 + 2] = xyz[2];
                ++read_3d;
            }
            else
            {
                fclose(f);
                smcube_free(luts);
                return nullptr;
            }
        }

        // read next data line
        char* res = fgets(buf, sizeof(buf), f);
        if (!res)
            break;
    }

    fclose(f);

    if (dim_1d > 0 && read_1d != dim_1d || dim_3d > 0 && read_3d != dim_3d * dim_3d * dim_3d)
    {
        smcube_free(luts);
        return nullptr;
    }
    return luts;
}

static bool is_lut_supported_by_resolve_cube(const smcube_lut& lut)
{
    if (lut.channels != 3 || lut.data_type != smcube_data_type::Float32 || (lut.dimension != 1 && lut.dimension != 3))
        return false;
    if (lut.dimension == 3 && (lut.size_x != lut.size_y || lut.size_x != lut.size_z || lut.size_y != lut.size_z))
        return false;
    return true;
}

bool smcube_save_to_file_resolve_cube(const char* path, const smcube_luts* luts)
{
    // argument checks
    if (path == nullptr || luts == nullptr || luts->luts.empty())
        return false;

    FILE* f = fopen(path, "wb");
    if (f == nullptr)
        return false;

    // write header
    fprintf(f, "# written by smol-cube\n");
    if (!luts->title.empty())
        fprintf(f, "TITLE \"%s\"\n", luts->title.c_str());
    //@TODO: comment
    for (const smcube_lut& lut : luts->luts)
    {
        if (!is_lut_supported_by_resolve_cube(lut))
            continue;
        if (lut.dimension == 1)
            fprintf(f, "LUT_1D_SIZE %i\n", lut.size_x);
        if (lut.dimension == 3)
            fprintf(f, "LUT_3D_SIZE %i\n", lut.size_x);
    }

    // write data
    for (const smcube_lut& lut : luts->luts)
    {
        if (!is_lut_supported_by_resolve_cube(lut))
            continue;
        const float* data = (const float*)lut.data;
        const float* data_end = data + lut_get_data_size(lut) / sizeof(float);
        while (data < data_end)
        {
            fprintf(f, "%.8f %.8f %.8f\n", data[0], data[1], data[2]);
            data += 3;
        }
    }

    fclose(f);
    return true;
}

void smcube_lut_convert_data(const smcube_luts* handle, size_t index, smcube_data_type dst_type, int dst_channels, void* dst_data)
{
    if (handle == nullptr || index >= handle->luts.size())
        return;
    if (dst_data == nullptr || dst_type >= smcube_data_type::DataTypeCount || dst_channels < 1 || dst_channels > 4)
        return;

    const smcube_lut& lut = handle->luts[index];
    const size_t data_items = lut.size_x * lut.size_y * lut.size_z;
    const size_t src_item_size = smcube_data_type_get_size(lut.data_type) * lut.channels;

    if (dst_type == lut.data_type && dst_channels == lut.channels) {
        // no conversion needed, just copy
        memcpy(dst_data, lut.data, data_items * src_item_size);
        return;
    }

    const int copy_channels = lut.channels < dst_channels ? lut.channels : dst_channels;
    const int skip_channels = lut.channels - copy_channels;
    const int zero_channels = dst_channels - copy_channels;

    if (lut.data_type == smcube_data_type::Float16)
    {
        // source is FP16
        const uint16_t* src = (const uint16_t*)lut.data;
        if (dst_type == smcube_data_type::Float32)
        {
            // FP16 -> FP32
            float* dst = (float*)dst_data;
            for (size_t i = 0; i < data_items; ++i)
            {
                for (int ch = 0; ch < copy_channels; ++ch)
                    *dst++ = half_to_float(*src++);
                for (int ch = 0; ch < zero_channels; ++ch)
                    *dst++ = 0;
                src += skip_channels;
            }
        }
        else if (dst_type == smcube_data_type::Float16)
        {
            // FP16 channels adjust
            uint16_t* dst = (uint16_t*)dst_data;
            for (size_t i = 0; i < data_items; ++i)
            {
                for (int ch = 0; ch < copy_channels; ++ch)
                    *dst++ = *src++;
                for (int ch = 0; ch < zero_channels; ++ch)
                    *dst++ = 0;
                src += skip_channels;
            }
        }
    }
    else if (lut.data_type == smcube_data_type::Float32)
    {
        // source is FP32
        const float* src = (const float*)lut.data;
        if (dst_type == smcube_data_type::Float16)
        {
            // FP32 -> FP16
            uint16_t* dst = (uint16_t*)dst_data;
            for (size_t i = 0; i < data_items; ++i)
            {
                for (int ch = 0; ch < copy_channels; ++ch)
                    *dst++ = float_to_half(*src++);
                for (int ch = 0; ch < zero_channels; ++ch)
                    *dst++ = 0;
                src += skip_channels;
            }
        }
        else if (dst_type == smcube_data_type::Float32)
        {
            // FP32 channels adjust
            float* dst = (float*)dst_data;
            for (size_t i = 0; i < data_items; ++i)
            {
                for (int ch = 0; ch < copy_channels; ++ch)
                    *dst++ = *src++;
                for (int ch = 0; ch < zero_channels; ++ch)
                    *dst++ = 0;
                src += skip_channels;
            }
        }
    }
}
