
struct StemPoint {
	HMM_Vec3 point;
	float thickness;
	HMM_Quat rotation; // +Z is up
};

struct Stem {
	DS_DynArray(StemPoint) points;
	float end_leaf_expand; // 0 if the end is a bud, between 0 and 1 means a ratio of how grown a leaf is
};

struct Plant {
	DS_Arena* arena;
	DS_DynArray(Stem) stems;
};

struct PlantParameters {
	float step_size = 0.02f;
	float age = 1.f; // age in years
	float growth_scale = 3.5f;
	float growth_speed = 0.1f;
	float thickness = 0.1f;
	float drop_frequency = 0.05f;
	float drop_leaf_growth_ratio = 0.07f;
	float drop_apical_growth_ratio = 0.6f;
	float axillary_drop_frequency = 0.15f;
	float axillary_price = 0.f;
	float axillary_drop_pitch = 70.f;
	float pitch_twist = 10.f;
	float yaw_twist = 10.f;
	float leaf_drop_pitch = 60.f;
	float leaf_drop_pitch_speed = 0.2f;
	float apical_growth = 0.02f;
	float equal_growth = 1.f;
	float equal_growth_deceler = 0.4f;
	float leaf_growth_speed = 0.3f;
};

// Decelerate the function `y = x` with the strength of `f`.
// The result comes to a full stop at `x = 1 / f` when `f > 0`.
// https://www.desmos.com/calculator/ncskawbzlg
static float Decelerate(float x, float f) {
	if (f > 0.f) {
		float f_inv = 1.f / f;
		float a = HMM_MIN(x, f_inv) / f_inv - 1.f;
		float a2 = a*a;
		return 0.25f * f_inv * (1.f - a2*a2);
	}
	return x;
}

// https://www.desmos.com/calculator/errv2lr33g
static float Approach(float x, float target, float speed) {
	if (speed > 0.f) {
		float speed_inv = 1.f/speed;
		float a = speed*HMM_MIN(x, speed_inv) - 1.f;
		float a2 = a*a;
		return target * (1.f - a2*a2);
	}
	return 0.f;
}

struct RandomGenerator {
	uint64_t state, inc;
};

uint32_t RandomU32(RandomGenerator* rng) {
	// Minimal PCG32 code / (c) 2014 M.E. O'Neill / pcg-random.org
	// Licensed under Apache License 2.0 (NO WARRANTY, etc. see website)
	uint64_t oldstate = rng->state;
	rng->state = oldstate * 6364136223846793005ULL + (rng->inc|1);
	uint32_t xorshifted = (uint32_t)(((oldstate >> 18u) ^ oldstate) >> 27u);
	uint32_t rot = oldstate >> 59u;
	return (xorshifted >> rot) | (xorshifted << ((0 - rot) & 31));
}

static float RandomFloat(RandomGenerator* rng, float min, float max) {
	return min + (RandomU32(rng) / (float)UINT_MAX) * (max - min);
}

