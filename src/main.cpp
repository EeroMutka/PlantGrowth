#define ENABLE_D3D11_DEBUG_MODE false

#pragma comment (lib, "gdi32")
#pragma comment (lib, "user32")
#pragma comment (lib, "dxguid")
#pragma comment (lib, "dxgi")
#pragma comment (lib, "d3d11")
#pragma comment (lib, "d3dcompiler")

#define _CRT_SECURE_NO_WARNINGS
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>

#define COBJMACROS
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_3.h>
#include <d3dcompiler.h>

#include "../Fire/fire_ds.h"

#define STR_USE_FIRE_DS_ARENA
#include "../Fire/fire_string.h"

#define FIRE_OS_WINDOW_IMPLEMENTATION
#include "../Fire/fire_os_window.h"

#define FIRE_OS_CLIPBOARD_IMPLEMENTATION
#include "../Fire/fire_os_clipboard.h"

#define STB_RECT_PACK_IMPLEMENTATION
#define STB_TRUETYPE_IMPLEMENTATION
#include "../Fire/fire_ui/stb_rect_pack.h"
#include "../Fire/fire_ui/stb_truetype.h"

#include "../Fire/fire_ui/fire_ui.h"
#include "../Fire/fire_ui/fire_ui_color_pickers.h"
#include "../Fire/fire_ui/fire_ui_extras.h"
#include "../Fire/fire_ui/fire_ui_backend_dx11.h"
#include "../Fire/fire_ui/fire_ui_backend_fire_os.h"

#include "third_party/HandmadeMath.h"
#include "utils/space_math.h"
#include "utils/basic_3d_renderer/basic_3d_renderer.h"
#include "utils/gizmos.h"
#include "utils/key_input/key_input.h"
#include "utils/key_input/key_input_fire_os.h"
#include "utils/fire_ui_backend_key_input.h"

#define CAMERA_VIEW_SPACE_IS_POSITIVE_Y_DOWN
#include "utils/camera.h"

#include "growth.h"

//// Globals ///////////////////////////////////////////////

static UI_Vec2 g_window_size = {1200, 900};
static OS_WINDOW g_window;

static ID3D11Device* g_device;
static ID3D11DeviceContext* g_device_context;
static IDXGISwapChain* g_swapchain;
static ID3D11RenderTargetView* g_framebuffer_rtv;

static UI_Font g_base_font, g_icons_font;

static Input_Frame g_inputs;
static DS_Arena g_persist;
static DS_Arena g_temp;

static Camera g_camera;

static PlantParameters g_plant_params;
static DS_Arena g_plant_arena;
static Plant g_plant;

////////////////////////////////////////////////////////////

static STR ReadEntireFile(DS_Arena* arena, const char* file) {
	FILE* f = fopen(file, "rb");
	assert(f);

	fseek(f, 0, SEEK_END);
	long fsize = ftell(f);
	fseek(f, 0, SEEK_SET);

	char* data = DS_ArenaPush(arena, fsize);
	fread(data, fsize, 1, f);

	fclose(f);
	STR result = {data, fsize};
	return result;
}

static void DebugDrawPlant(const GizmosViewport* vp, Plant* plant) {
	for (int i = 0; i < plant->stems.length; i++) {
		Stem* stem = &plant->stems.data[i];

		for (int j = 0; j < stem->points.length; j++) {
			StemPoint* stem_point = &stem->points.data[j];

			if (j > 0) {
				StemPoint* prev_stem_point = &stem->points.data[j - 1];
				DrawLine3D(vp, prev_stem_point->point, stem_point->point, 5.f, UI_BLUE);
			}
			DrawPoint3D(vp, stem_point->point, stem_point->thickness * 100.f, UI_BLUE);
		}
	}
}

