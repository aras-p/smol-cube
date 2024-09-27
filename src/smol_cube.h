#pragma once

#include <stddef.h>
#include <stdint.h>

enum class smcube_data_type
{
	Float32 = 0,
	Float16,
	DataTypeCount
};

enum class smcube_result
{
	Ok = 0,
	FileAccessError,
	InvalidArgument,
	InvalidHeaderData,
	InvalidContentData,
};

struct smcube_lut
{
	int channels = 3; // 3=RGB
	int dimension = 3; // 1=1D, 2=2D, 3=3D
	smcube_data_type data_type = smcube_data_type::Float32;
	int size_x = 1;
	int size_y = 1;
	int size_z = 1;
	void* data = nullptr;
};

struct smcube_luts;

size_t smcube_data_type_get_size(smcube_data_type type);
size_t smcube_lut_get_data_size(const smcube_lut& lut);

smcube_result smcube_write_file(const char* path, size_t lut_count, const smcube_lut* luts, bool use_filter, const char* title, const char* comment);

smcube_result smcube_luts_load_from_file(const char* path, smcube_luts*& r_luts);
void smcube_luts_free(smcube_luts* handle);

const char* smcube_luts_get_title(const smcube_luts* handle);
const char* smcube_luts_get_comment(const smcube_luts* handle);
size_t smcube_luts_get_count(const smcube_luts* handle);
smcube_lut smcube_luts_get_lut(const smcube_luts* handle, size_t index);

smcube_result smcube_load_from_resolve_cube_file(const char* path, smcube_lut& r_3dlut, smcube_lut& r_1dlut);
smcube_result smcube_save_to_resolve_cube_file(const char* path, const smcube_lut& lut3d, const smcube_lut& lut1d);
