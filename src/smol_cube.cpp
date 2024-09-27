#include "smol_cube.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>

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
inline Bytes16 SimdLoadA(const void* ptr) { return _mm_load_si128((const __m128i*)ptr); }
inline void SimdStore(void* ptr, Bytes16 x) { _mm_storeu_si128((__m128i*)ptr, x); }
inline void SimdStoreA(void* ptr, Bytes16 x) { _mm_store_si128((__m128i*)ptr, x); }

template<int lane> inline uint8_t SimdGetLane(Bytes16 x) { return _mm_extract_epi8(x, lane); }
template<int lane> inline Bytes16 SimdSetLane(Bytes16 x, uint8_t v) { return _mm_insert_epi8(x, v, lane); }
template<int index> inline Bytes16 SimdConcat(Bytes16 hi, Bytes16 lo) { return _mm_alignr_epi8(hi, lo, index); }

inline Bytes16 SimdAdd(Bytes16 a, Bytes16 b) { return _mm_add_epi8(a, b); }
inline Bytes16 SimdSub(Bytes16 a, Bytes16 b) { return _mm_sub_epi8(a, b); }

inline Bytes16 SimdShuffle(Bytes16 x, Bytes16 table) { return _mm_shuffle_epi8(x, table); }
inline Bytes16 SimdInterleaveL(Bytes16 a, Bytes16 b) { return _mm_unpacklo_epi8(a, b); }
inline Bytes16 SimdInterleaveR(Bytes16 a, Bytes16 b) { return _mm_unpackhi_epi8(a, b); }
inline Bytes16 SimdInterleave4L(Bytes16 a, Bytes16 b) { return _mm_unpacklo_epi32(a, b); }
inline Bytes16 SimdInterleave4R(Bytes16 a, Bytes16 b) { return _mm_unpackhi_epi32(a, b); }

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
inline Bytes16 SimdLoadA(const void* ptr) { return vld1q_u8((const uint8_t*)ptr); }
inline void SimdStore(void* ptr, Bytes16 x) { vst1q_u8((uint8_t*)ptr, x); }
inline void SimdStoreA(void* ptr, Bytes16 x) { vst1q_u8((uint8_t*)ptr, x); }

template<int lane> inline uint8_t SimdGetLane(Bytes16 x) { return vgetq_lane_u8(x, lane); }
template<int lane> inline Bytes16 SimdSetLane(Bytes16 x, uint8_t v) { return vsetq_lane_u8(v, x, lane); }
template<int index> inline Bytes16 SimdConcat(Bytes16 hi, Bytes16 lo) { return vextq_u8(lo, hi, index); }

inline Bytes16 SimdAdd(Bytes16 a, Bytes16 b) { return vaddq_u8(a, b); }
inline Bytes16 SimdSub(Bytes16 a, Bytes16 b) { return vsubq_u8(a, b); }

