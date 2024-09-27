#pragma once

#include <stddef.h>
#include <stdint.h>

enum class smol_cube_data_type : uint32_t
{
	Float32 = 0,
	Float16,
	DataTypeCount
};

enum class smol_cube_result : uint32_t
{
	Ok = 0,
	FileAccessError,
	InvalidArgument,
	InvalidHeaderData,
	InvalidContentData,
};

struct smol_cube_lut
{
	uint32_t channels = 3; // 3=RGB
	uint32_t dimension = 3; // 1=1D, 2=2D, 3=3D
	smol_cube_data_type data_type = smol_cube_data_type::Float32;
	uint32_t size_x = 1, size_y = 1, size_z = 1;
	void* data = nullptr;
};

struct smol_cube_file_handle;

size_t smol_cube_data_type_size(smol_cube_data_type type);
size_t smol_cube_lut_data_size(const smol_cube_lut& lut);

smol_cube_result smol_cube_write_file(const char* path, size_t lut_count, const smol_cube_lut* luts, bool use_filter, const char* title, const char* comment);

smol_cube_result smol_cube_read_file(const char* path, smol_cube_file_handle*& r_file);
void smol_cube_close_file(smol_cube_file_handle* handle);

smol_cube_result smol_cube_parse_resolve_cube_file(const char* path, smol_cube_lut& r_3dlut, smol_cube_lut& r_1dlut);
