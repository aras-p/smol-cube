#pragma once

#include <stdint.h>

enum class smol_cube_data_type : uint32_t
{
	Float = 0,
};

enum class smol_cube_result : uint32_t
{
	Ok = 0,
	FileNotFound,
	InvalidHeaderData,
	InvalidContentData,
};

struct smol_cube_lut
{
	uint32_t channels; // 3=RGB
	uint32_t dimension; // 1=1D, 2=2D, 3=3D
	smol_cube_data_type data_type;
	uint32_t size_x, size_y, size_z;
	void* data;
};

smol_cube_result smol_cube_parse_cube_file(const char* path, smol_cube_lut*& r_3dlut, smol_cube_lut*& r_1dlut);
