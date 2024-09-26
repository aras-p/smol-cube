#include <stdio.h>
#include <vector>
#include <algorithm>
#include "compression_helpers.h"
#include "filters.h"
#include "systeminfo.h"
#include "smol_cube.h"
#include <set>
#include <cmath>
#include <memory>
#include <string>

#define SOKOL_TIME_IMPL
#include "../libs/sokol_time.h"

constexpr int kRuns = 2;

struct FilterDesc
{
	const char* name;
	void (*filterFunc)(const uint8_t* src, uint8_t* dst, int channels, size_t dataElems);
	void (*unfilterFunc)(const uint8_t* src, uint8_t* dst, int channels, size_t dataElems);
};

static FilterDesc g_FilterSplit8 = {"-s8", Filter_Split, UnFilter_Split };
static FilterDesc g_FilterSplit8AndDeltaDiff = {"-s8dA", Filter_A, UnFilter_A }; // part 3 / part 6 beginning
static FilterDesc g_FilterSplit8Delta = { "-s8dD", Filter_D, UnFilter_D }; // part 6 end
static FilterDesc g_FilterSplit8DeltaOpt = { "-s8dH", Filter_H, UnFilter_H };

struct TestFile
{
	const char* path = nullptr;
	int channels = 1;
	size_t fileSize = 0;
	std::vector<float> fileData;
};

struct CompressorConfig
{
	CompressionFormat cmp;
	FilterDesc* filter;

	std::string GetName() const
	{
		std::string res = get_compression_name(cmp);
		if (filter != nullptr)
			res += filter->name;
		return res;
	}
	const char* GetShapeString() const
	{
		if (filter == &g_FilterSplit8DeltaOpt) return "'circle', pointSize: 4";
		if (filter == &g_FilterSplit8Delta) return "'circle'";
		if (filter == &g_FilterSplit8AndDeltaDiff) return "{type:'square', rotation: 45}, lineDashStyle: [4, 4]";
		if (filter == nullptr) return "'circle', lineDashStyle: [4, 2], pointSize: 4";
		return "'circle'";
	}
	uint32_t GetColor() const
	{
		// https://www.w3schools.com/colors/colors_picker.asp

		bool faded = true; // filter != &g_FilterSplit8DeltaOpt;
		if (cmp == kCompressionZstd)
		{
			return faded ? 0x90d596 : 0x0c9618; // green
		}
		return 0;
	}

	uint8_t* Compress(const TestFile& tf, int level, size_t& outCompressedSize)
	{
		const float* srcData = tf.fileData.data();
		uint8_t* filterBuffer = nullptr;
		if (filter)
		{
			filterBuffer = new uint8_t[sizeof(float) * tf.fileData.size()];
			filter->filterFunc((const uint8_t*)srcData, filterBuffer, tf.channels * sizeof(float), tf.fileData.size() / tf.channels);
			srcData = (const float*)filterBuffer;
		}

		outCompressedSize = 0;
		uint8_t* compressed = compress_data(srcData, tf.fileData.size() / tf.channels, tf.channels * sizeof(float), cmp, level, outCompressedSize);
		delete[] filterBuffer;
		return compressed;
	}

	void Decompress(const TestFile& tf, const uint8_t* compressed, size_t compressedSize, float* dst)
	{
		uint8_t* filterBuffer = nullptr;
		if (filter)
			filterBuffer = new uint8_t[sizeof(float) * tf.fileData.size()];
		decompress_data(compressed, compressedSize, filter == nullptr ? dst : (float*)filterBuffer, tf.fileData.size() / tf.channels, tf.channels * sizeof(float), cmp);

		if (filter)
		{
			filter->unfilterFunc(filterBuffer, (uint8_t*)dst, tf.channels * sizeof(float), tf.fileData.size() / tf.channels);
			delete[] filterBuffer;
		}
	}
};

static std::vector<CompressorConfig> g_Compressors;

