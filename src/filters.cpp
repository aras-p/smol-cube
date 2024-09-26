#include "filters.h"
#include "simd.h"
#include <assert.h>
#include <string.h>

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
