#pragma once

#include <stddef.h>
#include <stdint.h>

enum class smcube_data_type : uint32_t
{
	Float32 = 0,
	Float16,
	DataTypeCount
};

enum class smcube_result : uint32_t
{
	Ok = 0,
	FileAccessError,
	InvalidArgument,
	InvalidHeaderData,
	InvalidContentData,
};

struct smcube_lut
{
	uint32_t channels = 3; // 3=RGB
	uint32_t dimension = 3; // 1=1D, 2=2D, 3=3D
	smcube_data_type data_type = smcube_data_type::Float32;
	uint32_t size_x = 1, size_y = 1, size_z = 1;
	void* data = nullptr;
};

struct smcube_file_handle;

size_t smcube_data_type_size(smcube_data_type type);
size_t smcube_lut_data_size(const smcube_lut& lut);

smcube_result smcube_write_file(const char* path, size_t lut_count, const smcube_lut* luts, bool use_filter, const char* title, const char* comment);

smcube_result smcube_read_file(const char* path, smcube_file_handle*& r_file);
void smcube_close_file(smcube_file_handle* handle);
const char* smcube_get_file_title(const smcube_file_handle* handle);
const char* smcube_get_file_comment(const smcube_file_handle* handle);
size_t smcube_get_file_lut_count(const smcube_file_handle* handle);
smcube_lut smcube_get_file_lut(const smcube_file_handle* handle, size_t index);

smcube_result smcube_load_from_resolve_cube_file(const char* path, smcube_lut& r_3dlut, smcube_lut& r_1dlut);
smcube_result smcube_save_to_resolve_cube_file(const char* path, const smcube_lut& lut3d, const smcube_lut& lut1d);