static void TestCompressors(size_t testFileCount, TestFile* testFiles)
{
	g_Compressors.push_back({kCompressionZstd, &g_FilterSplit8DeltaOpt});
	g_Compressors.push_back({kCompressionZstd, &g_FilterSplit8Delta});
	g_Compressors.push_back({kCompressionZstd, &g_FilterSplit8AndDeltaDiff});
    g_Compressors.push_back({kCompressionZstd, &g_FilterSplit8});
	g_Compressors.push_back({kCompressionZstd, nullptr});


	size_t maxFloats = 0, totalFloats = 0, totalRawFileSize = 0;
	for (int tfi = 0; tfi < testFileCount; ++tfi)
	{
		size_t floats = testFiles[tfi].fileData.size();
		maxFloats = std::max(maxFloats, floats);
		totalFloats += floats;
		totalRawFileSize += testFiles[tfi].fileSize;
	}

	std::vector<float> decompressed(maxFloats);

	struct Result
	{
		int level = 0;
		size_t size = 0;
		double cmpTime = 0;
		double decTime = 0;
	};
	typedef std::vector<Result> LevelResults;
	std::vector<LevelResults> results;
	for (auto& cmp : g_Compressors)
	{
		auto levels = get_compressor_levels(cmp.cmp);
		LevelResults res(levels.size());
		for (size_t i = 0; i < levels.size(); ++i)
			res[i].level = levels[i];
		results.emplace_back(res);
	}

	std::string cmpName;
	for (int ir = 0; ir < kRuns; ++ir)
	{
		printf("Run %i/%i, %zi compressors on %zi files:\n", ir+1, kRuns, g_Compressors.size(), testFileCount);
		for (size_t ic = 0; ic < g_Compressors.size(); ++ic)
		{
			auto& config = g_Compressors[ic];
			cmpName = config.GetName();
			LevelResults& levelRes = results[ic];
			printf("%s: %zi levels:\n", cmpName.c_str(), levelRes.size());
			for (Result& res : levelRes)
			{
				printf(".");
				for (int tfi = 0; tfi < testFileCount; ++tfi)
				{
					const TestFile& tf = testFiles[tfi];

					const float* srcData = tf.fileData.data();
					SysInfoFlushCaches();

					// compress
					uint64_t t0 = stm_now();
					size_t compressedSize = 0;
					uint8_t* compressed = config.Compress(tf, res.level, compressedSize);
					double tComp = stm_sec(stm_since(t0));

					// decompress
					memset(decompressed.data(), 0, 4 * tf.fileData.size());
					SysInfoFlushCaches();
					t0 = stm_now();
					config.Decompress(tf, compressed, compressedSize, decompressed.data());
					double tDecomp = stm_sec(stm_since(t0));

					// stats
					res.size += compressedSize;
					res.cmpTime += tComp;
					res.decTime += tDecomp;

					// check validity
					if (memcmp(tf.fileData.data(), decompressed.data(), 4 * tf.fileData.size()) != 0)
					{
						printf("  ERROR, %s level %i did not decompress back to input on %s\n", cmpName.c_str(), res.level, tf.path);
						for (size_t i = 0; i < 4 * tf.fileData.size(); ++i)
						{
							float va = tf.fileData[i];
							float vb = decompressed[i];
							uint32_t ia = ((const uint32_t*)tf.fileData.data())[i];
							uint32_t ib = ((const uint32_t*)decompressed.data())[i];
							if (va != vb)
							{
								printf("    diff at #%zi: exp %f got %f (%08x %08x)\n", i, va, vb, ia, ib);
								break;
							}
						}
						exit(1);
					}
					delete[] compressed;
				}
			}
			printf("\n");
		}
		printf("\n");
	}

	// normalize results, cache the ones we ran, produce compressor versions
	int counterRan = 0;
	for (size_t ic = 0; ic < g_Compressors.size(); ++ic)
	{
		cmpName = get_compression_name(g_Compressors[ic].cmp);
		LevelResults& levelRes = results[ic];
		for (Result& res : levelRes)
		{
			res.size /= kRuns;
			res.cmpTime /= kRuns;
			res.decTime /= kRuns;
			++counterRan;
		}
	}
	printf("  Ran %i cases\n", counterRan);


	double oneMB = 1024.0 * 1024.0;
	double oneGB = oneMB * 1024.0;
	double rawMemSize = (double)(totalFloats * 4);
    double rawFileSize = double(totalRawFileSize);

	// print to HTML report page
	FILE* fout = fopen("../../report.html", "wb");
	fprintf(fout, "<script type='text/javascript' src='https://www.gstatic.com/charts/loader.js'></script>\n");
	fprintf(fout, "<center style='font-family: Arial;'>\n");
	fprintf(fout, "<div style='border: 1px solid #ccc; width: 1290px;'>\n");
	fprintf(fout, "<div id='chart_cmp' style='width: 640px; height: 480px; display:inline-block;'></div>\n");
	fprintf(fout, "<div id='chart_dec' style='width: 640px; height: 480px; display:inline-block;'></div>\n");
	fprintf(fout, "</div>\n");
	fprintf(fout, "</center>");
	fprintf(fout, "<script type='text/javascript'>\n");
	fprintf(fout, "google.charts.load('current', {'packages':['corechart']});\n");
	fprintf(fout, "google.charts.setOnLoadCallback(drawChart);\n");
	fprintf(fout, "function drawChart() {\n");
	fprintf(fout, "var dataCmp = new google.visualization.DataTable();\n");
	fprintf(fout, "var dataDec = new google.visualization.DataTable();\n");
	fprintf(fout, "dataCmp.addColumn('number', 'Throughput');\n");
	fprintf(fout, "dataDec.addColumn('number', 'Throughput');\n");
	for (auto& cmp : g_Compressors)
	{
		cmpName = cmp.GetName();
		fprintf(fout, "dataCmp.addColumn('number', '%s'); dataCmp.addColumn({type:'string', role:'tooltip'}); dataCmp.addColumn({type:'string', role:'style'});\n", cmpName.c_str());
		fprintf(fout, "dataDec.addColumn('number', '%s'); dataDec.addColumn({type:'string', role:'tooltip'}); dataDec.addColumn({type:'string', role:'style'});\n", cmpName.c_str());
	}
	fprintf(fout, "dataCmp.addRows([\n");
    
    double maxRatio = 0.0f;
	for (size_t ic = 0; ic < g_Compressors.size(); ++ic)
	{
		cmpName = g_Compressors[ic].GetName();
		const LevelResults& levelRes = results[ic];
		for (const Result& res : levelRes)
		{
			double csize = (double)res.size;
			double ctime = res.cmpTime;
			//double dtime = res.decTime;
			double ratio = rawFileSize / csize;
            maxRatio = std::max(ratio, maxRatio);
			double cspeed = rawMemSize / ctime;
			//double dspeed = rawSize / dtime;
			fprintf(fout, "  [%.3f", cspeed / oneGB);
			for (size_t j = 0; j < ic; ++j) fprintf(fout, ",null,null,null");
			fprintf(fout, ", %.3f,'%s", ratio, cmpName.c_str());
			if (levelRes.size() > 1)
				fprintf(fout, " %i", res.level);
			//if (strcmp(cmpName, "zstd-tst") == 0 && res.level == 1) // TEST TEST TEST
			//	printf("%s_%i ratio: %.3f\n", cmpName, res.level, ratio);
			fprintf(fout, "\\n%.3fx at %.3f GB/s\\n%.2FMB %.3fs','' ", ratio, cspeed / oneGB, csize / oneMB, ctime);
			for (size_t j = ic + 1; j < g_Compressors.size(); ++j) fprintf(fout, ",null,null,null");
			fprintf(fout, "]%s\n", (ic == g_Compressors.size() - 1) && (&res == &levelRes.back()) ? "" : ",");
		}
	}
	fprintf(fout, "]);\n");
	fprintf(fout, "dataDec.addRows([\n");
	for (size_t ic = 0; ic < g_Compressors.size(); ++ic)
	{
		cmpName = g_Compressors[ic].GetName();
		const LevelResults& levelRes = results[ic];
		for (const Result& res : levelRes)
		{
			double csize = (double)res.size;
			//double ctime = res.cmpTime;
			double dtime = res.decTime;
			double ratio = rawFileSize / csize;
            maxRatio = std::max(ratio, maxRatio);
			//double cspeed = rawSize / ctime;
			double dspeed = rawMemSize / dtime;
			fprintf(fout, "  [%.3f", dspeed / oneGB);
			for (size_t j = 0; j < ic; ++j) fprintf(fout, ",null,null,null");
			fprintf(fout, ", %.3f,'%s", ratio, cmpName.c_str());
			if (levelRes.size() > 1)
				fprintf(fout, " %i", res.level);
			fprintf(fout, "\\n%.3fx at %.3f GB/s\\n%.2FMB %.3fs','' ", ratio, dspeed / oneGB, csize / oneMB, dtime);
			for (size_t j = ic + 1; j < g_Compressors.size(); ++j) fprintf(fout, ",null,null,null");
			fprintf(fout, "]%s\n", (ic == g_Compressors.size() - 1) && (&res == &levelRes.back()) ? "" : ",");
		}
	}
	fprintf(fout, "]);\n");
	fprintf(fout, "var titleDec = 'Decompression Ratio vs Throughput';\n");
	fprintf(fout, "var options = {\n");
	fprintf(fout, "title: 'Compression Ratio vs Throughput',\n");
	fprintf(fout, "pointSize: 6,\n");
	fprintf(fout, "series: {\n");
	for (size_t ic = 0; ic < g_Compressors.size(); ++ic)
	{
		fprintf(fout, "  %zi: {pointShape: %s},\n", ic, g_Compressors[ic].GetShapeString());
	}
	fprintf(fout, "  %zi: {},\n", g_Compressors.size());
	fprintf(fout, "},\n");
	fprintf(fout, "colors: [");
	for (size_t ic = 0; ic < g_Compressors.size(); ++ic)
	{
		uint32_t col = g_Compressors[ic].GetColor();
		fprintf(fout, "'%02x%02x%02x'%s", (col >> 16)&0xFF, (col >> 8)&0xFF, col&0xFF, ic== g_Compressors.size()-1?"":",");
	}
	fprintf(fout, "],\n");
	fprintf(fout, "hAxis: {title: 'Compression GB/s', logScale: true, viewWindow: {min:0.02, max:6.0}},\n");
	fprintf(fout, "vAxis: {title: 'Ratio', viewWindow: {min:1.0, max:%.1f}},\n", std::ceil(maxRatio));
	fprintf(fout, "chartArea: {left:60, right:10, top:50, bottom:50},\n");
	fprintf(fout, "legend: {position: 'top'},\n");
	fprintf(fout, "lineWidth: 1\n");
	fprintf(fout, "};\n");
	fprintf(fout, "var chartCmp = new google.visualization.ScatterChart(document.getElementById('chart_cmp'));\n");
	fprintf(fout, "chartCmp.draw(dataCmp, options);\n");
	fprintf(fout, "options.title = titleDec;\n");
	fprintf(fout, "options.hAxis.title = 'Decompression GB/s';\n");
	fprintf(fout, "options.hAxis.viewWindow.min = 0.5;\n");
	fprintf(fout, "options.hAxis.viewWindow.max = 32.0;\n");
	fprintf(fout, "var chartDec = new google.visualization.ScatterChart(document.getElementById('chart_dec'));\n");
	fprintf(fout, "chartDec.draw(dataDec, options);\n");
	fprintf(fout, "}\n");
	fprintf(fout, "</script>\n");
	fclose(fout);

	// cleanup
	g_Compressors.clear();
}

