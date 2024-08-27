
struct StemPoint {
	HMM_Vec3 point;
	float thickness;
};

struct Stem {
	DS_DynArray(StemPoint) points;
};

struct Plant {
	DS_DynArray(Stem) stems;
};

struct PlantParameters {
	float points_per_meter = 10.f;
	float age = 1.f; // age in years
	float pitch_twist = 10.f; // total pitch twist per meter in degrees
	float yaw_twist = 10.f; // total yaw twist per meter in degrees
	//float pitch_rotate
};

static Plant GeneratePlant(DS_Arena* arena, const PlantParameters* params) {
	Plant plant{};
	DS_ArrInit(&plant.stems, arena);

	Stem stem{};
	DS_ArrInit(&stem.points, arena);

	float length = params->age; // grow one meter per year
	
	int stem_points_count = (int)ceilf(params->points_per_meter * length);
	if (stem_points_count < 2) stem_points_count = 2;

	// sometimes, the growth direction can change. But it should usually tend back towards the optimal growth direction.
	//HMM_Vec3 growth_direction = {0, 0, 1};
	HMM_Vec3 optimal_growth_direction = {0, 0, 1}; // this is the direction where the most sunlight comes from.

	HMM_Vec3 end_point = {0, 0, 0};
	HMM_Quat end_rotation = {0, 0, 0, 1};

	//float step_size_ratio = 1.f / ((float)stem_points_count - 1.f);

	HMM_Quat pitch_step_rotator = HMM_QFromAxisAngle_RH({1.f, 0.f, 0.f}, HMM_AngleDeg(params->pitch_twist) / params->points_per_meter);
	HMM_Quat yaw_step_rotator = HMM_QFromAxisAngle_RH({0.f, 0.f, 1.f}, HMM_AngleDeg(params->yaw_twist) / params->points_per_meter);

	for (int i = 0; i < stem_points_count; i++) {
		DS_ArrPush(&stem.points, {end_point, 0.1f});

		//HMM_QFromAxisAngle_RH(HMM_V3(
		
		// we could rotate the growth direction using just
		end_rotation = yaw_step_rotator * end_rotation;
		end_rotation = pitch_step_rotator * end_rotation;

		HMM_Vec3 end_up_direction = HMM_RotateV3({0, 0, 1}, end_rotation);
		//HMM_Rotate_RH(

		// rotate growth direction randomly using simplex noise?
		end_point += (1.f / params->points_per_meter) * end_up_direction;
	}
	//DS_ArrPush(&stem.points, {{0.5f, 0.5f, 2.f}, 0.1f});
	//DS_ArrPush(&stem.points, {{0.7f, 0.8f, 3.f}, 0.1f});
	
	DS_ArrPush(&plant.stems, stem);
	return plant;
}
