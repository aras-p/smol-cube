#include "smol_cube.h"

#include "../libs/argh/argh.h"

/*
stm_setup();
{"../../../tests/luts/synthetic/shaper_3d.cube", 3},
{"../../../tests/luts/blender/AgX_Base_sRGB.cube", 3},
{"../../../tests/luts/blender/Inverse_AgX_Base_Rec2020.cube", 3},
{"../../../tests/luts/blender/pbrNeutral.cube", 3},
{"../../../tests/luts/davinci/DCI-P3 Kodak 2383 D65.cube", 3},
{"../../../tests/luts/davinci/Gamma 2.4 to HDR 1000 nits.cube", 3},
{"../../../tests/luts/davinci/LMT ACES v0.1.1.cube", 3},
{"../../../tests/luts/tinyglade/Bluecine_75.cube", 3},
{"../../../tests/luts/tinyglade/Cold_Ice.cube", 3},
{"../../../tests/luts/tinyglade/LUNA_COLOR.cube", 3},
{"../../../tests/luts/tinyglade/Sam_Kolder.cube", 3},
*/

static bool are_luts_equal(const smcube_lut& la, const smcube_lut& lb)
{
	if (la.channels != lb.channels) return false;
	if (la.dimension != lb.dimension) return false;
	if (la.data_type != lb.data_type) return false;
	if (la.size_x != lb.size_x) return false;
	if (la.size_y != lb.size_y) return false;
	if (la.size_z != lb.size_z) return false;
	if (memcmp(la.data, lb.data, smcube_lut_get_data_size(la)) != 0) return false;
	return true;
}

int main(int argc, const char** argv)
{
	argh::parser args(argc, argv);
	const auto& input_files = args.pos_args();
	if (input_files.size() <= 1)
	{
		printf("Usage: smol-cube <input .cube file>\n");
		return 1;
	}

	const bool verbose = args["verbose"];
	const bool roundtrip = args["roundtrip"];

	int exit_code = 0;
	for (size_t idx = 1; idx < input_files.size(); ++idx)
	{
		// read input file
		const std::string& input_file = input_files[idx];
		smcube_luts* input_luts = smcube_luts_load_from_file_resolve_cube(input_file.c_str());
		if (input_luts == nullptr)
		{
			printf("ERROR: failed to parse input file '%s'\n", input_file.c_str());
			exit_code = 1;
			continue;
		}
		if (verbose)
		{
			printf("Input file %s: %zi luts\n", input_file.c_str(), smcube_luts_get_count(input_luts));
			const char* input_title = smcube_luts_get_title(input_luts);
			if (input_title != nullptr && input_title[0])
				printf("- Title '%s'\n", input_title);
			const char* input_comment = smcube_luts_get_comment(input_luts);
			if (input_comment != nullptr && input_comment[0])
				printf("- Comment '%s'\n", input_comment);
			for (size_t lut_idx = 0; lut_idx < smcube_luts_get_count(input_luts); ++lut_idx)
			{
				smcube_lut lut = smcube_luts_get_lut(input_luts, lut_idx);
				switch (lut.dimension) {
				case 1: printf("- 1D LUT: %i\n", lut.size_x); break;
				case 2: printf("- 2D LUT: %ix%i\n", lut.size_x, lut.size_y); break;
				case 3: printf("- 3D LUT: %ix%ix%i\n", lut.size_x, lut.size_y, lut.size_z); break;
				}
			}
		}

		// write output smol-cube file
		size_t last_dot_pos = input_file.rfind('.');
		if (last_dot_pos == std::string::npos)
		{
			printf("ERROR: input file '%s' has no extension\n", input_file.c_str());
			exit_code = 1;
			smcube_luts_free(input_luts);
			continue;
		}
		std::string output_file = input_file.substr(0, last_dot_pos) + "_float3.smcube";
		if (verbose)
		{
			printf("- Output file '%s'\n", output_file.c_str());
		}

		if (!smcube_luts_save_to_file_smcube(output_file.c_str(), input_luts, true))
		{
			printf("ERROR: failed to write output file '%s'\n", output_file.c_str());
			exit_code = 1;
			smcube_luts_free(input_luts);
			continue;
		}

		// read the written smol-cube file
		if (roundtrip)
		{
			smcube_luts* rtrip_luts = smcube_luts_load_from_file_smcube(output_file.c_str());
			if (rtrip_luts == nullptr)
			{
				printf("ERROR: failed to read written smcube file '%s'\n", output_file.c_str());
				exit_code = 1;
			}
			else
			{
				size_t input_lut_count = smcube_luts_get_count(input_luts);
				size_t rtrip_lut_count = smcube_luts_get_count(rtrip_luts);
				if (input_lut_count != rtrip_lut_count)
				{
					printf("ERROR: smcube file '%s' has LUT count %zi, input had LUT count %zi\n", output_file.c_str(), rtrip_lut_count, input_lut_count);
					exit_code = 1;
				}
				else
				{
					for (size_t lut_idx = 0; lut_idx < input_lut_count; ++lut_idx)
					{
						smcube_lut la = smcube_luts_get_lut(input_luts, lut_idx);
						smcube_lut lb = smcube_luts_get_lut(rtrip_luts, lut_idx);
						if (!are_luts_equal(la, lb))
						{
							printf("ERROR: smcube file '%s' LUT #%zi not same as input\n", output_file.c_str(), lut_idx);
							exit_code = 1;
						}
					}
				}
				output_file = input_file.substr(0, last_dot_pos) + ".out.cube";
				if (verbose)
				{
					printf("- Output roundtrip file '%s'\n", output_file.c_str());
				}
				smcube_luts_save_to_file_resolve_cube(output_file.c_str(), rtrip_luts);
			}
			smcube_luts_free(rtrip_luts);
		}

		smcube_luts_free(input_luts);
	}
	return exit_code;
}
