// SPDX-License-Identifier: MIT OR Unlicense
// smol-cube: https://github.com/aras-p/smol-cube

#pragma once

#include <stddef.h>
#include <stdint.h>

enum class smcube_data_type
{
	Float32 = 0, // Data is 32 bit floating point
	Float16,     // Data is 16 bit (half-precision) float
	DataTypeCount
};

// Flags used in `smcube_save_to_file_smcube`.
// They can be combined together.
enum smcube_save_flags
{
	smcube_save_flag_None = 0,

	// Apply lossless data filter to make the file more compressible.
	// Usually you want this if you plan to compress the file with
	// zlib/zstd/lz4 or similar general purpose compressors. This costs
	// a tiny bit of performance when loading, but makes the data 2-3x more
	// compressible.
	smcube_save_flag_FilterData = (1 << 0),

	// Convert LUT data to half-precision (16 bit) floating point format.
	smcube_save_flag_ConvertToFloat16 = (1 << 1),

	// Make the data be 4 channels (RGBA) instead of usual 3 (RGB).
	// The fourth channel is not really used, but LUT in this format
	// can be faster and more convenient to load onto GPU. Since many
	// 3D APIs do not support 3-channel textures directly.
	smcube_save_flag_ExpandTo4Channels = (1 << 2),
};

struct smcube_luts;

// Load LUT(s) from a file at given path.
//
// If file path ends with ".smcube" assumes it is a binary smol-cube file,
// and effectively calls `smcube_load_from_file_smcube`.
// If file path ends with ".cube" assumes it is a Resolve/Adobe LUT file,
// and effectively calls `smcube_load_from_file_resolve_cube`.
//
// Use the resulting opaque handle in other functions to query/inspect/save
// the LUT(s). Use `smcube_free` to delete the LUT(s).
//
// Returns nullptr in case of failure.
smcube_luts* smcube_load_from_file(const char* path);

// Load LUT(s) from smol-cube binaty file at path.
smcube_luts* smcube_load_from_file_smcube(const char* path);

// Load LUT(s) from Resolve/Adobe LUT file at path.
smcube_luts* smcube_load_from_file_resolve_cube(const char* path);

// Delete the LUT(s).
void smcube_free(smcube_luts* handle);

// Save LUT(s) to smol-cube format file.
// Flags control filtering, format and channel conversion.
// Returns true if all is ok.
bool smcube_save_to_file_smcube(const char* path, const smcube_luts* luts, smcube_save_flags flags = smcube_save_flag_None);

// Save LUT(s) to Resolve/Adobe LUT format file.
//
// Note that this only supports LUTs that the Resolve format can handle:
// - 3 channels,
// - 32 bit floating point,
// - one 1D LUT, or one 3D LUT, or one 1D LUT followed by one 3D LUT,
// - 3D LUT, if present, must have the same x/y/z sizes.
//
// Returns true if all is ok.
bool smcube_save_to_file_resolve_cube(const char* path, const smcube_luts* luts);

// Get "title" metadata of the LUT.
// Title is optional and nullptr would be returned in that case.
const char* smcube_get_title(const smcube_luts* handle);

// Get "comment" metadata of the LUT.
// Comment is optional and nullptr would be returned in that case.
const char* smcube_get_comment(const smcube_luts* handle);

// Get number of LUTs.
// Most files contain just one LUT, but some could contain more
// (typical combination is 1D "shaper" LUT followed by a 3D LUT).
size_t smcube_get_count(const smcube_luts* handle);

// Get number of channels in a LUT (3 = RGB, 4 = RGBA).
int smcube_lut_get_channels(const smcube_luts* handle, size_t index);

// Get dimension of a LUT (1 = 1D, 2 = 2D, 3 = 3D).
int smcube_lut_get_dimension(const smcube_luts* handle, size_t index);

// Get data type of the LUT data.
smcube_data_type smcube_lut_get_data_type(const smcube_luts* handle, size_t index);

// Get LUT size in X dimension.
int smcube_lut_get_size_x(const smcube_luts* handle, size_t index);
// Get LUT size in Y dimension (only relevant for 2D/3D LUTs).
int smcube_lut_get_size_y(const smcube_luts* handle, size_t index);
// Get LUT size in Z dimension (only relevant for 3D LUTs).
int smcube_lut_get_size_z(const smcube_luts* handle, size_t index);

// Get the actual data of the LUT.
//
// Data is laid out in row-major order, i.e. X dimension (which usually
// means "red") changes the fastest, and Z dimension (which usually
// means "blue") changes the slowest.
//
// The data is `size_x * size_y * size_z * channels` numbers in either
// float (4 bytes/number) or half-float (2 bytes/number) depending on
// LUT data type.
//
// If data is 4 channels, then the data can be directly loaded into a
// 3D texture on most/all graphics APIs.
const void* smcube_lut_get_data(const smcube_luts* handle, size_t index);

// Calculate LUT data size in bytes.
size_t smcube_lut_get_data_size(const smcube_luts* handle, size_t index);

// Calculate byte size of the data type (4 or 2 currently).
size_t smcube_data_type_get_size(smcube_data_type type);

// Convert LUT data into a different format or channel count.
//
// Note that this leaves LUT data unchanged; new data is written into
// a buffer that you provided. Destination buffer must contain
// space for `size_x * size_y * size_z * dst_channels` numbers of
// `dst_type` format.
void smcube_lut_convert_data(const smcube_luts* handle, size_t index, smcube_data_type dst_type, int dst_channels, void* dst_data);
