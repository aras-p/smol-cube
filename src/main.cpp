#include "smol_cube.h"

#define SOKOL_TIME_IMPL
#include "../libs/sokol_time.h"

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
		smol_cube_lut lut3d, lut1d;
		smol_cube_result res = smol_cube_parse_resolve_cube_file(input_file.c_str(), lut3d, lut1d);
		if (res != smol_cube_result::Ok)
		{
			printf("ERROR: failed to read input file '%s' (error %i)\n", input_file.c_str(), int(res));
			exit_code = 1;
			continue;
		}
		if (verbose)
		{
			printf("Input file %s:\n", input_file.c_str());
			if (lut1d.data)
			{
				printf("- 1D LUT: size %i\n", lut1d.size_x);
			}
			if (lut3d.data)
			{
				printf("- 3D LUT: size %ix%ix%i\n", lut3d.size_x, lut3d.size_y, lut3d.size_z);
			}
		}

		// write output smol-cube file
		size_t last_dot_pos = input_file.rfind('.');
		if (last_dot_pos == std::string::npos)
		{
			printf("ERROR: input file '%s' has no extension\n", input_file.c_str());
			exit_code = 1;
			if (lut3d.data) delete[] lut3d.data;
			if (lut1d.data) delete[] lut1d.data;
			continue;
		}
		std::string output_file = input_file.substr(0, last_dot_pos) + ".smcube";
		if (verbose)
		{
			printf("- Output file '%s'\n", output_file.c_str());
		}

		std::vector<smol_cube_lut> output_luts;
		if (lut1d.data)
			output_luts.push_back(lut1d);
		if (lut3d.data)
			output_luts.push_back(lut3d);
		res = smol_cube_write_file(output_file.c_str(), output_luts.size(), output_luts.data(), true, "Test", "Foobar");
		if (res != smol_cube_result::Ok)
		{
			printf("ERROR: failed to write output file '%s' (error %i)\n", output_file.c_str(), int(res));
			exit_code = 1;
		}

		if (lut3d.data) delete[] lut3d.data;
		if (lut1d.data) delete[] lut1d.data;

		// read the written smol-cube file
		if (roundtrip)
		{
			smol_cube_file_handle* fh = nullptr;
			res = smol_cube_read_file(output_file.c_str(), fh);
			if (res != smol_cube_result::Ok)
			{
				printf("ERROR: failed to read written smcube file '%s' (error %i)\n", output_file.c_str(), int(res));
				exit_code = 1;
			}
			smol_cube_close_file(fh);
		}
	}
	return exit_code;
}
