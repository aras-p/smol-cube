#include "smol_cube.h"

#define SOKOL_IMPL
#if defined(__APPLE__)
#define SOKOL_METAL
#elif defined(_WIN32)
#define SOKOL_D3D11
#else
#error "Unsupported Sokol build platform"
#endif
#include "../libs/sokol/sokol_app.h"
#include "../libs/sokol/sokol_gfx.h"
#include "../libs/sokol/sokol_log.h"
#include "../libs/sokol/sokol_time.h"
#include "../libs/sokol/sokol_glue.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#include "../libs/stb_image.h"

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
#endif

static const char* kSokolFragSource =
#if defined(SOKOL_METAL) || defined(SOKOL_D3D11)
// HLSL / Metal
#ifdef SOKOL_METAL
"#include <metal_stdlib>\n"
"using namespace metal;\n"
"struct v2f { float2 uv; };\n"
"fragment float4 fs_main(\n"
"v2f i [[stage_in]],\n"
"sampler smp [[sampler(0)]],\n"
"texture2d<float> tex0 [[texture(0)]],\n"
"texture2d<float> tex1 [[texture(1)]],\n"
"texture2d<float> tex2 [[texture(2)]],\n"
"texture2d<float> tex3 [[texture(3)]])\n"
#else
// Sokol does not support sRGB swapchains currently, so do linear->srgb manually
"float to_srgb(float x) { if (x <= 0.00031308) return 12.92 * x; else return 1.055 * pow(x, (1.0 / 2.4)) - 0.055; }\n"
"struct v2f { float2 uv : TEXCOORD0; };\n"
"sampler smp : register(s0);\n"
"Texture2D<float4> tex0 : register(t0);\n"
"Texture2D<float4> tex1 : register(t1);\n"
"Texture2D<float4> tex2 : register(t2);\n"
"Texture2D<float4> tex3 : register(t3);\n"
"float4 fs_main(v2f i) : SV_Target0\n"
#endif
"{\n"
"  float3 col1 = tex0.Sample(smp, i.uv * 2.0 - float2(0,0)).rgb;\n"
"  float3 col2 = tex1.Sample(smp, i.uv * 2.0 - float2(1,0)).rgb;\n"
"  float3 col3 = tex2.Sample(smp, i.uv * 2.0 - float2(0,1)).rgb;\n"
"  float3 col4 = tex3.Sample(smp, i.uv * 2.0 - float2(1,1)).rgb;\n"
"  float3 col12 = i.uv.x < 0.5 ? col1 : col2;\n"
"  float3 col34 = i.uv.x < 0.5 ? col3 : col4;\n"
"  float4 col = 0;\n"
"  col.rgb = i.uv.y < 0.5 ? col12 : col34;\n"
"  col.r = to_srgb(col.r);\n"
"  col.g = to_srgb(col.g);\n"
"  col.b = to_srgb(col.b);\n"
"  return col;\n"
"}\n";
#endif

static sg_image load_image(const char* path)
{
	int width, height, comps;
	stbi_uc* data = stbi_load(path, &width, &height, &comps, 4);
	if (data == nullptr)
		return sg_image{};

	sg_image_desc desc = {};
	desc.width = width;
	desc.height = height;
	desc.pixel_format = SG_PIXELFORMAT_SRGB8A8;
	desc.usage = SG_USAGE_IMMUTABLE;
	desc.data.subimage[0][0].ptr = data;
	desc.data.subimage[0][0].size = width * height * 4;
	sg_image tex = sg_make_image(&desc);
	stbi_image_free(data);
	return tex;
}

static sg_image gr_tex_photo1, gr_tex_photo2, gr_tex_photo3, gr_tex_photo4;
static sg_sampler gr_sampler;
static sg_shader gr_shader;
static sg_pipeline gr_pipe;

static void sapp_init(void)
{
	// graphics
	sg_desc setup_desc = {};
	setup_desc.environment = sglue_environment();
	setup_desc.logger.func = slog_func;
	sg_setup(&setup_desc);

	// load images
	gr_tex_photo1 = load_image("tests/photo1.jpg");
	gr_tex_photo2 = load_image("tests/photo2.jpg");
	gr_tex_photo3 = load_image("tests/photo3.jpg");
	gr_tex_photo4 = load_image("tests/photo4.jpg");

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
	sh_desc.fs.images[0].used = true;
	sh_desc.fs.images[1].used = true;
	sh_desc.fs.images[2].used = true;
	sh_desc.fs.images[3].used = true;
	sh_desc.fs.samplers[0].used = true;
	sh_desc.fs.image_sampler_pairs[0] = { true, 0, 0, "tex0" };
	sh_desc.fs.image_sampler_pairs[1] = { true, 1, 0, "tex1" };
	sh_desc.fs.image_sampler_pairs[2] = { true, 2, 0, "tex2" };
	sh_desc.fs.image_sampler_pairs[3] = { true, 3, 0, "tex3" };
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
	bind.fs.samplers[0] = gr_sampler;
	sg_apply_pipeline(gr_pipe);
	sg_apply_bindings(&bind);
	sg_draw(0, 3, 1);
	sg_end_pass();
	sg_commit();
}

static void sapp_cleanup(void)
{
	sg_shutdown();
}

static void sapp_onevent(const sapp_event* evt)
{
	if (evt->type == SAPP_EVENTTYPE_KEY_DOWN)
	{
		if (evt->key_code == SAPP_KEYCODE_ESCAPE)
		{
			sapp_quit();
		}
	}
}

sapp_desc sokol_main(int argc, char* argv[])
{
	(void)argc; (void)argv;

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