static void UpdateAndRender() {
	DS_ArenaReset(&g_temp);

	Camera_Update(&g_camera, &g_inputs, 0.01f, 0.001f, 70.f, g_window_size.x / g_window_size.y, 0.01f, 1000.f);

	GizmosViewport vp = {0};
	vp.camera = g_camera.cached;
	vp.window_size = {g_window_size.x, g_window_size.y};
	vp.window_size_inv = {1.f/g_window_size.x, 1.f/g_window_size.y};

	UI_Inputs ui_inputs{};
	UI_Input_ApplyInputs(&ui_inputs, &g_inputs);
	ui_inputs.base_font = &g_base_font;
	ui_inputs.icons_font = &g_icons_font;
	OS_WINDOW_GetMousePosition(&g_window, &ui_inputs.mouse_position.x, &ui_inputs.mouse_position.y);

	UI_DX11_BeginFrame();
	UI_BeginFrame(&ui_inputs, g_window_size);

	UI_Box* root = UI_MakeRootBox(UI_KEY(), g_window_size.x, g_window_size.y, 0);
	UI_PushBox(root);
	UI_AddBoxWithText(UI_KEY(), UI_SizeFit(), UI_SizeFit(), 0, STR_("This will become a plant generator!"));

	UI_AddFmt(UI_KEY(), "Points per meter: %!f", &g_plant_params.points_per_meter);
	UI_AddFmt(UI_KEY(), "Age: %!f", &g_plant_params.age);
	UI_AddFmt(UI_KEY(), "Pitch twist: %!f", &g_plant_params.pitch_twist);
	UI_AddFmt(UI_KEY(), "Yaw twist: %!f", &g_plant_params.yaw_twist);
	UI_AddFmt(UI_KEY(), "Drop pitch: %!f", &g_plant_params.drop_pitch);

	UI_AddButton(UI_KEY(), UI_SizeFit(), UI_SizeFit(), 0, STR_("Hello!"));
	UI_PopBox(root);

	DrawGrid3D(&vp, UI_DARKGRAY);
	DrawArrow3D(&vp, {0, 0, 0}, {1, 0, 0}, 0.03f, 0.01f, 8, 5.f, UI_RED);
	DrawArrow3D(&vp, {0, 0, 0}, {0, 1, 0}, 0.03f, 0.01f, 8, 5.f, UI_GREEN);
	
	bool regen_plant = true;
	if (regen_plant) {
		DS_ArenaReset(&g_plant_arena);
		g_plant = GeneratePlant(&g_plant_arena, &g_plant_params);
	}

	DebugDrawPlant(&vp, &g_plant);
	
	UI_BoxComputeRects(root, {0, 0});
	UI_DrawBox(root);

	UI_Outputs ui_outputs;
	UI_EndFrame(&ui_outputs);
	
	FLOAT clearcolor[4] = { 0.15f, 0.15f, 0.15f, 1.f };
	UI_DX11_STATE.device_context->ClearRenderTargetView(g_framebuffer_rtv, clearcolor);

	UI_DX11_EndFrame(&ui_outputs, g_framebuffer_rtv);
	UI_OS_ApplyMouseControl(&g_window, ui_outputs.cursor);
	
	g_swapchain->Present(1, 0);
}

static void OnResizeWindow(uint32_t width, uint32_t height, void *user_ptr) {
	g_window_size.x = (float)width;
	g_window_size.y = (float)height;

	// Recreate swapchain

	g_framebuffer_rtv->Release();

	g_swapchain->ResizeBuffers(0, 0, 0, DXGI_FORMAT_UNKNOWN, 0);

	ID3D11Texture2D* framebuffer;
	g_swapchain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&framebuffer); // grab framebuffer from swapchain

	D3D11_RENDER_TARGET_VIEW_DESC framebuffer_rtv_desc = {0};
	framebuffer_rtv_desc.Format        = DXGI_FORMAT_B8G8R8A8_UNORM;
	framebuffer_rtv_desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DMS;
	UI_DX11_STATE.device->CreateRenderTargetView(framebuffer, &framebuffer_rtv_desc, &g_framebuffer_rtv);

	framebuffer->Release(); // We don't need this handle anymore

	UpdateAndRender();
}

