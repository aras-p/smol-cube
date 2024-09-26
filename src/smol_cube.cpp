#include "smol_cube.h"
#include <stdio.h>

// .cube file format:
// https://resolve.cafe/developers/luts/
// 1D or 3D LUTs, with an optional shaper LUT before it
//
// LUT_1D_SIZE N (up to 65536)
// LUT_1D_INPUT_RANGE MIN_VAL MAX_VAL
// follows: N lines, each is 3 floats
//
// LUT_3D_SIZE N (up to 256)
// LUT_3D_INPUT_RANGE MIN_VAL MAX_VAL
// follows: N*N*N lines, each is 3 floats
//
// shaper LUT: defined same as 1D LUT, before the actual LUT. remaps input axis.
//
// optional props:
// # are comment lines
// TITLE "Description"
// LUT_IN_VIDEO_RANGE
// LUT_OUT_VIDEO_RANGE
//
// Adobe Cube LUT Specification 1.0 (2013)
//
// - lines no longer than 250 chars
// - newlines are LF
// - each line is a keyword, a comment, or table entry
// - all keywords are before any table data
// - keywords appear in any order, but each of them just once
// - leading or trailing spaces are allowed, except comment lines
//   can't heave leading space
// - in 3D LUT, the red axis changes most rapidly, i.e. index is r+N*g+N*N*b
//
// Keywords:
// TITLE "text"
// DOMAIN_MIN rl gl bl
// DOMAIN_MAX rh gh bh



// chunks:
// - u32: FOURCC
// - u64: data_size
// - u8[data_size]: data
//
// header: SML1
// - no data
//
// meta title: Titl
// - data is the title
// meta comment: Comm
// - data is the comment
// meta domain: Domn
// - u32: channels (e.g. 3 for RGB)
// - f32[channels]: min range
// - f32[channels]: max range
// LUT/image: ALut
// - u32: usage (app specified)
// - u32: channels (e.g. 3 for RGB)
// - u32: dimension (1=1D, 2=2D, 3=3D)
// - u32: data type (0=float)
// - u32: compression (0=none)
// - u32: filter (0=none)
// - u32[dimension]: dimensions
// - data

smol_cube_result smol_cube_parse_resolve_cube_file(const char* path, smol_cube_lut*& r_3dlut, smol_cube_lut*& r_1dlut)
{
    r_3dlut = nullptr;
    r_1dlut = nullptr;

    FILE* f = fopen(path, "rb");
    if (f == nullptr)
        return smol_cube_result::FileNotFound;

    char buf[1000];

    // read header
    int dim_3d = 0, dim_1d = 0;
    size_t read_3d = 0, read_1d = 0;
    bool header = true;
    while (true) {
        char* res = fgets(buf, sizeof(buf), f);
        if (!res)
            break;
        if (header && buf[0] >= '+' && buf[0] <= '9') // line starts with a number: header is done
        {
            header = false;
            if (dim_1d < 0 || dim_1d > 65536)
            {
                fclose(f);
                return smol_cube_result::InvalidHeaderData;
            }
            if (dim_3d < 0 || dim_3d > 4096)
            {
                fclose(f);
                return smol_cube_result::InvalidHeaderData;
            }
            if (dim_1d == 0 && dim_3d == 0)
            {
                fclose(f);
                return smol_cube_result::InvalidHeaderData;
            }

            if (dim_1d > 0)
            {
                r_1dlut = new smol_cube_lut {
                    .channels = 3,
                    .dimension = 1,
                    .data_type = smol_cube_data_type::Float,
                    .size_x = uint32_t(dim_1d),
                    .size_y = 1,
                    .size_z = 1,
                    .data = new float[dim_1d * 3]
                };
            }
            if (dim_3d > 0)
            {
                r_3dlut = new smol_cube_lut{
                    .channels = 3,
                    .dimension = 3,
                    .data_type = smol_cube_data_type::Float,
                    .size_x = uint32_t(dim_3d),
                    .size_y = uint32_t(dim_3d),
                    .size_z = uint32_t(dim_3d),
                    .data = new float[dim_3d * dim_3d * dim_3d * 3]
                };
            }
        }

        if (header) {
            int tmp;
            if (1 == sscanf(buf, "LUT_1D_SIZE %i", &tmp)) {
                dim_1d = tmp;
            }
            if (1 == sscanf(buf, "LUT_3D_SIZE %i", &tmp)) {
                dim_3d = tmp;
            }
        }
        else {
            // read data entry
            float x, y, z;
            if (3 == sscanf(buf, "%f %f %f", &x, &y, &z)) {

                if (dim_1d > 0 && read_1d < dim_1d)
                {
                    ((float*)r_1dlut->data)[read_1d * 3 + 0] = x;
                    ((float*)r_1dlut->data)[read_1d * 3 + 1] = y;
                    ((float*)r_1dlut->data)[read_1d * 3 + 2] = z;
                    ++read_1d;
                }
                else if (dim_3d > 0 && read_3d < dim_3d * dim_3d * dim_3d)
                {
                    ((float*)r_3dlut->data)[read_3d * 3 + 0] = x;
                    ((float*)r_3dlut->data)[read_3d * 3 + 1] = y;
                    ((float*)r_3dlut->data)[read_3d * 3 + 2] = z;
                    ++read_3d;
                }
                else
                {
                    fclose(f);
                    return smol_cube_result::InvalidContentData;
                }
            }
        }
    }

    fclose(f);

    if (dim_1d > 0 && read_1d != dim_1d || dim_3d > 0 && read_3d != dim_3d * dim_3d * dim_3d)
    {
        return smol_cube_result::InvalidContentData;
    }
    return smol_cube_result::Ok;
}