inline Bytes16 SimdShuffle(Bytes16 x, Bytes16 table) { return vqtbl1q_u8(x, table); }
inline Bytes16 SimdInterleaveL(Bytes16 a, Bytes16 b) { return vzip1q_u8(a, b); }
inline Bytes16 SimdInterleaveR(Bytes16 a, Bytes16 b) { return vzip2q_u8(a, b); }
inline Bytes16 SimdInterleave4L(Bytes16 a, Bytes16 b) { return vreinterpretq_u8_u32(vzip1q_u32(vreinterpretq_u32_u8(a), vreinterpretq_u32_u8(b))); }
inline Bytes16 SimdInterleave4R(Bytes16 a, Bytes16 b) { return vreinterpretq_u8_u32(vzip2q_u32(vreinterpretq_u32_u8(a), vreinterpretq_u32_u8(b))); }

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
    uint8_t prev = 0;
    for (int ich = 0; ich < channels; ++ich)
    {
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
    uint8_t prev = 0;
    for (int ich = 0; ich < channels; ++ich)
    {
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


// .cube file format:
// https://resolve.cafe/developers/luts/
// 1D or 3D LUTs, with an optional shaper LUT before it
//
// LUT_1D_SIZE N (up to 65536)
// LUT_1D_INPUT_RANGE MIN_VAL MAX_VAL
// follows: N lines, each is 3 floats
//
// LUT_3D_SIZE N (up to 256)
// LUT_3D_INPUT_RANGE MIN_VAL MAX_VAL
// follows: N*N*N lines, each is 3 floats
//
// shaper LUT: defined same as 1D LUT, before the actual LUT. remaps input axis.
//
// optional props:
// # are comment lines
// TITLE "Description"
// LUT_IN_VIDEO_RANGE
// LUT_OUT_VIDEO_RANGE
//
// Adobe Cube LUT Specification 1.0 (2013)
//
// - lines no longer than 250 chars
// - newlines are LF
// - each line is a keyword, a comment, or table entry
// - all keywords are before any table data
// - keywords appear in any order, but each of them just once
// - leading or trailing spaces are allowed, except comment lines
//   can't heave leading space
// - in 3D LUT, the red axis changes most rapidly, i.e. index is r+N*g+N*N*b
//
// Keywords:
// TITLE "text"
// DOMAIN_MIN rl gl bl
// DOMAIN_MAX rh gh bh



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

size_t smcube_data_type_get_size(smcube_data_type type)
{
    switch (type) {
    case smcube_data_type::Float32: return 4;
    case smcube_data_type::Float16: return 2;
    default: assert(false); return 0;
    }
}

size_t smcube_lut_get_data_size(const smcube_lut& lut)
{
    size_t item_size = smcube_data_type_get_size(lut.data_type) * lut.channels;
    size_t item_count = 1;
    if (lut.dimension >= 1) item_count *= lut.size_x;
    if (lut.dimension >= 2) item_count *= lut.size_y;
    if (lut.dimension >= 3) item_count *= lut.size_z;
    return item_count * item_size;
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

bool smcube_luts_save_to_file_smcube(const char* path, const smcube_luts* luts, bool use_filter)
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
    for (const smcube_lut& lut : luts->luts)
    {
        fwrite("ALut", 1, 4, f);
        const uint64_t data_item_len = lut.channels * smcube_data_type_get_size(lut.data_type);
        const uint64_t data_items = lut.size_x * lut.size_y * lut.size_z;
        const uint64_t data_size = data_item_len * data_items;
        const uint64_t chunk_len = sizeof(smcube_file_alut_header) + data_size;
        fwrite(&chunk_len, sizeof(chunk_len), 1, f);
        smcube_file_alut_header head;
        head.channels = lut.channels;
        head.dimension = lut.dimension;
        head.data_type = uint32_t(lut.data_type);
        head.filter = use_filter ? 1 : 0;
        head.size_x = lut.size_x;
        head.size_y = lut.size_y;
        head.size_z = lut.size_z;
        fwrite(&head, sizeof(head), 1, f);

        if (use_filter) {
            uint8_t* filtered_data = new uint8_t[data_size];
            FilterByteDelta((const uint8_t*)lut.data, filtered_data, int(data_item_len), data_items);
            fwrite(filtered_data, 1, data_size, f);
            delete[] filtered_data;
        }
        else {
            fwrite(lut.data, 1, data_size, f);
        }
    }

    fclose(f);
    return true;
}

const char* smcube_luts_get_title(const smcube_luts* handle)
{
    if (handle == nullptr)
        return "";
    return handle->title.c_str();
}

const char* smcube_luts_get_comment(const smcube_luts* handle)
{
    if (handle == nullptr)
        return "";
    return handle->comment.c_str();
}

size_t smcube_luts_get_count(const smcube_luts* handle)
{
    if (handle == nullptr)
        return 0;
    return handle->luts.size();
}

smcube_lut smcube_luts_get_lut(const smcube_luts* handle, size_t index)
{
    if (handle == nullptr || index >= handle->luts.size())
        return smcube_lut();
    return handle->luts[index];
}

smcube_luts* smcube_luts_load_from_file_smcube(const char* path)
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
        smcube_luts_free(luts);
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
            smcube_luts_free(luts);
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
                smcube_luts_free(luts);
                return nullptr;
            }

            smcube_lut lut;
            lut.channels = head.channels;
            lut.dimension = head.dimension;
            lut.data_type = smcube_data_type(head.data_type);
            lut.size_x = head.size_x;
            lut.size_y = head.size_y;
            lut.size_z = head.size_z;
            size_t lut_data_size = smcube_lut_get_data_size(lut);
            if (chunk_len - sizeof(smcube_file_alut_header) != lut_data_size)
            {
                smcube_luts_free(luts);
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

void smcube_luts_free(smcube_luts* handle)
{
    if (handle)
        delete[] handle->file_data;
    delete handle;
}

smcube_luts* smcube_luts_load_from_file_resolve_cube(const char* path)
{
    if (path == nullptr)
        return nullptr;

    FILE* f = fopen(path, "rb");
    if (f == nullptr)
        return nullptr;

    char buf[1000];

    // read header
    int dim_3d = 0, dim_1d = 0;
    while (true) {
        char* res = fgets(buf, sizeof(buf), f);
        if (!res)
            break;
        if (buf[0] >= '+' && buf[0] <= '9') // line starts with a number: header is done
            break;
        int tmp;
        if (1 == sscanf(buf, "LUT_1D_SIZE %i", &tmp)) {
            dim_1d = tmp;
        }
        if (1 == sscanf(buf, "LUT_3D_SIZE %i", &tmp)) {
            dim_3d = tmp;
        }
        //@TODO: title, comment
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
        float x, y, z;
        if (3 == sscanf(buf, "%f %f %f", &x, &y, &z)) {
            if (dim_1d > 0 && read_1d < dim_1d)
            {
                ((float*)lut1d.data)[read_1d * 3 + 0] = x;
                ((float*)lut1d.data)[read_1d * 3 + 1] = y;
                ((float*)lut1d.data)[read_1d * 3 + 2] = z;
                ++read_1d;
            }
            else if (dim_3d > 0 && read_3d < dim_3d * dim_3d * dim_3d)
            {
                ((float*)lut3d.data)[read_3d * 3 + 0] = x;
                ((float*)lut3d.data)[read_3d * 3 + 1] = y;
                ((float*)lut3d.data)[read_3d * 3 + 2] = z;
                ++read_3d;
            }
            else
            {
                fclose(f);
                smcube_luts_free(luts);
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
        smcube_luts_free(luts);
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

bool smcube_luts_save_to_file_resolve_cube(const char* path, const smcube_luts* luts)
{
    // argument checks
    if (path == nullptr || luts == nullptr || luts->luts.empty())
        return false;

    FILE* f = fopen(path, "wb");
    if (f == nullptr)
        return false;

    // write header
    fprintf(f, "# written by smol-cube\n");
    //@TODO: comment, title
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
        const float* data_end = data + smcube_lut_get_data_size(lut) / sizeof(float);
        while (data < data_end)
        {
            fprintf(f, "%f %f %f\n", data[0], data[1], data[2]);
            data += 3;
        }
    }

    fclose(f);
    return true;
}
