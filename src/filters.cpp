#include "filters.h"
#include "simd.h"
#include <assert.h>
#include <string.h>

const size_t kMaxChannels = 64;
static_assert(kMaxChannels >= 16, "max channels can't be lower than simd width");


// no-op filter: just a memcpy
void Filter_Null(const uint8_t* src, uint8_t* dst, int channels, size_t dataElems)
{
    memcpy(dst, src, channels * dataElems);
}

void UnFilter_Null(const uint8_t* src, uint8_t* dst, int channels, size_t dataElems)
{
    memcpy(dst, src, channels * dataElems);
}


// Part 6 "A"
template<typename T> static void EncodeDelta(T* data, size_t dataElems)
{
    T prev = 0;
    for (size_t i = 0; i < dataElems; ++i)
    {
        T v = *data;
        *data = v - prev;
        prev = v;
        ++data;
    }
}
template<typename T> static void DecodeDelta(T* data, size_t dataElems)
{
    T prev = 0;
    for (size_t i = 0; i < dataElems; ++i)
    {
        T v = *data;
        v = prev + v;
        *data = v;
        prev = v;
        ++data;
    }
}
template<typename T> static void Split(const T* src, T* dst, int channels, size_t dataElems)
{
    for (int ich = 0; ich < channels; ++ich)
    {
        const T* ptr = src + ich;
        for (size_t ip = 0; ip < dataElems; ++ip)
        {
            *dst = *ptr;
            ptr += channels;
            dst += 1;
        }
    }
}
template<typename T> static void UnSplit(const T* src, T* dst, int channels, size_t dataElems)
{
    for (int ich = 0; ich < channels; ++ich)
    {
        T* ptr = dst + ich;
        for (size_t ip = 0; ip < dataElems; ++ip)
        {
            *ptr = *src;
            src += 1;
            ptr += channels;
        }
    }
}

void Filter_Split(const uint8_t* src, uint8_t* dst, int channels, size_t dataElems)
{
    Split<uint8_t>(src, dst, channels, dataElems);
}
void UnFilter_Split(const uint8_t* src, uint8_t* dst, int channels, size_t dataElems)
{
    UnSplit<uint8_t>(src, dst, channels, dataElems);
}

void Filter_A(const uint8_t* src, uint8_t* dst, int channels, size_t dataElems)
{
    Split<uint8_t>(src, dst, channels, dataElems);
    EncodeDelta<uint8_t>(dst, channels * dataElems);
}
void UnFilter_A(const uint8_t* src, uint8_t* dst, int channels, size_t dataElems)
{
    DecodeDelta<uint8_t>((uint8_t*)src, channels * dataElems);
    UnSplit<uint8_t>(src, dst, channels, dataElems);
}

// Part 6 "D"
void Filter_D(const uint8_t* src, uint8_t* dst, int channels, size_t dataElems)
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

void UnFilter_D(const uint8_t* src, uint8_t* dst, int channels, size_t dataElems)
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


// Transpose NxM byte matrix, with faster code paths for rows=16, cols=multiple-of-16 case.
// Largely based on https://fgiesen.wordpress.com/2013/07/09/simd-transposes-1/ and
// https://fgiesen.wordpress.com/2013/08/29/simd-transposes-2/
static void EvenOddInterleave16(const Bytes16* a, Bytes16* b, int astride = 1)
{
    int bidx = 0;
    for (int i = 0; i < 8; ++i)
    {
        b[bidx] = SimdInterleaveL(a[i * astride], a[(i + 8) * astride]); bidx++;
        b[bidx] = SimdInterleaveR(a[i * astride], a[(i + 8) * astride]); bidx++;
    }
}
static void Transpose16x16(const Bytes16* a, Bytes16* b, int astride = 1)
{
    Bytes16 tmp1[16], tmp2[16];
    EvenOddInterleave16(a, tmp1, astride);
    EvenOddInterleave16(tmp1, tmp2);
    EvenOddInterleave16(tmp2, tmp1);
    EvenOddInterleave16(tmp1, b);
}
static void Transpose(const uint8_t* a, uint8_t* b, int cols, int rows)
{
    if (rows == 16 && ((cols % 16) == 0))
    {
        int blocks = cols / rows;
        for (int i = 0; i < blocks; ++i)
        {
            Transpose16x16(((const Bytes16*)a) + i, ((Bytes16*)b) + i * 16, blocks);
        }
    }
    else
    {
        for (int j = 0; j < rows; ++j)
        {
            for (int i = 0; i < cols; ++i)
            {
                b[i * rows + j] = a[j * cols + i];
            }
        }
    }
}

