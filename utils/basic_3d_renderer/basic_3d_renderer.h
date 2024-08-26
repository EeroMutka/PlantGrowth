// Basic high-level 3D renderer that is supposed to be fun and easy to use for prototyping.
// Not necessarily the most performant or flexible. Currently only targets D3D11.
// 
// For the feature set, I'm thinking an uber shader that can do most of what, say, blender EEVEE can do. But rendering
// basic meshes should still be super easy.
// 
// Depends on `fire.h`, `HandmadeMath.h` and D3D11
// 

#include <stdint.h>

typedef enum B3R_VertexLayout {
	B3R_VertexLayout_Position,            // { vec3 position; }
	//B3R_VertexLayout_PositionNormal,      // { vec3 position; vec3 normal; }
	B3R_VertexLayout_PositionNormalColor, // { vec3 position; vec3 normal; }
	B3R_VertexLayout_COUNT,
} B3R_VertexLayout;

typedef struct B3R_Mesh {
	int index_count;
	B3R_VertexLayout vertex_layout;
	ID3D11Buffer* vertex_buffer;
	ID3D11Buffer* index_buffer;
} B3R_Mesh;

typedef struct B3R_WireVertex {
	float x, y, z;
	uint32_t rgba;
} B3R_WireVertex;

// I think I need a wire renderer for drawing wireframes. Say I want to render lots of lines in the world. yeah... idk!
typedef struct B3R_WireMesh {
	// Each index in the index buffer is an index to a B3R_WireVertex. B3R_WireVertex's must always
	// come in a pair of 2, making it easier to fetch both vertices of an edge, given just one of them.
	// For this reason, the vertices are fetched manually in the vertex shader instead of using input attributes
	ID3D11Buffer* vertex_structured_buffer;
	ID3D11ShaderResourceView* vertex_structured_buffer_srv;
	int num_wire_vertices;
} B3R_WireMesh;

typedef struct B3R_Texture {
	ID3D11Texture2D* texture;
} B3R_Texture;

//typedef struct B3R_Material {
//	B3R_Texture* tex_color; // may be NULL
//} B3R_Material;

typedef enum B3R_DebugMode {
	B3R_DebugMode_None             = 0,
	B3R_DebugMode_Wireframe        = 1,
	B3R_DebugMode_FlatNormal       = 2,
	B3R_DebugMode_HalfFlatNormal   = 3,
	B3R_DebugMode_VertexNormal     = 4,
	B3R_DebugMode_HalfVertexNormal = 5,
	B3R_DebugMode_BlockoutGrid     = 6,
} B3R_DebugMode;

static void B3R_Init(ID3D11Device* device);
static void B3R_Deinit(void);

static void B3R_MeshInit(B3R_Mesh* mesh, B3R_VertexLayout layout, const void* vertices, int num_vertices, const uint32_t* indices, int num_indices);
static void B3R_MeshDeinit(B3R_Mesh* mesh);

// The vertices must come in pairs of 2, each defining a line segment to draw.
static void B3R_WireMeshInit(B3R_WireMesh* mesh, const B3R_WireVertex* vertices, int num_vertices);
static void B3R_WireMeshDeinit(B3R_WireMesh* mesh);

// RGBA8 layout is expected
static void B3R_TextureInit(B3R_Texture* texture, int width, int height, void* data);
static void B3R_TextureDeinit(B3R_Texture* texture);

static void B3R_BeginDrawing(ID3D11DeviceContext* dc,
	ID3D11RenderTargetView* framebuffer, ID3D11DepthStencilView* depthbuffer,
	HMM_Mat4 clip_from_world, HMM_Vec3 camera_pos);
static void B3R_EndDrawing(void);
static void B3R_DrawMesh(const B3R_Mesh* mesh, B3R_DebugMode debug_mode);
static void B3R_DrawWireMesh(const B3R_WireMesh* mesh,
	float thickness, float fade_dist_min, float fade_dist_max,
	float r, float g, float b, float a);

