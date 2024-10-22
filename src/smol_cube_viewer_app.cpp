// SPDX-License-Identifier: MIT OR Unlicense
// smol-cube: https://github.com/aras-p/smol-cube

#include "smol_cube.h"
#include <string>
#include <vector>
#include <filesystem>
#include <algorithm>

#define SOKOL_IMPL
#if defined(__APPLE__)
#define SOKOL_METAL
#elif defined(_WIN32)
#define SOKOL_D3D11
#else
#define SOKOL_GLCORE
#endif
#include "../libs/sokol/sokol_app.h"
#include "../libs/sokol/sokol_gfx.h"
#include "../libs/sokol/sokol_log.h"
#include "../libs/sokol/sokol_time.h"
#include "../libs/sokol/sokol_glue.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#include "../libs/stb_image.h"

static std::vector<std::string> s_lut_files;
static int s_lut_index = 0;

static std::vector<std::string> find_lut_files(const std::string& directory, const std::string& extension1, const std::string& extension2)
{
	std::vector<std::string> files;
	for (const auto& entry : std::filesystem::directory_iterator(directory))
	{
		if (!entry.is_regular_file())
			continue;
		auto entry_ext = entry.path().extension();
		if (entry_ext == extension1 || entry_ext == extension2)
		{
			files.push_back(entry.path().string());
		}
	}
	std::sort(files.begin(), files.end(), [](const std::string& a, const std::string& b) { return a < b; });
	return files;
}

static const char* kSokolVertexSource =
#if defined(SOKOL_METAL) || defined(SOKOL_D3D11)
// HLSL / Metal
#ifdef SOKOL_METAL
"#include <metal_stdlib>\n"
"using namespace metal;\n"
"struct v2f { float2 uv; float4 pos [[position]]; };\n"
"vertex v2f vs_main(uint vidx [[vertex_id]]) {\n"
#else
"struct v2f { float2 uv : TEXCOORD0; float4 pos : SV_Position; };\n"
"v2f vs_main(uint vidx: SV_VertexID) {\n"
#endif
"    v2f o;\n"
"    float2 uv = float2((vidx << 1) & 2, vidx & 2);\n"
"    o.uv = uv;\n"
"    o.pos = float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);\n"
"    return o;\n"
"}\n";
#elif defined(SOKOL_GLCORE)
"#version 410\n"
"out vec2 uv;\n"
"void main() {\n"
"  uv = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);\n"
"  gl_Position = vec4(uv * vec2(2, -2) + vec2(-1, 1), 0, 1);\n"
"}";
#endif

