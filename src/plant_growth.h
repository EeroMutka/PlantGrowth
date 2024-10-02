
// -- Constants ---------------------------------------------------------------
#define SHADOW_VOLUME_SIZE 64
// ----------------------------------------------------------------------------

struct Bud;

struct ShadowMapPoint {
	int x, y, z;
};

struct StemSegment {
	HMM_Vec3 end_point;
	HMM_Quat end_rotation;

	float width;
	float end_total_lightness;
	float step_scale;

	// for now, let's say that each segment (except the last one) ALWAYS has a lateral bud at the end.
	// @speed: StemSegment could be optimized (removing one Vec3) by removing `base_point` and `base_rotation` from inside the Bud struct.
	Bud* end_lateral;
};

struct Bud { // a bud can have grown into a branch, but we still call it a bud
	uint32_t id;

	HMM_Vec3 base_point;
	HMM_Quat base_rotation;

	ShadowMapPoint end_sample_point;

	DS_DynArray(StemSegment) segments;

	bool is_dead;

	float leaf_growth;

	int order;

	float total_lightness;
	float max_lightness; // max lightness of any single bud
	float next_bud_angle_rad; // incremented by golden ratio angle
};

struct Plant {
	DS_Arena* arena;
	Bud root;
	uint32_t next_bud_id;
	int age;
	float shadow_volume_half_extent;
	uint8_t shadow_volume[SHADOW_VOLUME_SIZE*SHADOW_VOLUME_SIZE*SHADOW_VOLUME_SIZE]; // each voxel stores the number of buds inside it
};

struct PlantParameters {
	int _;
};

// ----------------------------------------------------------------------------

void PlantInit(Plant* plant, DS_Arena* arena);

bool PlantDoGrowthIteration(Plant* plant, DS_Arena* temp, const PlantParameters* params);

float GetLightnessAtPoint(Plant* plant, HMM_Vec3 p);