static size_t GetFileSize(const char* path)
{
	FILE* f = fopen(path, "rb");
	if (f == nullptr)
		return 0;
	fseek(f, 0, SEEK_END);
	size_t size = ftell(f);
	fclose(f);
	return size;
}

static bool ReadCubeTestFile(TestFile& tf)
{
	smol_cube_lut* lut_3d;
	smol_cube_lut* lut_1d;
	smol_cube_result res = smol_cube_parse_cube_file(tf.path, lut_3d, lut_1d);
	if (res != smol_cube_result::Ok)
		return false;

	//@TODO: combined 3d + 1d?
	if (lut_3d)
	{
		tf.channels = 3;
		tf.fileData.resize(lut_3d->size_x * lut_3d->size_y * lut_3d->size_z * tf.channels);
		memcpy(tf.fileData.data(), lut_3d->data, tf.fileData.size() * sizeof(tf.fileData[0]));
		delete[] lut_3d->data;
		delete lut_3d;
		return true;
	}

	if (lut_1d)
	{
		tf.channels = 3;
		tf.fileData.resize(lut_1d->size_x * tf.channels);
		memcpy(tf.fileData.data(), lut_1d->data, tf.fileData.size() * sizeof(tf.fileData[0]));
		delete[] lut_1d->data;
		delete lut_1d;
		return true;
	}

	return false;
}

int main()
{
	stm_setup();

	TestFile testFiles[] = {
#if 1
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
#endif
	};
	for (auto& tf : testFiles)
	{
		tf.fileSize = GetFileSize(tf.path);
		if (tf.fileSize == 0)
		{
			printf("ERROR: failed to open data file %s\n", tf.path);
			return 1;
		}
        if (!ReadCubeTestFile(tf))
        {
            printf("ERROR: failed to read cube file %s\n", tf.path);
            return 1;
        }
	}

	TestCompressors(std::size(testFiles), testFiles);

	return 0;
}
