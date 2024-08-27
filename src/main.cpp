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

#define CGLTF_IMPLEMENTATION
#include "third_party/cgltf.h"

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

struct ImportedMeshMorphTarget {
	DS_DynArray(HMM_Vec3) vertices;
};

struct ImportedMesh {
	DS_DynArray(ImportedMeshMorphTarget) vertices_morphs;
	DS_DynArray(uint32_t) indices;
};

//// Globals ///////////////////////////////////////////////

static UI_Vec2 g_window_size = {1920, 1080};
static bool g_window_fullscreen = true;
static OS_WINDOW g_window;

static ID3D11Device* g_dx11_device;
static ID3D11DeviceContext* g_dx11_device_context;
static IDXGISwapChain* g_dx11_swapchain;
static ID3D11RenderTargetView* g_dx11_framebuffer_view;
static ID3D11Texture2D* g_dx11_depthbuffer;
static ID3D11DepthStencilView* g_dx11_depthbuffer_view;

static UI_Font g_base_font, g_icons_font;

static Input_Frame g_inputs;
static DS_Arena g_persist;
static DS_Arena g_temp;

static Camera g_camera;

static PlantParameters g_plant_params;
static DS_Arena g_plant_arena;
static Plant g_plant;

static bool g_has_plant_mesh;
static B3R_Mesh g_plant_mesh;

static B3R_WireMesh g_grid_mesh;

static ImportedMesh g_imported_mesh_bud;
static ImportedMesh g_imported_mesh_leaf;

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

static void ImportMeshAddMorph(DS_Arena* arena, ImportedMesh* result, cgltf_attribute* attributes, int attributes_count) {
	HMM_Vec3* positions_data = NULL;
	HMM_Vec3* normals_data = NULL;
	HMM_Vec2* texcoords_data = NULL;

	uint32_t num_vertices = (uint32_t)attributes[0].data->count;

	for (int i = 0; i < attributes_count; i++) {
		cgltf_attribute* attribute = &attributes[i];
		void* attribute_data = (char*)attribute->data->buffer_view->buffer->data + attribute->data->buffer_view->offset;

		assert(attribute->data->count == num_vertices);

		if (attribute->type == cgltf_attribute_type_position) {
			positions_data = (HMM_Vec3*)attribute_data;
		} else if (attribute->type == cgltf_attribute_type_normal) {
			normals_data = (HMM_Vec3*)attribute_data;
		} else if (attribute->type == cgltf_attribute_type_texcoord) {
			texcoords_data = (HMM_Vec2*)attribute_data;
		}
	}

	ImportedMeshMorphTarget morph{};
	DS_ArrInit(&morph.vertices, arena);
	DS_ArrReserve(&morph.vertices, num_vertices);

	for (uint32_t i = 0; i < num_vertices; i++) {
		HMM_Vec3 position = positions_data[i];
		
		// In GLTF, Y is up, but we want Z up.
		position = {position.X, -1.f * position.Z, position.Y};
		// normal = {normal.X, -1.f * normal.Z, normal.Y};
		
		DS_ArrPush(&morph.vertices, position);
	}
	
	DS_ArrPush(&result->vertices_morphs, morph);
}

static ImportedMesh ImportMesh(DS_Arena* arena, const char* filepath) {
	ImportedMesh result{};
	DS_ArrInit(&result.vertices_morphs, arena);
	DS_ArrInit(&result.indices, arena);

	cgltf_options options{};
	cgltf_data* data = NULL;

	bool ok = true;
	ok = cgltf_parse_file(&options, filepath, &data) == cgltf_result_success;

	ok = ok && cgltf_load_buffers(&options, data, filepath) == cgltf_result_success;

	ok = ok && cgltf_validate(data) == cgltf_result_success;

	ok = ok && data->meshes_count == 1;
	
	if (ok) {
		cgltf_mesh* mesh = &data->meshes[0];

		for (cgltf_size primitive_i = 0; primitive_i < mesh->primitives_count; primitive_i++) {
			cgltf_primitive* primitive = &mesh->primitives[primitive_i];
			cgltf_accessor* indices = primitive->indices;

			DS_ArrResizeUndef(&result.indices, (int)indices->count);

			void* primitive_indices = (char*)indices->buffer_view->buffer->data + indices->buffer_view->offset;
			if (indices->component_type == cgltf_component_type_r_16u) {
				for (cgltf_size i = 0; i < indices->count; i++) {
					result.indices.data[i] = ((uint16_t*)primitive_indices)[i];
				}
			}
			else if (indices->component_type == cgltf_component_type_r_32u) {
				for (cgltf_size i = 0; i < indices->count; i++) {
					result.indices.data[i] = ((uint32_t*)primitive_indices)[i];
				}
			}
			else assert(0);

			ImportMeshAddMorph(arena, &result, primitive->attributes, (int)primitive->attributes_count);
			
			for (int i = 0; i < primitive->targets_count; i++) {
				cgltf_morph_target morph_target = primitive->targets[i];
				ImportMeshAddMorph(arena, &result, morph_target.attributes, (int)morph_target.attributes_count);
			}
		}
	}

	assert(ok);
	return result;
}