// ** IMPLEMENTATION **

typedef struct B3R_Constants {
	HMM_Mat4 clip_from_world;
	HMM_Vec3 camera_pos;
	float wire_thickness;
	float wire_color[4];
	float wire_fade_dist_min;
	float wire_fade_dist_max;
	int debug_mode;
} B3R_Constants;

typedef struct B3R_State {
	ID3D11Device* device;
	ID3D11VertexShader* vertex_shaders[B3R_VertexLayout_COUNT];
	ID3D11PixelShader* pixel_shaders[B3R_VertexLayout_COUNT];
	ID3D11InputLayout* vertex_layouts[B3R_VertexLayout_COUNT];
	int vertex_layout_vertex_sizes[B3R_VertexLayout_COUNT];
	ID3D11VertexShader* wire_vs;
	ID3D11PixelShader* wire_ps;
	ID3D11RasterizerState* raster_state;
	ID3D11RasterizerState* wireframe_raster_state;
	ID3D11BlendState* blend_state;
	ID3D11SamplerState* sampler_state;
	ID3D11Buffer* constant_buffer;
	
	B3R_Constants constants;
	ID3D11DeviceContext* dc; // only valid in between BeginDrawing and EndDrawing
} B3R_State;

static B3R_State B3R_STATE;

#define B3R_MULTILINE_STR(...) #__VA_ARGS__