static const char* kSokolFragSource =
#if defined(SOKOL_METAL) || defined(SOKOL_D3D11)
// HLSL / Metal
#ifdef SOKOL_METAL
"#include <metal_stdlib>\n"
"using namespace metal;\n"
"#define Sample sample\n"
"#define lerp mix\n"
"struct frame_uni { float2 lut_intensity_size; };\n"
"struct v2f { float2 uv; };\n"
"fragment float4 fs_main(\n"
"v2f i [[stage_in]], constant frame_uni& uni [[buffer(0)]],\n"
"sampler smp [[sampler(0)]],\n"
"texture2d<float> tex0 [[texture(0)]],\n"
"texture2d<float> tex1 [[texture(1)]],\n"
"texture2d<float> tex2 [[texture(2)]],\n"
"texture2d<float> tex3 [[texture(3)]],\n"
"texture3d<float> texlut [[texture(4)]])\n"
"{\n"
"  float intensity = uni.lut_intensity_size.x;\n"
"  float lutsize = uni.lut_intensity_size.y;\n"
#else
"cbuffer uni : register(b0) { float2 lut_intensity_size; };\n"
"struct v2f { float2 uv : TEXCOORD0; };\n"
"sampler smp : register(s0);\n"
"Texture2D<float4> tex0 : register(t0);\n"
"Texture2D<float4> tex1 : register(t1);\n"
"Texture2D<float4> tex2 : register(t2);\n"
"Texture2D<float4> tex3 : register(t3);\n"
"Texture3D<float4> texlut : register(t4);\n"
"float4 fs_main(v2f i) : SV_Target0\n"
"{\n"
"  float intensity = lut_intensity_size.x;\n"
"  float lutsize = lut_intensity_size.y;\n"
#endif
"  float3 col1 = tex0.Sample(smp, i.uv * 2.0 - float2(0,0)).rgb;\n"
"  float3 col2 = tex1.Sample(smp, i.uv * 2.0 - float2(1,0)).rgb;\n"
"  float3 col3 = tex2.Sample(smp, i.uv * 2.0 - float2(0,1)).rgb;\n"
"  float3 col4 = tex3.Sample(smp, i.uv * 2.0 - float2(1,1)).rgb;\n"
"  float3 col12 = i.uv.x < 0.5 ? col1 : col2;\n"
"  float3 col34 = i.uv.x < 0.5 ? col3 : col4;\n"
"  float3 col = 0;\n"
"  col = i.uv.y < 0.5 ? col12 : col34;\n"
"  float3 lutuv = col * ((lutsize-1) / lutsize) + 0.5/lutsize;\n"
"  float3 cc_col = texlut.Sample(smp, lutuv).rgb;\n"
"  col = lerp(col, cc_col, intensity);\n"
"  return float4(col, 1.0);\n"
"}\n";
#elif defined(SOKOL_GLCORE)
"#version 410\n"
"uniform vec2 lut_intensity_size;\n"
"in vec2 uv;\n"
"out vec4 frag_color;\n"
"uniform sampler2D tex0;\n"
"uniform sampler2D tex1;\n"
"uniform sampler2D tex2;\n"
"uniform sampler2D tex3;\n"
"uniform sampler3D texlut;\n"
"void main()\n"
"{\n"
"  float intensity = lut_intensity_size.x;\n"
"  float lutsize = lut_intensity_size.y;\n"
"  vec3 col1 = texture(tex0, uv * 2.0 - vec2(0,0)).rgb;\n"
"  vec3 col2 = texture(tex1, uv * 2.0 - vec2(1,0)).rgb;\n"
"  vec3 col3 = texture(tex2, uv * 2.0 - vec2(0,1)).rgb;\n"
"  vec3 col4 = texture(tex3, uv * 2.0 - vec2(1,1)).rgb;\n"
"  vec3 col12 = uv.x < 0.5 ? col1 : col2;\n"
"  vec3 col34 = uv.x < 0.5 ? col3 : col4;\n"
"  vec3 col = vec3(0);\n"
"  col = uv.y < 0.5 ? col12 : col34;\n"
"  vec3 lutuv = col * ((lutsize-1) / lutsize) + 0.5/lutsize;\n"
"  vec3 cc_col = texture(texlut, lutuv).rgb;\n"
"  col = mix(col, cc_col, intensity);\n"
"  frag_color = vec4(col, 1.0);\n"
"}\n";
#endif

static const char* kBaseWindowTitle = "L/R: change LUT, U/D: change intensity";
static std::string s_cur_lut_title;
static int s_cur_lut_size = 0;
static float s_cur_lut_load_time = 0.0f;

static sg_image load_image(const char* path)
{
	int width, height, comps;
	stbi_uc* data = stbi_load(path, &width, &height, &comps, 4);
	if (data == nullptr)
		return sg_image{};

	sg_image_desc desc = {};
	desc.width = width;
	desc.height = height;
	desc.pixel_format = SG_PIXELFORMAT_RGBA8;
	desc.usage = SG_USAGE_IMMUTABLE;
	desc.data.subimage[0][0].ptr = data;
	desc.data.subimage[0][0].size = width * height * 4;
	sg_image tex = sg_make_image(&desc);
	stbi_image_free(data);
	return tex;
}

static sg_image create_empty_lut(float& lut_size)
{
	s_cur_lut_title = "<empty>";
	s_cur_lut_size = 0;
	s_cur_lut_load_time = 0.0f;
	sg_image tex = {};
	const int SIZE = 2;
	lut_size = SIZE;
	float* rgba = new float[SIZE * SIZE * SIZE * 4];
	size_t idx = 0;
	for (int iz = 0; iz < SIZE; ++iz) {
		for (int iy = 0; iy < SIZE; ++iy) {
			for (int ix = 0; ix < SIZE; ++ix) {
				rgba[idx * 4 + 0] = float(ix) / (SIZE-1);
				rgba[idx * 4 + 1] = float(iy) / (SIZE-1);
				rgba[idx * 4 + 2] = float(iz) / (SIZE-1);
				rgba[idx * 4 + 3] = 1.0f;
				++idx;
			}
		}
	}

	sg_image_desc desc = {};
	desc.type = SG_IMAGETYPE_3D;
	desc.width = SIZE;
	desc.height = SIZE;
	desc.num_slices = SIZE;
	desc.pixel_format = SG_PIXELFORMAT_RGBA32F;
	desc.usage = SG_USAGE_IMMUTABLE;
	desc.data.subimage[0][0].ptr = rgba;
	desc.data.subimage[0][0].size = SIZE * SIZE * SIZE * 4 * sizeof(float);
	tex = sg_make_image(&desc);
	delete[] rgba;
	return tex;
}