typedef DS_DynArray(HMM_Vec3) MeshVertexList;
typedef DS_DynArray(uint32_t) MeshIndexList;

static void MeshBuilderAddQuad(MeshIndexList* list, uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
	DS_ArrPush(list, a); DS_ArrPush(list, b); DS_ArrPush(list, c);
	DS_ArrPush(list, a); DS_ArrPush(list, c); DS_ArrPush(list, d);
}

static void MeshBuilderAddImportedMesh(MeshVertexList* vertices, MeshIndexList* indices, ImportedMesh* imported_mesh, HMM_Vec3 position, HMM_Quat rotation, float scale, float morph_amount)
{
	ImportedMeshMorphTarget base_morph = DS_ArrGet(imported_mesh->vertices_morphs, 0);
	
	uint32_t first_vertex = (uint32_t)vertices->length;
	for (int i = 0; i < base_morph.vertices.length; i++) {
		HMM_Vec3 src_vert = base_morph.vertices.data[i];
		if (morph_amount > 0.f) {
			assert(imported_mesh->vertices_morphs.length > 1);
			ImportedMeshMorphTarget second_morph = DS_ArrGet(imported_mesh->vertices_morphs, 1);
			src_vert = HMM_LerpV3(src_vert, morph_amount, second_morph.vertices.data[i]);
		}

		HMM_Vec3 vert = position + HMM_RotateV3(src_vert * scale, rotation);
		DS_ArrPush(vertices, vert);
	}

	for (int i = 0; i < imported_mesh->indices.length; i++) {
		uint32_t src_idx = imported_mesh->indices.data[i];
		DS_ArrPush(indices, src_idx + first_vertex);
	}
}

static void RegeneratePlantMesh() {
	if (g_has_plant_mesh) {
		B3R_MeshDeinit(&g_plant_mesh);
	}
	
	MeshVertexList vertices = {&g_temp};
	MeshIndexList indices = {&g_temp};

	for (int i = 0; i < g_plant.stems.length; i++) {
		Stem* stem = &g_plant.stems.data[i];

		uint32_t prev_circle_first_vertex = 0;
		for (int j = 0; j < stem->points.length; j++) {
			StemPoint* stem_point = &stem->points.data[j];
			HMM_Vec3 local_x_offset = HMM_RotateV3({stem_point->thickness, 0, 0}, stem_point->rotation);
			HMM_Vec3 local_y_offset = HMM_RotateV3({0, stem_point->thickness, 0}, stem_point->rotation);

			uint32_t first_vertex = (uint32_t)vertices.length;
			int num_segments = 8;
			for (int k = 0; k < num_segments; k++) {
				float theta = 2.f * HMM_PI32 * (float)k / (float)num_segments;
				HMM_Vec3 p = stem_point->point + local_x_offset * cosf(theta) + local_y_offset * sinf(theta);
				DS_ArrPush(&vertices, p);
				
				if (j > 0) {
					int next_k = (k + 1) % 8;
					MeshBuilderAddQuad(&indices, prev_circle_first_vertex + k, prev_circle_first_vertex + next_k, first_vertex + next_k, first_vertex + k);
				}
			}

			prev_circle_first_vertex = first_vertex;
		}
		
		// Add a bud (or leaf) at the end.
		StemPoint* last_point = DS_ArrPeekPtr(stem->points);
		if (true || stem->end_leaf_expand > 0.f) {
			MeshBuilderAddImportedMesh(&vertices, &indices, &g_imported_mesh_leaf, last_point->point, last_point->rotation, 1.f, 0.5f);
		}
		else {
			MeshBuilderAddImportedMesh(&vertices, &indices, &g_imported_mesh_bud, last_point->point, last_point->rotation, 1.f, 0.f);
		}
	}

	B3R_MeshInit(&g_plant_mesh, B3R_VertexLayout_Position, vertices.data, vertices.length, indices.data, indices.length);
	g_has_plant_mesh = true;
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

	PlantParameters plant_params_old;
	memcpy(&plant_params_old, &g_plant_params, sizeof(PlantParameters)); // use memcpy to copy any compiler-introduced padding bytes as well

	UI_AddFmt(UI_KEY(), "Points per meter: %!f", &g_plant_params.points_per_meter);
	UI_AddFmt(UI_KEY(), "Age: %!f", &g_plant_params.age);
	UI_AddFmt(UI_KEY(), "Pitch twist: %!f", &g_plant_params.pitch_twist);
	UI_AddFmt(UI_KEY(), "Yaw twist: %!f", &g_plant_params.yaw_twist);
	UI_AddFmt(UI_KEY(), "Drop pitch: %!f", &g_plant_params.drop_pitch);

	//UI_AddButton(UI_KEY(), UI_SizeFit(), UI_SizeFit(), 0, STR_("Hello!"));
	UI_PopBox(root);

	bool regen_plant = memcmp(&plant_params_old, &g_plant_params, sizeof(PlantParameters)) != 0 || !g_has_plant_mesh;
	if (regen_plant) {
		DS_ArenaReset(&g_plant_arena);
		g_plant = GeneratePlant(&g_plant_arena, &g_plant_params);
		RegeneratePlantMesh();
	}

	UI_BoxComputeRects(root, {0, 0});
	UI_DrawBox(root);

	UI_Outputs ui_outputs;
	UI_EndFrame(&ui_outputs);
	
	FLOAT clearcolor[4] = { 0.3f, 0.4f, 0.5f, 1.f };
	g_dx11_device_context->ClearRenderTargetView(g_dx11_framebuffer_view, clearcolor);
	g_dx11_device_context->ClearDepthStencilView(g_dx11_depthbuffer_view, D3D11_CLEAR_DEPTH, 1.0f, 0);
	
	B3R_BeginDrawing(g_dx11_device_context, g_dx11_framebuffer_view, g_dx11_depthbuffer_view, g_camera.cached.clip_from_world, g_camera.cached.position);
	B3R_DrawWireMesh(&g_grid_mesh, 0.01f, 100000.f, 100000.f, 1.f, 1.f, 1.f, 1.f);
	B3R_DrawMesh(&g_plant_mesh, B3R_DebugMode_HalfFlatNormal);
	B3R_EndDrawing();

	UI_DX11_EndFrame(&ui_outputs, g_dx11_framebuffer_view);
	UI_OS_ApplyMouseControl(&g_window, ui_outputs.cursor);
	
	g_dx11_swapchain->Present(1, 0);
}

