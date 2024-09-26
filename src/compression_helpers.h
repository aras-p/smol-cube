#pragma once

#include <stdint.h>
#include <stddef.h>
#include <vector>

// generic lossless compressors
enum CompressionFormat
{
	kCompressionZstd = 0,
	kCompressionMeshOpt,
	kCompressionMeshOptZstd,
	kCompressionCount
};
size_t compress_calc_bound(size_t srcItemCount, size_t srcItemSize, CompressionFormat format);
uint8_t* compress_data(const void* src, size_t srcItemCount, size_t srcItemSize, CompressionFormat format, int level, size_t& outSize);
size_t decompress_data(const void* src, size_t srcSizeBytes, void* dst, size_t dstItemCount, size_t dstItemSize, CompressionFormat format);

const char* get_compression_name(CompressionFormat fmt);
std::vector<int> get_compressor_levels(CompressionFormat fmt);
