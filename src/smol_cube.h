#pragma once

#include <stddef.h>
#include <stdint.h>

enum class smcube_data_type
{
	Float32 = 0,
	Float16,
	DataTypeCount
};

enum smcube_save_flags
{
	smcube_save_flag_None = 0,
	smcube_save_flag_FilterData = (1 << 0),
	smcube_save_flag_ConvertToFloat16 = (1 << 1),
	smcube_save_flag_ExpandTo4Channels = (1 << 2),
};

struct smcube_luts;

smcube_luts* smcube_luts_load_from_file(const char* path);
smcube_luts* smcube_luts_load_from_file_smcube(const char* path);
smcube_luts* smcube_luts_load_from_file_resolve_cube(const char* path);
void smcube_luts_free(smcube_luts* handle);

bool smcube_luts_save_to_file_smcube(const char* path, const smcube_luts* luts, smcube_save_flags flags = smcube_save_flag_None);
bool smcube_luts_save_to_file_resolve_cube(const char* path, const smcube_luts* luts);

const char* smcube_luts_get_title(const smcube_luts* handle);
const char* smcube_luts_get_comment(const smcube_luts* handle);
size_t smcube_luts_get_count(const smcube_luts* handle);

int smcube_luts_get_lut_channels(const smcube_luts* handle, size_t index);
int smcube_luts_get_lut_dimension(const smcube_luts* handle, size_t index);
smcube_data_type smcube_luts_get_lut_data_type(const smcube_luts* handle, size_t index);
int smcube_luts_get_lut_size_x(const smcube_luts* handle, size_t index);
int smcube_luts_get_lut_size_y(const smcube_luts* handle, size_t index);
int smcube_luts_get_lut_size_z(const smcube_luts* handle, size_t index);
const void* smcube_luts_get_lut_data(const smcube_luts* handle, size_t index);

size_t smcube_lut_get_data_size(const smcube_luts* handle, size_t index);
size_t smcube_data_type_get_size(smcube_data_type type);
