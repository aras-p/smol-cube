#include "compression_helpers.h"

#include <zstd.h>
#include <meshoptimizer.h>

size_t compress_calc_bound(size_t srcItemCount, size_t srcItemSize, CompressionFormat format)
{
	if (srcItemCount == 0 || srcItemSize == 0)
		return 0;
	switch (format)
	{
	case kCompressionZstd: return ZSTD_compressBound(srcItemCount * srcItemSize);
	case kCompressionMeshOpt: return meshopt_encodeVertexBufferBound(srcItemCount, srcItemSize);
	default: return 0;
	}	
}
size_t compress_data(const void* src, size_t srcItemCount, size_t srcItemSize, void* dst, size_t dstSizeBytes, CompressionFormat format, int level)
{
	if (srcItemCount == 0 || srcItemSize == 0)
		return 0;
	switch (format)
	{
	case kCompressionZstd: return ZSTD_compress(dst, dstSizeBytes, src, srcItemCount * srcItemSize, level);
	case kCompressionMeshOpt: return meshopt_encodeVertexBuffer((unsigned char*)dst, dstSizeBytes, src, srcItemCount, srcItemSize);
	default: return 0;
	}
}
size_t decompress_data(const void* src, size_t srcSizeBytes, void* dst, size_t dstItemCount, size_t dstItemSize, CompressionFormat format)
{
	if (srcSizeBytes == 0 || dstItemCount == 0 || dstItemSize == 0)
		return 0;
	switch (format)
	{
	case kCompressionZstd: return ZSTD_decompress(dst, dstItemCount * dstItemSize, src, srcSizeBytes);
	case kCompressionMeshOpt: return meshopt_decodeVertexBuffer(dst, dstItemCount, dstItemSize, (const unsigned char*)src, srcSizeBytes) == 0 ? dstItemCount * dstItemSize : 0;
	default: return 0;
	}	
}

const char* get_compression_name(CompressionFormat fmt)
{
	switch (fmt) {
	case kCompressionZstd: return "zstd";
	case kCompressionMeshOpt: return "meshopt";
	default: return "<unknown compressor>";
	}
}

std::vector<int> get_compressor_levels(CompressionFormat fmt)
{
	switch (fmt)
	{
	case kCompressionZstd:
		return { -1, 3, 9, 15 };
	default:
		return { 0 };
	}
}

uint8_t* compress_data(const void* src, size_t srcItemCount, size_t srcItemSize, CompressionFormat format, int level, size_t& outSize)
{
	size_t bound = compress_calc_bound(srcItemCount, srcItemSize, format);
	uint8_t* cmp = new uint8_t[bound];
	outSize = compress_data(src, srcItemCount, srcItemSize, cmp, bound, format, level);
	return cmp;
}