static void InitRenderTargets() {
	ID3D11Texture2D* framebuffer;
	g_dx11_swapchain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&framebuffer); // grab framebuffer from swapchain

	D3D11_RENDER_TARGET_VIEW_DESC framebuffer_view_desc = {0};
	framebuffer_view_desc.Format        = DXGI_FORMAT_B8G8R8A8_UNORM;
	framebuffer_view_desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DMS;
	g_dx11_device->CreateRenderTargetView(framebuffer, &framebuffer_view_desc, &g_dx11_framebuffer_view);

	framebuffer->Release(); // Let go of the framebuffer handle

	D3D11_TEXTURE2D_DESC depthbuffer_desc;
	framebuffer->GetDesc(&depthbuffer_desc); // copy framebuffer properties; they're mostly the same
	depthbuffer_desc.Format    = DXGI_FORMAT_D24_UNORM_S8_UINT;
	depthbuffer_desc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
	g_dx11_device->CreateTexture2D(&depthbuffer_desc, NULL, &g_dx11_depthbuffer);
	g_dx11_device->CreateDepthStencilView(g_dx11_depthbuffer, NULL, &g_dx11_depthbuffer_view);
}

static void DeinitRenderTargets() {
	g_dx11_framebuffer_view->Release();
	g_dx11_depthbuffer->Release();
	g_dx11_depthbuffer_view->Release();
}

static void OnResizeWindow(uint32_t width, uint32_t height, void *user_ptr) {
	g_window_size.x = (float)width;
	g_window_size.y = (float)height;

	// Recreate swapchain

	DeinitRenderTargets();

	g_dx11_swapchain->ResizeBuffers(0, 0, 0, DXGI_FORMAT_UNKNOWN, 0);

	InitRenderTargets();

	UpdateAndRender();
}