static void AddStem(Plant* plant, uint32_t id, HMM_Vec3 base_point, HMM_Quat base_rotation, float age, float growth_scale, bool is_leaf, const PlantParameters* params) {
	Stem stem{};
	DS_ArrInit(&stem.points, plant->arena);

	stem.end_leaf_expand = 0.f;
	if (is_leaf) {
		stem.end_leaf_expand = Approach(age, 1.f, params->leaf_growth_speed);
		if (stem.end_leaf_expand == 0.f) stem.end_leaf_expand = 0.00001f;
	}

	// sometimes, the growth direction can change. But it should usually tend back towards the optimal growth direction.
	HMM_Vec3 optimal_growth_direction = {0, 0, 1}; // this is the direction where the most sunlight comes from.

	float growth = Approach(age, growth_scale*params->growth_scale, params->growth_speed); // each stem stops growing at some point
	float age_over_growth = age / growth;

	//if (is_leaf) {
	//	clamped_age = Decelerate(clamped_age, params->leaf_age_deceler);
	//}
	const float golden_ratio_rad_increment = 1.61803398875f * 3.1415926f * 2.f;

	HMM_Vec3 end_point = base_point;
	HMM_Quat end_rotation = base_rotation;
	float end_point_drop_leaf_yaw = golden_ratio_rad_increment; // rotate the yaw by golden ratio each time a new leaf is dropped by the bud
	float end_point_drop_axillary_yaw = golden_ratio_rad_increment;

	// Weird things:
	// - why aren't all stems growing equally fast?
	// - why do new leaves and stems only start growing from the top?

	// The apical bud is very special thing. Where there is an apical bud, the stem just keeps on growing and growing to push the apical bud out and to give space for new leaves.
	// If it's just a stem with a leaf at the end, and the leaf is in a good position, then the stem doesn't need to waste resources by growing further.

	// it's all about resource allocation. Where would it make sense to grow a new apical meristem?

	float last_leaf_drop_growth = 0.f;
	float last_axillary_drop_growth = 0.f;

	float default_step_dg = params->step_size;

	int num_segments = (int)ceilf(growth / default_step_dg);
	if (num_segments < 1) num_segments = 1; // TODO: fix this

	//float total_step_size = (float)num_points * single_step_size;
	float last_step_duration_ratio = (growth - ((float)num_segments - 1) * default_step_dg) / default_step_dg;
	//printf("last_step_size: %f\n", last_step_duration_ratio);

	// Ok, we don't actually need to increase the segment lengths. We can instead bias the total growth age.

	RandomGenerator rng = {id, 1234};

	for (int i = 0; i <= num_segments; i++) {
		// we only want to adjust the last point step size!
		bool is_valid_segment = i != num_segments;
		
		float step_duration_ratio = i == num_segments - 1 ? last_step_duration_ratio : 1.f;
		
		float step_dg = default_step_dg * step_duration_ratio;
		float step_length = params->apical_growth * step_dg;
		
		float segment_start_point_growth = default_step_dg * (float)i;
		float segment_start_point_growth_age = growth - segment_start_point_growth;

		float segment_end_point_growth = segment_start_point_growth + step_dg;
		float segment_end_point_growth_age = growth - segment_end_point_growth;
			
		step_length += params->equal_growth * Decelerate(segment_end_point_growth_age, params->equal_growth_deceler) * step_dg;

		float segment_start_point_age = segment_start_point_growth_age * age_over_growth;

		if (is_valid_segment && !is_leaf) {
			// drop a new leaf and a new stem from the bud?
			// TODO: the next step is to spawn new axilary buds that can start growing into their own stems.

			if (segment_start_point_growth - last_leaf_drop_growth > params->drop_frequency) {
				// The drop pitch generally starts out as 0 and as the apical meristem grows in width, it pushes it out and the pitch increases.
				// Also, as the new stem grows and becomes more heavy, the pitch increases further.
				float leaf_drop_pitch = Approach(segment_start_point_growth_age, params->leaf_drop_pitch, params->leaf_drop_pitch_speed);
				float leaf_lives_up_to_age = RandomFloat(&rng, 4.f, 10.f);
				
				if (segment_start_point_age < leaf_lives_up_to_age) {
					HMM_Quat new_leaf_rot = {0, 0, 0, 1};
					new_leaf_rot = HMM_QFromAxisAngle_RH({1.f, 0.f, 0.f}, HMM_AngleDeg(leaf_drop_pitch)) * new_leaf_rot;
					new_leaf_rot = HMM_QFromAxisAngle_RH({0.f, 0.f, 1.f}, HMM_AngleRad(end_point_drop_leaf_yaw)) * new_leaf_rot;
					new_leaf_rot = end_rotation * new_leaf_rot;
					AddStem(plant, id + i, end_point, new_leaf_rot, segment_start_point_age, params->drop_leaf_growth_ratio, true, params);
				}

				// hmm okay... why, in some trees, d

				if (segment_start_point_growth - last_axillary_drop_growth > params->axillary_drop_frequency) {
					float axillary_bud_age = segment_start_point_age - params->axillary_price;
					if (axillary_bud_age > 0.f) {
						HMM_Quat new_axillary_bud_rot = {0, 0, 0, 1};
						new_axillary_bud_rot = HMM_QFromAxisAngle_RH({1.f, 0.f, 0.f}, HMM_AngleDeg(params->axillary_drop_pitch)) * new_axillary_bud_rot;
						new_axillary_bud_rot = HMM_QFromAxisAngle_RH({0.f, 0.f, 1.f}, HMM_AngleRad(end_point_drop_axillary_yaw)) * new_axillary_bud_rot;
						new_axillary_bud_rot = end_rotation * new_axillary_bud_rot;
						AddStem(plant, id + i, end_point, new_axillary_bud_rot, axillary_bud_age, growth_scale * params->drop_apical_growth_ratio, false, params);
					}
			
					end_point_drop_axillary_yaw += golden_ratio_rad_increment;
					last_axillary_drop_growth = segment_start_point_growth;
				}

				end_point_drop_leaf_yaw += golden_ratio_rad_increment;
				last_leaf_drop_growth = segment_start_point_growth;
			}
		}
		
		float this_thickness = 0.001f;
		if (!is_leaf) this_thickness += segment_start_point_age * params->thickness * 0.01f;

		DS_ArrPush(&stem.points, {end_point, this_thickness, end_rotation});
		
		if (is_valid_segment) {
			HMM_Quat pitch_step_rotator = HMM_QFromAxisAngle_RH({1.f, 0.f, 0.f}, HMM_AngleDeg(params->pitch_twist) * step_dg);
			HMM_Quat yaw_step_rotator = HMM_QFromAxisAngle_RH({0.f, 0.f, 1.f}, HMM_AngleDeg(params->yaw_twist) * step_dg);

			// we could rotate the growth direction using just
			end_rotation = yaw_step_rotator * end_rotation;
			end_rotation = pitch_step_rotator * end_rotation;

			HMM_Vec3 end_up_direction = HMM_RotateV3({0, 0, 1}, end_rotation);

			// so the end point can split at any time into a new growth source. This is the most likely place for new growth sources.

			// rotate growth direction randomly using simplex noise?
			end_point += step_length * end_up_direction;

			//if (is_last) {
			//	DS_ArrPush(&stem.points, {end_point, this_thickness, end_rotation});
			//	break;
			//}
		}
	}
	
	assert(stem.points.length >= 2);
	DS_ArrPush(&plant->stems, stem);
}

static Plant GeneratePlant(DS_Arena* arena, const PlantParameters* params) {
	Plant plant{};
	plant.arena = arena;
	DS_ArrInit(&plant.stems, arena);

	AddStem(&plant, 0, {0, 0, 0}, {0, 0, 0, 1}, params->age, 1.f, false, params);

	return plant;
}