static sg_image load_lut(const char* path, float& lut_size)
{
	lut_size = 2;
	sg_image tex = {};

	uint64_t t0 = stm_now();
	smcube_luts* luts = smcube_load_from_file(path);
	if (luts == nullptr)
	{
		slog_func("smol-cube", 1, 0, "Failed to load LUT file", 1, path, nullptr);
		return create_empty_lut(lut_size);
	}

	for (size_t li = 0, ln = smcube_get_count(luts); li != ln; ++li)
	{
		const int dim = smcube_lut_get_dimension(luts, li);
		const int channels = smcube_lut_get_channels(luts, li);
		smcube_data_type data_type = smcube_lut_get_data_type(luts, li);
		const int sizex = smcube_lut_get_size_x(luts, li);
		const int sizey = smcube_lut_get_size_y(luts, li);
		const int sizez = smcube_lut_get_size_z(luts, li);
		const void* in_data = smcube_lut_get_data(luts, li);

		if (dim != 3)
			continue;
		if (channels != 3 && channels != 4)
			continue;
		if (data_type != smcube_data_type::Float32 && data_type != smcube_data_type::Float16)
			continue;

		sg_image_desc desc = {};
		desc.type = SG_IMAGETYPE_3D;
		desc.width = sizex;
		desc.height = sizey;
		desc.num_slices = sizez;
		desc.pixel_format = data_type == smcube_data_type::Float32 ? SG_PIXELFORMAT_RGBA32F : SG_PIXELFORMAT_RGBA16F;
		desc.usage = SG_USAGE_IMMUTABLE;

		float* rgba32 = nullptr;
		uint16_t* rgba16 = nullptr;
		if (data_type == smcube_data_type::Float32)
		{
			desc.pixel_format = SG_PIXELFORMAT_RGBA32F;
			desc.data.subimage[0][0].size = sizex * sizey * sizez * 4 * sizeof(float);

			if (channels == 4)
			{
				desc.data.subimage[0][0].ptr = in_data;
			}
			else
			{
				const float* src_rgb = (const float*)in_data;
				rgba32 = new float[sizex * sizey * sizez * 4];
				size_t idx = 0;
				for (int iz = 0; iz < sizez; ++iz) {
					for (int iy = 0; iy < sizey; ++iy) {
						for (int ix = 0; ix < sizex; ++ix) {
							rgba32[idx * 4 + 0] = src_rgb[idx * 3 + 0];
							rgba32[idx * 4 + 1] = src_rgb[idx * 3 + 1];
							rgba32[idx * 4 + 2] = src_rgb[idx * 3 + 2];
							rgba32[idx * 4 + 3] = 1.0f;
							++idx;
						}
					}
				}
				desc.data.subimage[0][0].ptr = rgba32;
			}
		}
		else if (data_type == smcube_data_type::Float16)
		{
			desc.pixel_format = SG_PIXELFORMAT_RGBA16F;
			desc.data.subimage[0][0].size = sizex * sizey * sizez * 4 * sizeof(uint16_t);
			if (channels == 4)
			{
				desc.data.subimage[0][0].ptr = in_data;
			}
			else
			{
				const uint16_t* src_rgb = (const uint16_t*)in_data;
				rgba16 = new uint16_t[sizex * sizey * sizez * 4];
				size_t idx = 0;
				for (int iz = 0; iz < sizez; ++iz) {
					for (int iy = 0; iy < sizey; ++iy) {
						for (int ix = 0; ix < sizex; ++ix) {
							rgba16[idx * 4 + 0] = src_rgb[idx * 3 + 0];
							rgba16[idx * 4 + 1] = src_rgb[idx * 3 + 1];
							rgba16[idx * 4 + 2] = src_rgb[idx * 3 + 2];
							rgba16[idx * 4 + 3] = 0x3c00; // 1.0 as FP16
							++idx;
						}
					}
				}
				desc.data.subimage[0][0].ptr = rgba16;
			}
		}

		tex = sg_make_image(&desc);
		lut_size = float(sizex);
		delete[] rgba32;
		delete[] rgba16;

		uint64_t t1 = stm_now();
		s_cur_lut_load_time = float(stm_ms(stm_diff(t1, t0)));
		s_cur_lut_size = sizex;

		s_cur_lut_title = path;
		size_t fnamepos = s_cur_lut_title.find_last_of("/\\");
		if (fnamepos != std::string::npos)
			s_cur_lut_title = s_cur_lut_title.substr(fnamepos + 1);
		break;
	}
	smcube_free(luts);

	return tex;
}