// Fetch 16 N-sized items, transpose, SIMD delta, write N separate 16-sized items
void Filter_H(const uint8_t* src, uint8_t* dst, int channels, size_t dataElems)
{
    uint8_t* dstPtr = dst;
    int64_t ip = 0;
    
    const uint8_t* srcPtr = src;
    // simd loop
    Bytes16 prev[kMaxChannels] = {};
    for (; ip < int64_t(dataElems) - 15; ip += 16)
    {
        // fetch 16 data items
        uint8_t curr[kMaxChannels * 16];
        memcpy(curr, srcPtr, channels * 16);
        srcPtr += channels * 16;
        // transpose so we have 16 bytes for each channel
        Bytes16 currT[kMaxChannels];
        Transpose(curr, (uint8_t*)currT, channels, 16);
        // delta within each channel, store
        for (int ich = 0; ich < channels; ++ich)
        {
            Bytes16 v = currT[ich];
            Bytes16 delta = SimdSub(v, SimdConcat<15>(v, prev[ich]));
            SimdStore(dstPtr + dataElems * ich, delta);
            prev[ich] = v;
        }
        dstPtr += 16;
    }
    // any remaining leftover
    if (ip < int64_t(dataElems))
    {
        uint8_t prev1[kMaxChannels];
        for (int ich = 0; ich < channels; ++ich)
            prev1[ich] = SimdGetLane<15>(prev[ich]);
        for (; ip < int64_t(dataElems); ip++)
        {
            for (int ich = 0; ich < channels; ++ich)
            {
                uint8_t v = *srcPtr;
                srcPtr++;
                dstPtr[dataElems * ich] = v - prev1[ich];
                prev1[ich] = v;
            }
            dstPtr++;
        }
    }
}

// Fetch 16b from N streams, prefix sum SIMD undelta, transpose, sequential write 16xN chunk.
void UnFilter_H(const uint8_t* src, uint8_t* dst, int channels, size_t dataElems)
{
    uint8_t* dstPtr = dst;
    int64_t ip = 0;

    // simd loop: fetch 16 bytes from each stream
    Bytes16 curr[kMaxChannels] = {};
    const Bytes16 hibyte = SimdSet1(15);
    for (; ip < int64_t(dataElems) - 15; ip += 16)
    {
        // fetch 16 bytes from each channel, prefix-sum un-delta
        const uint8_t* srcPtr = src + ip;
        for (int ich = 0; ich < channels; ++ich)
        {
            Bytes16 v = SimdLoad(srcPtr);
            // un-delta via prefix sum
            curr[ich] = SimdAdd(SimdPrefixSum(v), SimdShuffle(curr[ich], hibyte));
            srcPtr += dataElems;
        }

        // now transpose 16xChannels matrix
        uint8_t currT[kMaxChannels * 16];
        Transpose((const uint8_t*)curr, currT, 16, channels);

        // and store into destination
        memcpy(dstPtr, currT, 16 * channels);
        dstPtr += 16 * channels;
    }

    // any remaining leftover
    if (ip < int64_t(dataElems))
    {
        uint8_t curr1[kMaxChannels];
        for (int ich = 0; ich < channels; ++ich)
            curr1[ich] = SimdGetLane<15>(curr[ich]);
        for (; ip < int64_t(dataElems); ip++)
        {
            const uint8_t* srcPtr = src + ip;
            for (int ich = 0; ich < channels; ++ich)
            {
                uint8_t v = *srcPtr + curr1[ich];
                curr1[ich] = v;
                *dstPtr = v;
                srcPtr += dataElems;
                dstPtr += 1;
            }
        }
    }
}