static void InitApp() {
	DS_ArenaInit(&g_persist, 4096, DS_HEAP);
	DS_ArenaInit(&g_temp, 4096, DS_HEAP);

	g_window = OS_WINDOW_Create((uint32_t)g_window_size.x, (uint32_t)g_window_size.y, "UI demo (DX11)");
	OS_WINDOW_SetFullscreen(&g_window, g_window_fullscreen);

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
		&swapchain_desc, &g_dx11_swapchain, &g_dx11_device, NULL, &g_dx11_device_context);
	assert(res == S_OK);

	g_dx11_swapchain->GetDesc(&swapchain_desc); // Update swapchain_desc with actual window size

	InitRenderTargets();

	UI_Backend ui_backend = {0};
	UI_DX11_Init(&ui_backend, g_dx11_device, g_dx11_device_context);
	UI_Init(&g_persist, &ui_backend);

	// NOTE: the font data must remain alive across the whole program lifetime!
	STR roboto_mono_ttf = ReadEntireFile(&g_persist, "../fire/fire_ui/resources/roboto_mono.ttf");
	STR icons_ttf = ReadEntireFile(&g_persist, "../fire/fire_ui/resources/fontello/font/fontello.ttf");

	UI_FontInit(&g_base_font, roboto_mono_ttf.data, -4.f);
	UI_FontInit(&g_icons_font, icons_ttf.data, -2.f);

	B3R_Init(g_dx11_device);
}

static void DeinitApp() {
	B3R_Deinit();

	UI_FontDeinit(&g_base_font);
	UI_FontDeinit(&g_icons_font);

	UI_Deinit();
	UI_DX11_Deinit();

	DeinitRenderTargets();
	g_dx11_swapchain->Release();
	g_dx11_device->Release();
	g_dx11_device_context->Release();

	DS_ArenaDeinit(&g_persist);
	DS_ArenaDeinit(&g_temp);
}

static void InitGridMesh(B3R_WireMesh* mesh) {
	struct WireVertex {
		HMM_Vec3 position;
		UI_Color color;
	};

	UI_Color grid_color = UI_MakeColorF(0.3f, 0.3f, 0.3f, 0.5f);
	UI_Color x_axis_color = UI_MakeColorF(1.f, 0.2f, 0.2f, 0.8f);
	UI_Color y_axis_color = UI_MakeColorF(0.15f, 1.f, 0.15f, 0.6f);

	DS_DynArray(WireVertex) wire_verts = {&g_temp};
	int grid_extent = 8;

	float cell_size = 1.f;
	HMM_Vec3 origin = {0, 0, 0};
	HMM_Vec3 x_dir = {cell_size, 0, 0};
	HMM_Vec3 y_dir = {0, cell_size, 0};

	HMM_Vec3 x_origin_a = origin + y_dir * -(float)grid_extent;
	HMM_Vec3 x_origin_b = origin + y_dir * (float)grid_extent;

	for (int x = -grid_extent; x <= grid_extent; x++) {
		HMM_Vec3 x_offset = x_dir * (float)x;
		DS_ArrPush(&wire_verts, {x_origin_a + x_offset, x == 0 ? y_axis_color : grid_color});
		DS_ArrPush(&wire_verts, {x_origin_b + x_offset, x == 0 ? y_axis_color : grid_color});
	}

	HMM_Vec3 y_origin_a = origin + x_dir * -(float)grid_extent;
	HMM_Vec3 y_origin_b = origin + x_dir * (float)grid_extent;

	for (int y = -grid_extent; y <= grid_extent; y++) {
		HMM_Vec3 y_offset = y_dir * (float)y;
		DS_ArrPush(&wire_verts, {y_origin_a + y_offset, y == 0 ? x_axis_color : grid_color});
		DS_ArrPush(&wire_verts, {y_origin_b + y_offset, y == 0 ? x_axis_color : grid_color});
	}

	B3R_WireMeshInit(mesh, (B3R_WireVertex*)wire_verts.data, wire_verts.length);
}

int main() {
	InitApp();

	g_camera.pos.Y = -5.f;
	g_camera.pos.Z = 2.f;

	DS_ArenaInit(&g_plant_arena, 256, DS_HEAP);

	InitGridMesh(&g_grid_mesh);
	
	g_imported_mesh_leaf = ImportMesh(&g_persist, "../resources/leaf_with_morph_targets.glb");
	g_imported_mesh_bud = ImportMesh(&g_persist, "../resources/bud.glb");

	while (!OS_WINDOW_ShouldClose(&g_window)) {
		Input_OS_Events input_events;
		Input_OS_BeginEvents(&input_events, &g_inputs, &g_temp);
		for (OS_WINDOW_Event event; OS_WINDOW_PollEvent(&g_window, &event, OnResizeWindow, NULL);) {
			Input_OS_AddEvent(&input_events, &event);
		}
		Input_OS_EndEvents(&input_events);

		UpdateAndRender();
	}

	B3R_WireMeshDeinit(&g_grid_mesh);
	DS_ArenaDeinit(&g_plant_arena);

	DeinitApp();
}