static const char B3R_SHADER_SRC[] = B3R_MULTILINE_STR(
\n	cbuffer constants : register(b0) {
\n		float4x4 clip_from_world;
\n		float3 camera_pos;
\n		float wire_thickness;
\n		float4 wire_color;
\n		float wire_fade_dist_min;
\n		float wire_fade_dist_max;
\n		int debug_mode;
\n	}
\n	
\n	struct VertexData {
\n		float3 position : POS;
\n	#if defined(B3R_LAYOUT_POSITION_NORMAL_COLOR)
\n		float3 normal   : NOR;
\n		float4 color    : COL;
\n	#endif
\n	};
\n	
\n	struct PixelData {
\n		float4 position    : SV_POSITION;
\n		float3 position_ws : POS;
\n	#if defined(B3R_LAYOUT_POSITION_NORMAL_COLOR)
\n		float3 normal      : NOR;
\n		float4 color       : COL;
\n	#endif
\n	};
\n	
\n	PixelData VSMain(VertexData vertex) {
\n		PixelData output;
\n		output.position = mul(clip_from_world, float4(vertex.position, 1));
\n		output.position.y *= -1.;
\n		output.position_ws = vertex.position;
\n	#if defined(B3R_LAYOUT_POSITION_NORMAL_COLOR)
\n		output.normal = vertex.normal;
\n		output.color = vertex.color;
\n	#endif
\n		return output;
\n	}
\n	
\n	float3 BlockoutGrid(float3 position, float3 normal) {
\n		position /= 64.;
\n		float3 xyz = abs(frac(position) - 0.5);
\n		
\n		float3 weights = abs(normal);
\n		weights *= 1. / (weights.x + weights.y + weights.z);
\n
\n		float x_checker = frac(0.5*(floor(position.y) + floor(position.z) + 0.5));
\n		float y_checker = frac(0.5*(floor(position.z) + floor(position.x) + 0.5));
\n		float z_checker = frac(0.5*(floor(position.x) + floor(position.y) + 0.5));
\n		float x_grid = saturate((max(xyz.y, xyz.z) - 0.48)*105.) - 0.5*x_checker;
\n		float y_grid = saturate((max(xyz.z, xyz.x) - 0.48)*105.) - 0.5*y_checker;
\n		float z_grid = saturate((max(xyz.x, xyz.y) - 0.48)*105.) - 0.5*z_checker;
\n		float grid = weights.x*x_grid + weights.y*y_grid + weights.z*z_grid;
\n
\n		//float lightness = dot(normal, normalize(float3(0.4, 0.25, 0.9)))*0.5 + 0.5;
\n		//return lerp(float3(0.24, 0.1, 0.08), float3(0.8, 0.7, 0.6), lightness) * lerp(1, 0.75, grid); // float3(0.9, 0.4, 0.3)  float3(0.8, 0.4, 0.3)
\n		float3 lightness = normal*0.5 + 0.5;
\n		return lightness * lerp(1, 0.75, grid); // float3(0.9, 0.4, 0.3)  float3(0.8, 0.4, 0.3)
\n	}
\n	
\n	
\n	float4 PSMain(PixelData pixel) : SV_TARGET {
\n	#if defined(B3R_LAYOUT_POSITION_NORMAL_COLOR)
\n		float3 n = pixel.normal;
\n	#else
\n		float3 n = normalize(cross(ddy(pixel.position_ws), ddx(pixel.position_ws)));
\n	#endif
\n
\n		if (debug_mode > 1) {
\n			if (debug_mode == 2 || debug_mode == 3) { // flat normal or half flat normal
\n				float3 flat_n = normalize(cross(ddy(pixel.position_ws), ddx(pixel.position_ws)));
\n				return debug_mode == 3 ? float4(flat_n*0.5 + 0.5, 1) : float4(flat_n, 1);
\n			}
\n	#if defined(B3R_LAYOUT_POSITION_NORMAL_COLOR)
\n			if (debug_mode == 4 || debug_mode == 5) { // vertex normal or half vertex normal
\n				return debug_mode == 5 ? float4(pixel.normal*0.5 + 0.5, 1) : float4(pixel.normal, 1);
\n			}
\n	#endif
\n			if (debug_mode == 6) { // blockout grid mode
\n				float3 blockout_color = BlockoutGrid(pixel.position_ws, n);
\n				return float4(blockout_color, 1);
\n			}
\n		}
\n
\n	#if defined(B3R_LAYOUT_POSITION_NORMAL_COLOR)
\n		//float3 n = normalize(cross(pos_ddy, pos_ddx));
\n		//return float4(n, 1);
\n		float lightness = dot(n, normalize(float3(1, 1, 1)))*0.5 + 0.5;
\n		return float4(lerp(0.5, 1., lightness) * pixel.color.xyz, 1);
\n	#else
\n		return float4(n*0.5 + 0.5, 1);
\n	#endif
\n	}
\n);