static sg_image gr_tex_photo1, gr_tex_photo2, gr_tex_photo3, gr_tex_photo4;
static sg_image gr_tex_lut;
static sg_sampler gr_sampler;
static sg_shader gr_shader;
static sg_pipeline gr_pipe;

struct frame_uniforms {
	float lut_intensity;
	float lut_size;
};
static frame_uniforms gr_uniforms = { 1.0f, 4.0f };

static bool s_do_quit = false;

static void sapp_init(void)
{
	// graphics
	sg_desc setup_desc = {};
	setup_desc.environment = sglue_environment();
	setup_desc.logger.func = slog_func;
	sg_setup(&setup_desc);

	s_lut_files = find_lut_files("./tests/luts", ".cube", ".smcube");
	if (s_lut_files.empty())
	{
		s_do_quit = true;
		return;
	}

	// load images
	gr_tex_photo1 = load_image("tests/photo1.jpg");
	gr_tex_photo2 = load_image("tests/photo2.jpg");
	gr_tex_photo3 = load_image("tests/photo3.jpg");
	gr_tex_photo4 = load_image("tests/photo4.jpg");

	// load LUT
	s_lut_index = int(s_lut_files.size()) - 1;
	gr_tex_lut = load_lut(s_lut_files[s_lut_index].c_str(), gr_uniforms.lut_size);

	// sampler
	sg_sampler_desc smp_desc = {};
	smp_desc.min_filter = SG_FILTER_LINEAR;
	smp_desc.mag_filter = SG_FILTER_LINEAR;
	smp_desc.wrap_u = smp_desc.wrap_v = smp_desc.wrap_w = SG_WRAP_CLAMP_TO_EDGE;
	gr_sampler = sg_make_sampler(&smp_desc);

	// shader and pipeline
	sg_shader_desc sh_desc = {};
	sh_desc.vs.source = kSokolVertexSource;
	sh_desc.vs.entry = "vs_main";
	sh_desc.fs.source = kSokolFragSource;
	sh_desc.fs.entry = "fs_main";
	sh_desc.fs.uniform_blocks[0].size = sizeof(frame_uniforms);
	sh_desc.fs.uniform_blocks[0].uniforms[0].name = "lut_intensity_size";
	sh_desc.fs.uniform_blocks[0].uniforms[0].type = SG_UNIFORMTYPE_FLOAT2;
	sh_desc.fs.images[0].used = true;
	sh_desc.fs.images[1].used = true;
	sh_desc.fs.images[2].used = true;
	sh_desc.fs.images[3].used = true;
	sh_desc.fs.images[4].used = true;
	sh_desc.fs.images[4].image_type = SG_IMAGETYPE_3D;
	sh_desc.fs.samplers[0].used = true;
	sh_desc.fs.image_sampler_pairs[0] = { true, 0, 0, "tex0" };
	sh_desc.fs.image_sampler_pairs[1] = { true, 1, 0, "tex1" };
	sh_desc.fs.image_sampler_pairs[2] = { true, 2, 0, "tex2" };
	sh_desc.fs.image_sampler_pairs[3] = { true, 3, 0, "tex3" };
	sh_desc.fs.image_sampler_pairs[4] = { true, 4, 0, "texlut" };
	gr_shader = sg_make_shader(&sh_desc);

	sg_pipeline_desc p_desc = {};
	p_desc.shader = gr_shader;
	p_desc.depth.compare = SG_COMPAREFUNC_ALWAYS;
	p_desc.depth.write_enabled = false;
	p_desc.index_type = SG_INDEXTYPE_NONE;
	p_desc.cull_mode = SG_CULLMODE_NONE;
	gr_pipe = sg_make_pipeline(&p_desc);
}

