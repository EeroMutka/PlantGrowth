
struct Bud;

struct ShadowMapPoint {
	int x, y, z;
};

struct StemSegment {
	HMM_Vec3 end_point;
	HMM_Quat end_rotation;

	float width;
	//float end_total_lightness;
	float step_scale;

	// for now, let's say that each segment (except the last one) ALWAYS has a lateral bud at the end.
	// @speed: StemSegment could be optimized (removing one Vec3) by removing `base_point` and `base_rotation` from inside the Bud struct.
	Bud* end_lateral;
};

struct Bud { // a bud can have grown into a branch, but we still call it a bud
	uint32_t id;

	HMM_Vec3 base_point;
	HMM_Quat base_rotation;

	float distance_from_root;

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
	uint8_t* shadow_volume;
};

struct PlantParameters {
	uint32_t random_seed = 1;

	float max_age = 1000.f;
	float vigor_scale = 0.05f;

	float ac_base_dist_factor = 1.f;
	float ac_stem_length_factor = 1.f;
	float ac_order_factor = 0.f;
	float ac_overall_factor = 0.01f;
	const Curve* apical_control_curve;
	
	// can we maybe have a curve graph of apical control over maturity?
	//float apical_control = 1.f;
	//int apical_control_maturity;

	int final_apical_control;
};

// ----------------------------------------------------------------------------

void PlantInit(Plant* plant, DS_Arena* arena);

void PlantReset(Plant* plant);

// Returns true if modifications were made
bool PlantDoGrowthIteration(Plant* plant, DS_Arena* temp, const PlantParameters* params);

float GetLightnessAtPoint(Plant* plant, HMM_Vec3 p);