// Idea: maybe we can expand the wires in ALL directions, such that edges with the same vertex get rendered as points!
// Having a distance fade would be a nice extra touch.
static const char B3R_WIRE_SHADER_SRC[] = B3R_MULTILINE_STR(
\n	cbuffer constants : register(b0) {
\n		float4x4 clip_from_world;
\n		float3 camera_pos;
\n		float wire_thickness;
\n		float4 wire_color;
\n		float wire_fade_dist_min;
\n		float wire_fade_dist_max;
\n		int debug_mode;
\n	}
\n	
\n	struct WireVertex {
\n		float3 position;
\n		uint rgba;
\n	};
\n	
\n	StructuredBuffer<WireVertex> wire_vertices : register(t0);
\n
\n	struct PixelData {
\n		float4 position : SV_POSITION;
\n		float3 position_ws : POS;
\n		float4 color    : COL;
\n	};
\n	
\n	static const uint quad_v_idx[6] = {0, 1, 1, 0, 1, 0};
\n	static const float2 quad_expand[6] = {float2(-1, -1), float2(1, -1), float2(1, 1), float2(-1, -1), float2(1, 1), float2(-1, 1)};
\n	
\n	PixelData VSMain(uint vertex_idx : SV_VertexID) {
\n		uint local_vertex_idx = vertex_idx % 6;
\n		uint v0_idx = (vertex_idx - local_vertex_idx) / 3;
\n		uint v1_idx = v0_idx + 1;
\n		
\n		float3 pos[2] = {wire_vertices[v0_idx].position, wire_vertices[v1_idx].position};
\n		uint col[2] = {wire_vertices[v0_idx].rgba, wire_vertices[v1_idx].rgba};
\n		float3 right = normalize(pos[1] - pos[0]);
\n		float3 up = normalize(cross(right, pos[0] - camera_pos.xyz));
\n
\n		float2 expand = quad_expand[local_vertex_idx];
\n		float3 vertex_pos = pos[quad_v_idx[local_vertex_idx]] + (expand.x*right + expand.y*up)*wire_thickness;
\n
\n		uint vertex_col = col[quad_v_idx[local_vertex_idx]];
\n
\n		PixelData output;
\n		output.position = mul(clip_from_world, float4(vertex_pos, 1));
\n		output.position_ws = vertex_pos;
\n		output.position.y *= -1.;
\n		output.color = float4(vertex_col & 0xFF, (vertex_col >> 8) & 0xFF, (vertex_col >> 16) & 0xFF, vertex_col >> 24) / 255.;
\n		return output;
\n	}
\n	
\n	float4 PSMain(PixelData pixel) : SV_TARGET {
\n		float dist = length(pixel.position_ws - camera_pos.xyz);
\n		float dist_fade = 1. - smoothstep(1500., 5500., dist);
\n		float4 result_color = wire_color * pixel.color;
\n		result_color.a *= dist_fade;
\n		return result_color;
\n	}
\n);

