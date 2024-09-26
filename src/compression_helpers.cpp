#include "compression_helpers.h"

#include <zstd.h>
#include <meshoptimizer.h>

size_t compress_calc_bound(size_t srcItemCount, size_t srcItemSize, CompressionFormat format)
{
	if (srcItemCount == 0 || srcItemSize == 0)
		return 0;
	switch (format)
	{
	case kCompressionZstd:
		return ZSTD_compressBound(srcItemCount * srcItemSize);
	case kCompressionMeshOpt:
		return meshopt_encodeVertexBufferBound(srcItemCount, srcItemSize);
	case kCompressionMeshOptZstd:
		return ZSTD_compressBound(meshopt_encodeVertexBufferBound(srcItemCount, srcItemSize));
	default: return 0;
	}	
}
uint8_t* compress_data(const void* src, size_t srcItemCount, size_t srcItemSize, CompressionFormat format, int level, size_t& outSize)
{
	outSize = 0;
	if (srcItemCount == 0 || srcItemSize == 0)
		return nullptr;

	size_t bound = compress_calc_bound(srcItemCount, srcItemSize, format);
	uint8_t* dst = new uint8_t[bound];
	switch (format)
	{
	case kCompressionZstd: outSize = ZSTD_compress(dst, bound, src, srcItemCount * srcItemSize, level); break;
	case kCompressionMeshOpt: outSize = meshopt_encodeVertexBuffer((unsigned char*)dst, bound, src, srcItemCount, srcItemSize); break;
	case kCompressionMeshOptZstd: {
		size_t tmpBound = compress_calc_bound(srcItemCount, srcItemSize, kCompressionMeshOpt);
		uint8_t* tmp = new uint8_t[tmpBound];
		size_t tmpSize = meshopt_encodeVertexBuffer(tmp, tmpBound, src, srcItemCount, srcItemSize);
		outSize = ZSTD_compress(dst, bound, tmp, tmpSize, level);
		delete[] tmp;
		break;
	}
	}
	return dst;
}
size_t decompress_data(const void* src, size_t srcSizeBytes, void* dst, size_t dstItemCount, size_t dstItemSize, CompressionFormat format)
{
	if (srcSizeBytes == 0 || dstItemCount == 0 || dstItemSize == 0)
		return 0;
	switch (format)
	{
	case kCompressionZstd: return ZSTD_decompress(dst, dstItemCount * dstItemSize, src, srcSizeBytes);
	case kCompressionMeshOpt: return meshopt_decodeVertexBuffer(dst, dstItemCount, dstItemSize, (const unsigned char*)src, srcSizeBytes) == 0 ? dstItemCount * dstItemSize : 0;
	case kCompressionMeshOptZstd:
	{
		size_t tmpSize = ZSTD_getFrameContentSize(src, srcSizeBytes);
		uint8_t* tmp = new uint8_t[tmpSize];
		int64_t decSize = ZSTD_decompress(tmp, tmpSize, src, srcSizeBytes);
		meshopt_decodeVertexBuffer(dst, dstItemCount, dstItemSize, tmp, tmpSize);
		delete[] tmp;
		return dstItemCount * dstItemSize;
	}
	default: return 0;
	}	
}

const char* get_compression_name(CompressionFormat fmt)
{
	switch (fmt) {
	case kCompressionZstd: return "zstd";
	case kCompressionMeshOpt: return "meshopt";
	case kCompressionMeshOptZstd: return "meshopt+zstd";
	default: return "<unknown compressor>";
	}
}

std::vector<int> get_compressor_levels(CompressionFormat fmt)
{
	switch (fmt)
	{
	case kCompressionZstd:
	case kCompressionMeshOptZstd:
		return { -1, 3, 9, 15 };
	default:
		return { 0 };
	}
}
