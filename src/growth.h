
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
	float points_per_meter = 100.f;
	float age = 0.5f; // age in years
	float leaf_age_ratio = 1.f;
	float leaf_age_deceler = 0.6f;
	float drop_frequency = 0.03f;
	float pitch_twist = 50.f;
	float yaw_twist = 50.f;
	float drop_pitch = 50.f;
	float drop_pitch_deceler = 0.2f;
	float apical_growth = 0.0f;
	float equal_growth = 1.f;
	float equal_growth_deceler = 0.3f;
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

static void AddStem(Plant* plant, HMM_Vec3 base_point, HMM_Quat base_rotation, float age, bool is_leaf, const PlantParameters* params) {
	Stem stem{};
	DS_ArrInit(&stem.points, plant->arena);

	stem.end_leaf_expand = 0.f;
	if (is_leaf) {
		// The following slowly approaches 1
		stem.end_leaf_expand = Decelerate(age, params->leaf_growth_speed) * params->leaf_growth_speed * 4.f;
		if (stem.end_leaf_expand == 0.f) stem.end_leaf_expand = 0.00001f;
	}

	// sometimes, the growth direction can change. But it should usually tend back towards the optimal growth direction.
	HMM_Vec3 optimal_growth_direction = {0, 0, 1}; // this is the direction where the most sunlight comes from.

	float clamped_age = age; // limit the age of leaves
	if (is_leaf) {
		clamped_age = Decelerate(clamped_age, params->leaf_age_deceler);
	}

	HMM_Vec3 end_point = base_point;
	HMM_Quat end_rotation = base_rotation;
	float end_point_drop_yaw = 0.f; // rotate the yaw by golden ratio each time a new leaf is dropped by the bud

	// Weird things:
	// - why aren't all stems growing equally fast?
	// - why do new leaves and stems only start growing from the top?

	// The apical bud is very special thing. Where there is an apical bud, the stem just keeps on growing and growing to push the apical bud out and to give space for new leaves.
	// If it's just a stem with a leaf at the end, and the leaf is in a good position, then the stem doesn't need to waste resources by growing further.

	// So, in my model, there is a stem. At the end of a stem, there is a leaf, or a group of undeveloped leaves (an apical bud).
	// If the end is a leaf, then the stem grows to a fixed length and stops growing. If the end is a group of leaves, then the stem might decide to grow without limits,
	// or to not grow.
	// 
	// When a stem is growing, the apical bud every so often gives away a leaf. After reaching a good age, the bud will stop growing new leaves, and will just in its last leaf. Visually, a "dying" bud would look the best if it ends in a tight cluster of 2 or more leaves at the end.
	// 
	// At any point, anywhere in the tree, a new apical bud may start growing. A new one will most likely start growing out from an already active apical bud. But it could spawn anywhere (most likely into a "juicy" spot).
	// hmm... so a new apical bud most likely starts growing at the top.

	float last_bud_t = 0.f;

	float default_step_dt = 0.02f; // we decide some default step size

	int num_segments = (int)ceilf(clamped_age / default_step_dt);
	if (num_segments < 1) num_segments = 1; // TODO: fix this

	//float total_step_size = (float)num_points * single_step_size;
	float last_step_duration_ratio = (clamped_age - ((float)num_segments - 1) * default_step_dt) / default_step_dt;
	//printf("last_step_size: %f\n", last_step_duration_ratio);

	// Ok, we don't actually need to increase the segment lengths. We can instead bias the total growth age.

	for (int i = 0; i <= num_segments; i++) {
		// we only want to adjust the last point step size!
		bool is_valid_segment = i != num_segments;
		
		float step_duration_ratio = i == num_segments - 1 ? last_step_duration_ratio : 1.f;
		
		float step_dt = default_step_dt * step_duration_ratio;
		float step_length = params->apical_growth * step_dt;
		
		float segment_start_point_t = default_step_dt * (float)i;
		float segment_start_point_age = clamped_age - segment_start_point_t;

		float segment_end_point_t = segment_start_point_t + step_dt;
		float segment_end_point_age = clamped_age - segment_end_point_t;
			
		step_length += params->equal_growth * Decelerate(segment_end_point_age, params->equal_growth_deceler) * step_dt;

		if (is_valid_segment) {
			
			// drop a new leaf and a new stem from the bud?
			if (!is_leaf && segment_start_point_t - last_bud_t > params->drop_frequency) {
				// rotate the new stem (pitch).
				float golden_ratio_rad_increment = 1.61803398875f * 3.1415926f * 2.f;
			
				// The drop pitch generally starts out as 0 and as the apical meristem grows in width, it pushes it out and the pitch increases.
				// Also, as the new stem grows and becomes more heavy, the pitch increases further.
				float drop_pitch = params->drop_pitch * Decelerate(segment_start_point_age, params->drop_pitch_deceler); //params->drop_pitch * HMM_MIN(segment_start_point_age * 2.f, 1.f);
			
				HMM_Quat new_stem_rot = {0, 0, 0, 1};
				new_stem_rot = HMM_QFromAxisAngle_RH({1.f, 0.f, 0.f}, HMM_AngleDeg(drop_pitch)) * new_stem_rot;
				new_stem_rot = HMM_QFromAxisAngle_RH({0.f, 0.f, 1.f}, HMM_AngleRad(end_point_drop_yaw)) * new_stem_rot;
				new_stem_rot = end_rotation * new_stem_rot;
			
				AddStem(plant, end_point, new_stem_rot, segment_start_point_age * params->leaf_age_ratio, true, params);
			
				end_point_drop_yaw += golden_ratio_rad_increment;
				last_bud_t = segment_start_point_t;
			}
		}

		float this_thickness = step_length*0.1f + 0.001f;//segment_start_point_age * 0.002f + 0.002f;
		DS_ArrPush(&stem.points, {end_point, this_thickness, end_rotation});
		
		if (is_valid_segment) {
			HMM_Quat pitch_step_rotator = HMM_QFromAxisAngle_RH({1.f, 0.f, 0.f}, HMM_AngleDeg(params->pitch_twist) * step_dt);
			HMM_Quat yaw_step_rotator = HMM_QFromAxisAngle_RH({0.f, 0.f, 1.f}, HMM_AngleDeg(params->yaw_twist) * step_dt);

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

	float desired_length = params->age;
	AddStem(&plant, {0, 0, 0}, {0, 0, 0, 1}, desired_length, false, params);

	return plant;
}