static void sapp_frame(void)
{
	if (s_do_quit)
	{
		sapp_quit();
		return;
	}
	char buf[1000];
	snprintf(buf, sizeof(buf), "%s. LUT %i/%i: %s size %i load %.2fms (%i%%)", kBaseWindowTitle, s_lut_index+1, int(s_lut_files.size()), s_cur_lut_title.c_str(), s_cur_lut_size, s_cur_lut_load_time, (int)(gr_uniforms.lut_intensity * 100.0f));
	sapp_set_window_title(buf);

	sg_pass pass = {};
	pass.action.colors[0].load_action = SG_LOADACTION_CLEAR;
	pass.action.colors[0].clear_value = { 0.5f, 0.5f, 0.5f, 1.0f };
	pass.swapchain = sglue_swapchain();
	sg_begin_pass(&pass);

	sg_bindings bind = {};
	bind.fs.images[0] = gr_tex_photo1;
	bind.fs.images[1] = gr_tex_photo2;
	bind.fs.images[2] = gr_tex_photo3;
	bind.fs.images[3] = gr_tex_photo4;
	bind.fs.images[4] = gr_tex_lut;
	bind.fs.samplers[0] = gr_sampler;
	sg_apply_pipeline(gr_pipe);
	sg_apply_bindings(&bind);
	sg_range uni_range{&gr_uniforms, sizeof(gr_uniforms)};
	sg_apply_uniforms(SG_SHADERSTAGE_FS, 0, &uni_range);
	sg_draw(0, 3, 1);
	sg_end_pass();
	sg_commit();
}

static void sapp_cleanup(void)
{
	sg_shutdown();
}

static void reload_lut()
{
	sg_destroy_image(gr_tex_lut);
	if (s_lut_index == -1)
		gr_tex_lut = create_empty_lut(gr_uniforms.lut_size);
	else
		gr_tex_lut = load_lut(s_lut_files[s_lut_index].c_str(), gr_uniforms.lut_size);
}

static void sapp_onevent(const sapp_event* evt)
{
	if (evt->type == SAPP_EVENTTYPE_KEY_DOWN)
	{
		if (evt->key_code == SAPP_KEYCODE_ESCAPE)
		{
			sapp_quit();
		}
		if (evt->key_code == SAPP_KEYCODE_UP)
		{
			gr_uniforms.lut_intensity += 0.2f;
			if (gr_uniforms.lut_intensity > 1.0f)
				gr_uniforms.lut_intensity = 1.0f;
		}
		if (evt->key_code == SAPP_KEYCODE_DOWN)
		{
			gr_uniforms.lut_intensity -= 0.2f;
			if (gr_uniforms.lut_intensity < 0.0f)
				gr_uniforms.lut_intensity = 0.0f;
		}
		if (evt->key_code == SAPP_KEYCODE_LEFT)
		{
			s_lut_index--;
			if (s_lut_index < -1)
				s_lut_index = -1;
			reload_lut();
		}
		if (evt->key_code == SAPP_KEYCODE_RIGHT)
		{
			s_lut_index++;
			if (s_lut_index >= s_lut_files.size())
				s_lut_index = int(s_lut_files.size())-1;
			reload_lut();
		}
		if (evt->key_code == SAPP_KEYCODE_SPACE)
		{
			reload_lut();
		}
		if (evt->key_code == SAPP_KEYCODE_1)
		{
			s_lut_index = -1;
			reload_lut();
		}
	}
}

sapp_desc sokol_main(int argc, char* argv[])
{
	(void)argc; (void)argv;

	stm_setup();

	// figure out where is the data folder
#ifdef __APPLE__
	//snprintf(s_data_path, sizeof(s_data_path), "%s", [[NSBundle mainBundle].resourcePath UTF8String] );
#else
	//strncpy(s_data_path, "data", sizeof(s_data_path));
#endif

	sapp_desc res = {};
	res.init_cb = sapp_init;
	res.frame_cb = sapp_frame;
	res.cleanup_cb = sapp_cleanup;
	res.event_cb = sapp_onevent;
	res.width = 1024;
	res.height = 683;
	res.window_title = "smol-cube viewer app";
	res.icon.sokol_default = true;
	res.logger.func = slog_func;

	return res;
}

// PC, timings in ms:
//                                      our .cube  OCIO  smcube float3   half3 half4
// Bluecine_75.cube:			size 33      10.4  58.1
// Cold_Ice.cube:				size 16       1.3   7.3
// LUNA_COLOR.cube:				size 33      11.7  61.4            0.7     0.5   0.5
// Sam_Kolder.cube:				size 33       9.8  55.0
// pbrNeutral.cube:				size 57      54.7 302.5            4.5           2.5
// DCI-P3 Kodak 2383 D65.cube:	size 33       9.5  56.9
// LMT ACES v0.1.1.cube:		size 65     116.7 516.1            6.2     3.5   3.5

// OCIO: https://gist.github.com/aras-p/df0c7310e87daf471eb321291dda3761
// - does not support TITLE tag (throws exception)
// - fails parsing if DOMAIN_MIN or DOMAIN_MAX tags are present
