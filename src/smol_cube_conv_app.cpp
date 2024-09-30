// SPDX-License-Identifier: MIT OR Unlicense
// smol-cube: https://github.com/aras-p/smol-cube

#include "smol_cube.h"

#include "../libs/argh/argh.h"

static bool are_luts_equal(const smcube_luts* ha, size_t ia, const smcube_luts* hb, size_t ib)
{
	const int dima = smcube_lut_get_dimension(ha, ia);
	const int dimb = smcube_lut_get_dimension(hb, ib);
	if (dima != dimb) return false;

	const int sizeax = smcube_lut_get_size_x(ha, ia), sizeay = smcube_lut_get_size_y(ha, ia), sizeaz = smcube_lut_get_size_z(ha, ia);
	const int sizebx = smcube_lut_get_size_x(hb, ib), sizeby = smcube_lut_get_size_y(hb, ib), sizebz = smcube_lut_get_size_z(hb, ib);
	if (sizeax != sizebx) return false;
	if (sizeay != sizeby) return false;
	if (sizeaz != sizebz) return false;

	const int channelsa = smcube_lut_get_channels(ha, ia);
	const int channelsb = smcube_lut_get_channels(hb, ib);
	smcube_data_type typea = smcube_lut_get_data_type(ha, ia);
	smcube_data_type typeb = smcube_lut_get_data_type(hb, ib);

	if (channelsa == channelsb && typea == typeb) {
		if (memcmp(smcube_lut_get_data(ha, ia), smcube_lut_get_data(hb, ib), smcube_lut_get_data_size(ha, ia)) != 0)
			return false;
	}

	// convert to Float32 RGBA
	bool ok = true;
	const size_t float_count = sizeax * sizeay * sizeaz * 4;
	float* rgbaa = new float[float_count];
	float* rgbab = new float[float_count];
	smcube_lut_convert_data(ha, ia, smcube_data_type::Float32, 4, rgbaa);
	smcube_lut_convert_data(hb, ib, smcube_data_type::Float32, 4, rgbab);
	float maxdiff = 0.0f;
	for (size_t i = 0; i < float_count; ++i)
	{
		float diff = fabsf(rgbaa[i] - rgbab[i]);
		if (diff > maxdiff)
			maxdiff = diff;
	}
	if (typea == typeb)
	{
		if (maxdiff > 0.0f)
			ok = false; // LUTs of the same type should have no difference at all (besides channels being different)
	}
	else
	{
		if (maxdiff > 0.004f)
			ok = false; // different types (FP32 vs FP16) allow some difference, hand picked here
	}

	delete[] rgbaa;
	delete[] rgbab;
	return ok;
}

