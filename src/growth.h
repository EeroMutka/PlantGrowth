
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
	float points_per_meter = 30.f;
	float age = 0.4f; // age in years
	float pitch_twist = 10.f; // total pitch twist per meter in degrees
	float yaw_twist = 10.f; // total yaw twist per meter in degrees
	float drop_pitch = 50.f;
};

static void AddStem(Plant* plant, HMM_Vec3 base_point, HMM_Quat base_rotation, float age, bool is_leaf, const PlantParameters* params) {
	Stem stem{};
	DS_ArrInit(&stem.points, plant->arena);

	stem.end_leaf_expand = 0.f;
	if (is_leaf) {
		stem.end_leaf_expand = HMM_MIN(age*5.f, 1.f);
	}

	// sometimes, the growth direction can change. But it should usually tend back towards the optimal growth direction.
	HMM_Vec3 optimal_growth_direction = {0, 0, 1}; // this is the direction where the most sunlight comes from.

	float clamped_age = age; // limit the age of leaves
	if (is_leaf) clamped_age = HMM_MIN(clamped_age, 0.02f); // limit the growth (thickness + length) of leaves
	float desired_length = clamped_age;

	HMM_Vec3 end_point = base_point;
	HMM_Quat end_rotation = base_rotation;
	float end_point_drop_yaw = 0.f; // rotate the yaw by golden ratio each time a new leaf is dropped by the bud
	float end_point_t = 0.f;

	// We want to simulate the apical bud! Ok so we always have an apical bud (a 3d mesh).
	// We also match the leaf mesh to fit the apical bud exactly, so that the transition from an apical bud into a leaf is perfectly seamless.

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

	const float drop_density = 0.1f;

	float distance_since_last_bud = 0.f;

	for (;;) {
		float step_size = 1.f / params->points_per_meter;
		bool is_last = end_point_t + step_size > desired_length;
		if (is_last) {
			step_size = desired_length - end_point_t;
		}
		
		float point_age = clamped_age * (1.f - end_point_t / desired_length);

		float this_thickness = HMM_Lerp(0.0015f, HMM_MIN(point_age, 1.f), 0.005f); // quick test

		// drop a new leaf and a new stem from the bud?
		if (!is_leaf && distance_since_last_bud > drop_density && point_age > 0.f) {
			// rotate the new stem (pitch).
			float golden_ratio_rad_increment = 1.61803398875f * 3.1415926f * 2.f;

			// The drop pitch generally starts out as 0 and as the apical meristem grows in width, it pushes it out and the pitch increases.
			// Also, as the new stem grows and becomes more heavy, the pitch increases further.
			float drop_pitch = params->drop_pitch * HMM_MIN(point_age * 2.f, 1.f);

			HMM_Quat new_stem_rot = {0, 0, 0, 1};
			new_stem_rot = HMM_QFromAxisAngle_RH({1.f, 0.f, 0.f}, HMM_AngleDeg(drop_pitch)) * new_stem_rot;
			new_stem_rot = HMM_QFromAxisAngle_RH({0.f, 0.f, 1.f}, HMM_AngleRad(end_point_drop_yaw)) * new_stem_rot;
			new_stem_rot = end_rotation * new_stem_rot;

			AddStem(plant, end_point, new_stem_rot, point_age * 0.5f, true, params);
				
			end_point_drop_yaw += golden_ratio_rad_increment;
		}

		DS_ArrPush(&stem.points, {end_point, this_thickness, end_rotation});

		HMM_Quat pitch_step_rotator = HMM_QFromAxisAngle_RH({1.f, 0.f, 0.f}, HMM_AngleDeg(params->pitch_twist) * step_size);
		HMM_Quat yaw_step_rotator = HMM_QFromAxisAngle_RH({0.f, 0.f, 1.f}, HMM_AngleDeg(params->yaw_twist) * step_size);

		// we could rotate the growth direction using just
		end_rotation = yaw_step_rotator * end_rotation;
		end_rotation = pitch_step_rotator * end_rotation;

		HMM_Vec3 end_up_direction = HMM_RotateV3({0, 0, 1}, end_rotation);

		// so the end point can split at any time into a new growth source. This is the most likely place for new growth sources.

		// rotate growth direction randomly using simplex noise?
		end_point += step_size * end_up_direction;
		end_point_t += step_size;
		distance_since_last_bud += step_size;

		if (is_last) {
			DS_ArrPush(&stem.points, {end_point, this_thickness, end_rotation});
			break;
		}
	}
	
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