static void InitApp() {
	DS_ArenaInit(&g_persist, 4096, DS_HEAP);
	DS_ArenaInit(&g_temp, 4096, DS_HEAP);

	g_window = OS_WINDOW_Create((uint32_t)g_window_size.x, (uint32_t)g_window_size.y, "UI demo (DX11)");

	D3D_FEATURE_LEVEL dx_feature_levels[] = { D3D_FEATURE_LEVEL_11_0 };

	DXGI_SWAP_CHAIN_DESC swapchain_desc = {0};
	swapchain_desc.BufferDesc.Width  = 0; // use window width
	swapchain_desc.BufferDesc.Height = 0; // use window height
	swapchain_desc.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	swapchain_desc.SampleDesc.Count  = 8;
	swapchain_desc.BufferUsage       = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapchain_desc.BufferCount       = 2;
	swapchain_desc.OutputWindow      = (HWND)g_window.handle;
	swapchain_desc.Windowed          = TRUE;
	swapchain_desc.SwapEffect        = DXGI_SWAP_EFFECT_DISCARD;

	uint32_t create_device_flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT | (ENABLE_D3D11_DEBUG_MODE ? D3D11_CREATE_DEVICE_DEBUG : 0);

	HRESULT res = D3D11CreateDeviceAndSwapChain(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL,
		create_device_flags, dx_feature_levels, ARRAYSIZE(dx_feature_levels), D3D11_SDK_VERSION,
		&swapchain_desc, &g_swapchain, &g_device, NULL, &g_device_context);
	assert(res == S_OK);

	g_swapchain->GetDesc(&swapchain_desc); // Update swapchain_desc with actual window size

	///////////////////////////////////////////////////////////////////////////////////////////////

	ID3D11Texture2D* framebuffer;
	g_swapchain->GetBuffer(0, _uuidof(ID3D11Texture2D), (void**)&framebuffer); // grab framebuffer from swapchain

	D3D11_RENDER_TARGET_VIEW_DESC framebuffer_rtv_desc = {0};
	framebuffer_rtv_desc.Format        = DXGI_FORMAT_B8G8R8A8_UNORM;
	framebuffer_rtv_desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DMS;
	g_device->CreateRenderTargetView(framebuffer, &framebuffer_rtv_desc, &g_framebuffer_rtv);

	framebuffer->Release(); // We don't need this handle anymore

	///////////////////////////////////////////////////////////////////////////////////////////////

	UI_Backend ui_backend = {0};
	UI_DX11_Init(&ui_backend, g_device, g_device_context);
	UI_Init(&g_persist, &ui_backend);

	// NOTE: the font data must remain alive across the whole program lifetime!
	STR roboto_mono_ttf = ReadEntireFile(&g_persist, "../fire/fire_ui/resources/roboto_mono.ttf");
	STR icons_ttf = ReadEntireFile(&g_persist, "../fire/fire_ui/resources/fontello/font/fontello.ttf");

	UI_FontInit(&g_base_font, roboto_mono_ttf.data, -4.f);
	UI_FontInit(&g_icons_font, icons_ttf.data, -2.f);
}

static void DeinitApp() {
	UI_FontDeinit(&g_base_font);
	UI_FontDeinit(&g_icons_font);

	UI_Deinit();
	UI_DX11_Deinit();

	g_framebuffer_rtv->Release();
	g_swapchain->Release();
	g_device->Release();
	g_device_context->Release();

	DS_ArenaDeinit(&g_persist);
	DS_ArenaDeinit(&g_temp);
}

int main() {
	InitApp();

	g_camera.pos.Y = -5.f;
	g_camera.pos.Z = 2.f;

	DS_ArenaInit(&g_plant_arena, 256, DS_HEAP);
	
	while (!OS_WINDOW_ShouldClose(&g_window)) {
		Input_OS_Events input_events;
		Input_OS_BeginEvents(&input_events, &g_inputs, &g_temp);
		for (OS_WINDOW_Event event; OS_WINDOW_PollEvent(&g_window, &event, OnResizeWindow, NULL);) {
			Input_OS_AddEvent(&input_events, &event);
		}
		Input_OS_EndEvents(&input_events);

		UpdateAndRender();
	}

	DS_ArenaDeinit(&g_plant_arena);
}