int main(int argc, const char** argv)
{
	argh::parser args(argc, argv);
	const auto& input_files = args.pos_args();
	if (input_files.size() <= 1)
	{
		printf("Usage: smol-cube-conv [flags] <input file> ...\n");
		printf("\n");
		printf("Without extra arguments, this will convert given input .cube file(s) into .smcube files\n");
		printf("with lossless data filtering (making them more compressible), and keeping the data\n");
		printf("in full Float32 precision. Optional flags:\n");
		printf("\n");
		printf("--float16     Convert data into Float16 (half precision floats)\n");
		printf("--rgba        Expand data from RGB to RGB(A) (A being unused)\n");
		printf("--nofilter    Do not perform data filtering to improve compressability\n");
		return 1;
	}

	const bool nofilter = args["nofilter"];
	const bool float16 = args["float16"];
	const bool rgba = args["rgba"];
	const bool verbose = args["verbose"];
	const bool roundtrip = args["roundtrip"];

	uint32_t save_flags = nofilter ? smcube_save_flag_None : smcube_save_flag_FilterData;
	if (float16) save_flags |= smcube_save_flag_ConvertToFloat16;
	if (rgba) save_flags |= smcube_save_flag_ExpandTo4Channels;

	int exit_code = 0;
	for (size_t idx = 1; idx < input_files.size(); ++idx)
	{
		// read input file
		const std::string& input_file = input_files[idx];
		smcube_luts* input_luts = smcube_load_from_file_resolve_cube(input_file.c_str());
		if (input_luts == nullptr)
		{
			printf("ERROR: failed to parse input file '%s'\n", input_file.c_str());
			exit_code = 1;
			continue;
		}
		if (verbose)
		{
			printf("Input file %s: %zi luts\n", input_file.c_str(), smcube_get_count(input_luts));
			const char* input_title = smcube_get_title(input_luts);
			if (input_title != nullptr && input_title[0])
				printf("- Title '%s'\n", input_title);
			const char* input_comment = smcube_get_comment(input_luts);
			if (input_comment != nullptr && input_comment[0])
				printf("- Comment '%s'\n", input_comment);
			for (size_t lut_idx = 0; lut_idx < smcube_get_count(input_luts); ++lut_idx)
			{
				const int dim = smcube_lut_get_dimension(input_luts, lut_idx);
				const int sizex = smcube_lut_get_size_x(input_luts, lut_idx);
				const int sizey = smcube_lut_get_size_y(input_luts, lut_idx);
				const int sizez = smcube_lut_get_size_z(input_luts, lut_idx);
				switch (dim) {
				case 1: printf("- 1D LUT: %i\n", sizex); break;
				case 2: printf("- 2D LUT: %ix%i\n", sizex, sizey); break;
				case 3: printf("- 3D LUT: %ix%ix%i\n", sizex, sizey, sizez); break;
				}
			}
		}

		// write output smol-cube file
		size_t last_dot_pos = input_file.rfind('.');
		if (last_dot_pos == std::string::npos)
		{
			printf("ERROR: input file '%s' has no extension\n", input_file.c_str());
			exit_code = 1;
			smcube_free(input_luts);
			continue;
		}
		std::string output_file = input_file.substr(0, last_dot_pos);
		output_file += '_';
		output_file += float16 ? "half" : "float";
		output_file += rgba ? "4" : "3";
		if (nofilter)
			output_file += "_nofilter";
		output_file += ".smcube";
		if (verbose)
		{
			printf("- Output file '%s'\n", output_file.c_str());
		}

		if (!smcube_save_to_file_smcube(output_file.c_str(), input_luts, smcube_save_flags(save_flags)))
		{
			printf("ERROR: failed to write output file '%s'\n", output_file.c_str());
			exit_code = 1;
			smcube_free(input_luts);
			continue;
		}

		// read the written smol-cube file
		if (roundtrip)
		{
			smcube_luts* rtrip_luts = smcube_load_from_file_smcube(output_file.c_str());
			if (rtrip_luts == nullptr)
			{
				printf("ERROR: failed to read written smcube file '%s'\n", output_file.c_str());
				exit_code = 1;
			}
			else
			{
				size_t input_lut_count = smcube_get_count(input_luts);
				size_t rtrip_lut_count = smcube_get_count(rtrip_luts);
				if (input_lut_count != rtrip_lut_count)
				{
					printf("ERROR: smcube file '%s' has LUT count %zi, input had LUT count %zi\n", output_file.c_str(), rtrip_lut_count, input_lut_count);
					exit_code = 1;
				}
				else
				{
					for (size_t lut_idx = 0; lut_idx < input_lut_count; ++lut_idx)
					{
						if (!are_luts_equal(input_luts, lut_idx, rtrip_luts, lut_idx))
						{
							printf("ERROR: smcube file '%s' LUT #%zi not same as input\n", output_file.c_str(), lut_idx);
							exit_code = 1;
						}
					}
				}
				output_file = input_file.substr(0, last_dot_pos) + ".cube.txt";
				if (verbose)
				{
					printf("- Output roundtrip file '%s'\n", output_file.c_str());
				}
				smcube_save_to_file_resolve_cube(output_file.c_str(), rtrip_luts);
			}
			smcube_free(rtrip_luts);
		}

		smcube_free(input_luts);
	}
	return exit_code;
}