static void B3R_Init(ID3D11Device* device) {
	B3R_STATE.device = device;
	
	ID3DBlob* errors = NULL;
	
	{
		ID3DBlob* wire_vs_blob;
		D3DCompile(B3R_WIRE_SHADER_SRC, sizeof(B3R_WIRE_SHADER_SRC)-1, "VS", NULL, NULL, "VSMain", "vs_5_0", 0, 0, &wire_vs_blob, &errors);
		if (wire_vs_blob == NULL) { char* errs = (char*)errors->GetBufferPointer(); __debugbreak(); }

		ID3DBlob* wire_ps_blob;
		D3DCompile(B3R_WIRE_SHADER_SRC, sizeof(B3R_WIRE_SHADER_SRC)-1, "PS", NULL, NULL, "PSMain", "ps_5_0", 0, 0, &wire_ps_blob, &errors);
		if (wire_ps_blob == NULL) { char* errs = (char*)errors->GetBufferPointer(); __debugbreak(); }

		device->CreateVertexShader(wire_vs_blob->GetBufferPointer(), wire_vs_blob->GetBufferSize(), NULL, &B3R_STATE.wire_vs);
		device->CreatePixelShader(wire_ps_blob->GetBufferPointer(), wire_ps_blob->GetBufferSize(), NULL, &B3R_STATE.wire_ps);
	}

	for (int i = 0; i < B3R_VertexLayout_COUNT; i++) {
		ID3DBlob* vs_blob;
		D3D_SHADER_MACRO macros[2] = {0};
		if (i == B3R_VertexLayout_PositionNormalColor) {
			macros[0].Name = "B3R_LAYOUT_POSITION_NORMAL_COLOR";
			macros[0].Definition = "1";
		}

		D3DCompile(B3R_SHADER_SRC, sizeof(B3R_SHADER_SRC) - 1, "VS", macros, NULL, "VSMain", "vs_5_0", 0, 0, &vs_blob, &errors);
		if (vs_blob == NULL) { char* errs = (char*)errors->GetBufferPointer(); __debugbreak(); }

		device->CreateVertexShader(vs_blob->GetBufferPointer(), vs_blob->GetBufferSize(), NULL, &B3R_STATE.vertex_shaders[i]);
		
		ID3DBlob* ps_blob;
		D3DCompile(B3R_SHADER_SRC, sizeof(B3R_SHADER_SRC) - 1, "PS", macros, NULL, "PSMain", "ps_5_0", 0, 0, &ps_blob, NULL);

		device->CreatePixelShader(ps_blob->GetBufferPointer(), ps_blob->GetBufferSize(), NULL, &B3R_STATE.pixel_shaders[i]);
		
		int vertex_size;
		const D3D11_INPUT_ELEMENT_DESC* input_elems;
		int input_elems_count;

		if (i == B3R_VertexLayout_Position) {
			static const D3D11_INPUT_ELEMENT_DESC input_elem_desc[] = {
				{"POS", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
			};
			input_elems = input_elem_desc;
			input_elems_count = 1;
			vertex_size = sizeof(float)*3;
		}
		else if (i == B3R_VertexLayout_PositionNormalColor) {
			static const D3D11_INPUT_ELEMENT_DESC input_elem_desc[] = {
				{"POS", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
				{"NOR", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
				{"COL", 0, DXGI_FORMAT_R8G8B8A8_UNORM,  0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0},
			};
			input_elems = input_elem_desc;
			input_elems_count = 3;
			vertex_size = sizeof(float)*3 + sizeof(float)*3 + sizeof(uint32_t);
		}
		else assert(0);
		
		B3R_STATE.vertex_layout_vertex_sizes[i] = vertex_size;
		device->CreateInputLayout(input_elems, input_elems_count, vs_blob->GetBufferPointer(), vs_blob->GetBufferSize(), &B3R_STATE.vertex_layouts[i]);
	}

	///////////////////////////////////////////////////////////////////////////////////////////////

	D3D11_BUFFER_DESC buffer_desc = {0};
	buffer_desc.ByteWidth = (sizeof(B3R_Constants) + 0xf) & 0xfffffff0; // ensure constant buffer size is multiple of 16 bytes
	buffer_desc.Usage = D3D11_USAGE_DYNAMIC; // Will be updated every frame
	buffer_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	buffer_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	device->CreateBuffer(&buffer_desc, NULL, &B3R_STATE.constant_buffer);
	
	D3D11_RASTERIZER_DESC raster_desc = {0};
	raster_desc.FillMode = D3D11_FILL_SOLID;
	raster_desc.CullMode = D3D11_CULL_NONE;
	raster_desc.MultisampleEnable = true; // MSAA
	//raster_desc.CullMode = D3D11_CULL_BACK;
	device->CreateRasterizerState(&raster_desc, &B3R_STATE.raster_state);

	D3D11_RASTERIZER_DESC wireframe_raster_desc = {0};
	wireframe_raster_desc.FillMode = D3D11_FILL_WIREFRAME;
	wireframe_raster_desc.CullMode = D3D11_CULL_NONE;
	wireframe_raster_desc.MultisampleEnable = true; // MSAA
	//wireframe_raster_desc.CullMode = D3D11_CULL_BACK;
	device->CreateRasterizerState(&wireframe_raster_desc, &B3R_STATE.wireframe_raster_state);
	
	D3D11_SAMPLER_DESC sampler_desc = {0};
	sampler_desc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
	sampler_desc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
	sampler_desc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
	sampler_desc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
	sampler_desc.ComparisonFunc = D3D11_COMPARISON_NEVER;
	device->CreateSamplerState(&sampler_desc, &B3R_STATE.sampler_state);
	
	D3D11_RENDER_TARGET_BLEND_DESC blend_desc = {0};
	blend_desc.BlendEnable = 1;
	blend_desc.SrcBlend = D3D11_BLEND_SRC_ALPHA;
	blend_desc.DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
	blend_desc.BlendOp = D3D11_BLEND_OP_ADD;
	blend_desc.SrcBlendAlpha = D3D11_BLEND_ONE;
	blend_desc.DestBlendAlpha = D3D11_BLEND_ZERO;
	blend_desc.BlendOpAlpha = D3D11_BLEND_OP_ADD;
	blend_desc.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

	D3D11_BLEND_DESC blend_state_desc = {0};
	blend_state_desc.RenderTarget[0] = blend_desc;
	device->CreateBlendState(&blend_state_desc, &B3R_STATE.blend_state);
}

static void B3R_WireMeshInit(B3R_WireMesh* mesh, const B3R_WireVertex* vertices, int num_vertices) {
	mesh->num_wire_vertices = num_vertices;

	D3D11_SUBRESOURCE_DATA vertex_data = {vertices};
	D3D11_BUFFER_DESC vertex_buffer_desc = {0};
	vertex_buffer_desc.ByteWidth = sizeof(B3R_WireVertex) * num_vertices;
	vertex_buffer_desc.Usage = D3D11_USAGE_IMMUTABLE;
	vertex_buffer_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	vertex_buffer_desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
	vertex_buffer_desc.StructureByteStride = sizeof(B3R_WireVertex);
	B3R_STATE.device->CreateBuffer(&vertex_buffer_desc, &vertex_data, &mesh->vertex_structured_buffer);

	D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = {0};
	srv_desc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
	srv_desc.Buffer.FirstElement = 0;
	srv_desc.Buffer.NumElements = num_vertices;
	B3R_STATE.device->CreateShaderResourceView(mesh->vertex_structured_buffer, &srv_desc, &mesh->vertex_structured_buffer_srv);
}

static void B3R_WireMeshDeinit(B3R_WireMesh* mesh) {
	mesh->vertex_structured_buffer_srv->Release();
	mesh->vertex_structured_buffer->Release();
}

static void B3R_MeshInit(B3R_Mesh* mesh, B3R_VertexLayout layout, const void* vertices, int num_vertices, const uint32_t* indices, int num_indices) {
	//assert(layout == B3R_VertexLayout_PositionNormalColor); // TODO: support other layouts

	mesh->index_count = num_indices;
	mesh->vertex_layout = layout;
	mesh->vertex_buffer = NULL;
	mesh->index_buffer = NULL;
	if (num_vertices > 0 && num_indices > 0) {
		D3D11_SUBRESOURCE_DATA vertex_data = {vertices};
		D3D11_BUFFER_DESC vertex_buffer_desc = {0};
		vertex_buffer_desc.ByteWidth = B3R_STATE.vertex_layout_vertex_sizes[layout] * num_vertices;
		vertex_buffer_desc.Usage = D3D11_USAGE_IMMUTABLE;
		vertex_buffer_desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
		B3R_STATE.device->CreateBuffer(&vertex_buffer_desc, &vertex_data, &mesh->vertex_buffer);
	
		D3D11_SUBRESOURCE_DATA index_data = {indices};
		D3D11_BUFFER_DESC index_buffer_desc = {0};
		index_buffer_desc.ByteWidth = sizeof(uint32_t) * num_indices;
		index_buffer_desc.Usage = D3D11_USAGE_IMMUTABLE;
		index_buffer_desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
		B3R_STATE.device->CreateBuffer(&index_buffer_desc, &index_data, &mesh->index_buffer);
	}
}

static void B3R_MeshDeinit(B3R_Mesh* mesh) {
	if (mesh->vertex_buffer) mesh->vertex_buffer->Release();
	if (mesh->index_buffer) mesh->index_buffer->Release();
	memset(mesh, 0, sizeof(*mesh));
}

static void B3R_UpdateConstants() {
	D3D11_MAPPED_SUBRESOURCE cbuffer_mapped;
	B3R_STATE.dc->Map(B3R_STATE.constant_buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &cbuffer_mapped);
	memcpy(cbuffer_mapped.pData, &B3R_STATE.constants, sizeof(B3R_STATE.constants));
	B3R_STATE.dc->Unmap(B3R_STATE.constant_buffer, 0);
}

static void B3R_BeginDrawing(ID3D11DeviceContext* dc,
	ID3D11RenderTargetView* framebuffer, ID3D11DepthStencilView* depthbuffer,
	HMM_Mat4 clip_from_world, HMM_Vec3 camera_pos)
{
	B3R_STATE.dc = dc;
	
	// D3D11_VIEWPORT viewport = { 0.0f, 0.0f, UI_STATE.window_size.x, UI_STATE.window_size.y, 0.0f, 1.0f };

	B3R_STATE.constants.clip_from_world = clip_from_world;
	B3R_STATE.constants.camera_pos = camera_pos;
	B3R_UpdateConstants();

	dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	dc->VSSetConstantBuffers(0, 1, &B3R_STATE.constant_buffer);
	dc->PSSetConstantBuffers(0, 1, &B3R_STATE.constant_buffer);

	// dc->RSSetViewports(1, &viewport);
	//dc->RSSetState(B3R_STATE.wireframe_raster_state);

	dc->PSSetSamplers(0, 1, &B3R_STATE.sampler_state);

	dc->OMSetRenderTargets(1, &framebuffer, depthbuffer);

	dc->OMSetBlendState(B3R_STATE.blend_state, NULL, 0xffffffff);
}

static void B3R_EndDrawing(void) {
	B3R_STATE.dc = NULL;
}

static void B3R_DrawWireMesh(const B3R_WireMesh* mesh,
	float thickness, float fade_dist_min, float fade_dist_max,
	float r, float g, float b, float a)
{
	B3R_STATE.constants.wire_color[0] = r;
	B3R_STATE.constants.wire_color[1] = g;
	B3R_STATE.constants.wire_color[2] = b;
	B3R_STATE.constants.wire_color[3] = a;
	B3R_STATE.constants.wire_thickness = thickness;
	B3R_STATE.constants.wire_fade_dist_min = fade_dist_min;
	B3R_STATE.constants.wire_fade_dist_max = fade_dist_max;
	B3R_UpdateConstants();

	B3R_STATE.dc->RSSetState(B3R_STATE.raster_state);
	B3R_STATE.dc->VSSetShader(B3R_STATE.wire_vs, NULL, 0);
	B3R_STATE.dc->PSSetShader(B3R_STATE.wire_ps, NULL, 0);
	
	B3R_STATE.dc->VSSetShaderResources(0, 1, &mesh->vertex_structured_buffer_srv);
	
	B3R_STATE.dc->Draw(mesh->num_wire_vertices * 3, 0);
}

static void B3R_DrawMesh(const B3R_Mesh* mesh, B3R_DebugMode debug_mode) {
	B3R_STATE.constants.debug_mode = (int)debug_mode;
	B3R_UpdateConstants();

	if (debug_mode == B3R_DebugMode_Wireframe) {
		B3R_STATE.dc->RSSetState(B3R_STATE.wireframe_raster_state);
	} else {
		B3R_STATE.dc->RSSetState(B3R_STATE.raster_state);
	}
	
	B3R_STATE.dc->VSSetShader(B3R_STATE.vertex_shaders[mesh->vertex_layout], NULL, 0);
	B3R_STATE.dc->PSSetShader(B3R_STATE.pixel_shaders[mesh->vertex_layout], NULL, 0);

	B3R_STATE.dc->IASetInputLayout(B3R_STATE.vertex_layouts[mesh->vertex_layout]);

	UINT stride = B3R_STATE.vertex_layout_vertex_sizes[mesh->vertex_layout];
	UINT offset = 0;
	B3R_STATE.dc->IASetVertexBuffers(0, 1, &mesh->vertex_buffer, &stride, &offset);
	
	B3R_STATE.dc->IASetIndexBuffer(mesh->index_buffer, DXGI_FORMAT_R32_UINT, 0);
	
	B3R_STATE.dc->DrawIndexed(mesh->index_count, 0, 0);
}
