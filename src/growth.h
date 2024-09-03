
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

struct Stem;
struct StemSegment;

struct StemSegment {
	HMM_Vec3 end_point;
	HMM_Quat end_rotation;
	float scale_ratio;
	float end_lightness_main; // Q_m
	float end_lightness_lateral; // Q_l

	// for now, let's say that each segment (except the last one) ALWAYS has a lateral bud at the end.
	Stem* end_lateral;
};

struct Stem {
	HMM_Vec3 base_point;
	HMM_Quat base_rotation;
	DS_DynArray(StemSegment) segments;
	int order;

	float next_bud_angle_rad; // incremented by golden ratio angle
	//HMM_Vec3 current_direction;
};

#define SHADOW_VOLUME_DIM 64

struct Plant {
	DS_Arena* arena;
	DS_DynArray(Stem*) all_stems;
	Stem* base;

	float shadow_volume_half_extent;
	uint8_t shadow_volume[SHADOW_VOLUME_DIM*SHADOW_VOLUME_DIM*SHADOW_VOLUME_DIM]; // each voxel stores the number of buds inside it
};

struct PlantParameters {
	int _;
};

static Stem* GrowNewStem(Plant* plant, HMM_Vec3 base_point, HMM_Quat base_rotation, float resources, DS_Arena* temp_arena);
static void StemGrow(Plant* plant, Stem* stem, float resources, DS_Arena* temp_arena);

static void IncrementShadowValue(Plant* plant, int x, int y, int z, uint8_t amount) {
	uint8_t* val = &plant->shadow_volume[z*SHADOW_VOLUME_DIM*SHADOW_VOLUME_DIM + y*SHADOW_VOLUME_DIM + x];
	uint8_t val_before = *val;
	*val = val_before + amount;
	if (*val < val_before) *val = 255; // overflow
}

static void IncrementShadowValueClampedSquare(Plant* plant, int min_x, int max_x, int min_y, int max_y, int z, uint8_t amount) {
	min_x = HMM_MIN(HMM_MAX(min_x, 0), SHADOW_VOLUME_DIM - 1);
	max_x = HMM_MIN(HMM_MAX(max_x, 0), SHADOW_VOLUME_DIM - 1);
	min_y = HMM_MIN(HMM_MAX(min_y, 0), SHADOW_VOLUME_DIM - 1);
	max_y = HMM_MIN(HMM_MAX(max_y, 0), SHADOW_VOLUME_DIM - 1);
	z = HMM_MIN(HMM_MAX(z, 0), SHADOW_VOLUME_DIM - 1);

	for (int y = min_y; y <= max_y; y++) {
		for (int x = min_x; x <= max_x; x++) {
			IncrementShadowValue(plant, x, y, z, amount);
		}
	}
}

struct ShadowMapPoint {
	int x, y, z;
};

static ShadowMapPoint PointToShadowMapSpace(Plant* plant, HMM_Vec3 point) {
	int x = (int)((point.X + plant->shadow_volume_half_extent) * SHADOW_VOLUME_DIM);
	int y = (int)((point.Y + plant->shadow_volume_half_extent) * SHADOW_VOLUME_DIM);
	int z = (int)((point.Z) * SHADOW_VOLUME_DIM);
	return {x, y, z};
}

static bool FindOptimalGrowthDirection(Plant* plant, HMM_Vec3 point, HMM_Vec3* out_direction) {
	ShadowMapPoint shadow_p = PointToShadowMapSpace(plant, point);
	int min_x = HMM_MAX(shadow_p.x - 1, 0), max_x = HMM_MIN(shadow_p.x + 1, SHADOW_VOLUME_DIM-1);
	int min_y = HMM_MAX(shadow_p.y - 1, 0), max_y = HMM_MIN(shadow_p.y + 1, SHADOW_VOLUME_DIM-1);
	int min_z = HMM_MAX(shadow_p.z - 1, 0), max_z = HMM_MIN(shadow_p.z + 1, SHADOW_VOLUME_DIM-1);
	
	uint8_t min_val = 255;
	ShadowMapPoint min_val_p = shadow_p;
	int min_val_tiebreaker = -100;

	for (int z = min_z; z <= max_z; z++) {
		for (int y = min_y; y <= max_y; y++) {
			for (int x = min_x; x <= max_x; x++) {
				int local_x = x - shadow_p.x, local_y = y - shadow_p.y, local_z = z - shadow_p.z;
				if (local_x == 0 && local_y == 0 && local_z == 0) continue;
				
				int tiebreaker = local_z - local_x*local_x - local_y*local_y;
				uint8_t val = plant->shadow_volume[z*SHADOW_VOLUME_DIM*SHADOW_VOLUME_DIM + y*SHADOW_VOLUME_DIM + x];

				if (val < min_val || (val == min_val && tiebreaker > min_val_tiebreaker)) {
					min_val = val;
					min_val_p = {x, y, z};
					min_val_tiebreaker = tiebreaker;
				}
			}
		}
	}

	HMM_Vec3 dir = HMM_Vec3{(float)min_val_p.x, (float)min_val_p.y, (float)min_val_p.z} - HMM_Vec3{(float)shadow_p.x, (float)shadow_p.y, (float)shadow_p.z};
	float dir_len = HMM_LenV3(dir);
	if (dir_len == 0.f) return false;
	
	*out_direction = dir / dir_len;
	return true;
}

/*
struct SortElem {
	float sort_key;
	int index;
};

static int SortElemQSortCompare(const void* a, const void* b) {
	return ((SortElem*)a)->sort_key < ((SortElem*)b)->sort_key ? 1 : -1;
}

float DistributionWeight(float i, float n) {
	float w_max = 1.f;
	float w_min = 0.006f;
	float k = 0.5f;
	float kn = k*n;
	if (i > kn) return w_min;
	return w_max - i * (w_max - w_min) / kn;
}*/

static void StemGrow(Plant* plant, Stem* stem, float resources, DS_Arena* temp_arena) {
	// TODO: stop with the distribution crap and just have an apical control parameter
	/*
	DS_DynArray(SortElem) sort_elems = {temp_arena};
	DS_DynArray(float) weights = {temp_arena};
	DS_ArrReserve(&sort_elems, stem->segments.length);
	DS_ArrResizeUndef(&weights, stem->segments.length);
	
	float apical_resources = resources;
	float total_weight_inv = 1.f;

	if (stem->segments.length > 0) {
		for (int i = 0; i < stem->segments.length; i++) {
			StemSegment segment = stem->segments.data[i];
			DS_ArrPush(&sort_elems, {segment.end_point_lightness, i});
		}
		DS_ArrPeek(sort_elems).sort_key = 100000000.f; // always put the apical bud as the first element in the list
		//DS_ArrPeek(sort_elems).sort_key += 0.05f; // bias towards the apical bud

		qsort(sort_elems.data, sort_elems.length, sizeof(SortElem), SortElemQSortCompare);

		if (stem->order <= 0) { // share resources evenly across lateral buds for lateral growth
			float total_weight = 0.f;
			for (int i = 0; i < sort_elems.length; i++) {
				SortElem sort_elem = sort_elems.data[i];
				StemSegment segment = stem->segments.data[sort_elem.index];
		
				//int weight_param_i = i < DS_ArrayCount(weight_params) ? i : DS_ArrayCount(weight_params) - 1;
				//float weight = segment.end_point_lightness * weight_params[weight_param_i];
				float weight = 1.f;//segment.end_point_lightness * DistributionWeight((float)i, (float)sort_elems.length);
		
				weights.data[sort_elem.index] = weight;
				total_weight += weight;
			}
		
			total_weight_inv = 1.f / total_weight;
			apical_resources *= DS_ArrPeek(weights) * total_weight_inv;
		}
	}*/

	int old_segment_count = stem->segments.length;

	// Apical growth.
	// It's important to do this first to let the apical growth "steal" the optimal growth direction.

	DS_DynArray(float) resources_per_segment = {temp_arena};

	float resources_left = resources;
	float apical_control = 0.7f;

	for (int i = 0; i < stem->segments.length - 1; i++) {
		StemSegment segment = stem->segments.data[i];
		
		float v_main_weight = apical_control * segment.end_lightness_main;
		float v_lateral_weight = (1.f - apical_control) * segment.end_lightness_lateral;
		float mult = resources_left / (v_main_weight + v_lateral_weight);
		float v_main = v_main_weight * mult;
		float v_lateral = v_lateral_weight * mult;

		DS_ArrPush(&resources_per_segment, v_lateral);
		
		resources_left = v_main;
	}

	for (float f = resources_left; f > 0.f; f -= 1.f) {
		float scale_ratio = HMM_MIN(f, 1.f);

		HMM_Vec3 stem_end_point = stem->segments.length > 0 ? DS_ArrPeek(stem->segments).end_point : stem->base_point;
		HMM_Quat stem_end_rotation = stem->segments.length > 0 ? DS_ArrPeek(stem->segments).end_rotation : stem->base_rotation;

		HMM_Vec3 optimal_direction = {0, 0, 1};
		bool optimal_direction_ok = FindOptimalGrowthDirection(plant, stem_end_point, &optimal_direction);
		
		HMM_Vec3 old_dir = HMM_RotateV3({0, 0, 1}, stem_end_rotation);
		HMM_Vec3 new_dir = HMM_LerpV3(old_dir, 0.2f * scale_ratio, optimal_direction);
		new_dir = HMM_NormV3(new_dir);

		HMM_Quat rotator_to_new_dir = HMM_ShortestRotationBetweenUnitVectors(old_dir, new_dir, {0, 0, 1});
		
		HMM_Quat new_end_rotation = rotator_to_new_dir * stem_end_rotation;

		// the step size needs to be roughly the same as the voxel size!
		float step_length = scale_ratio / SHADOW_VOLUME_DIM;
		HMM_Vec3 new_end_point = stem_end_point + step_length * new_dir;

		ShadowMapPoint shadow_p = PointToShadowMapSpace(plant, new_end_point);

		if (optimal_direction_ok &&
			shadow_p.x >= 0 && shadow_p.x < SHADOW_VOLUME_DIM &&
			shadow_p.y >= 0 && shadow_p.y < SHADOW_VOLUME_DIM &&
			shadow_p.z >= 0 && shadow_p.z < SHADOW_VOLUME_DIM)
		{
			if (stem->segments.length == 0 || DS_ArrPeek(stem->segments).scale_ratio + scale_ratio > 1.f) {
				StemSegment new_segment{};
				DS_ArrPush(&stem->segments, new_segment);
				IncrementShadowValue(plant, shadow_p.x, shadow_p.y, shadow_p.z, 1);
				IncrementShadowValueClampedSquare(plant, shadow_p.x, shadow_p.x, shadow_p.y, shadow_p.y, shadow_p.z-1, 1);
				IncrementShadowValueClampedSquare(plant, shadow_p.x, shadow_p.x, shadow_p.y, shadow_p.y, shadow_p.z-2, 1);
				IncrementShadowValueClampedSquare(plant, shadow_p.x - 1, shadow_p.x + 1, shadow_p.y - 1, shadow_p.y + 1, shadow_p.z-3, 1);
				IncrementShadowValueClampedSquare(plant, shadow_p.x - 1, shadow_p.x + 1, shadow_p.y - 1, shadow_p.y + 1, shadow_p.z-4, 1);
				//IncrementShadowValueClampedSquare(plant, shadow_p.x - 2, shadow_p.x + 2, shadow_p.y - 2, shadow_p.y + 2, shadow_p.z-4, 255);
			}

			StemSegment* last_segment = DS_ArrPeekPtr(stem->segments);
			last_segment->end_point = new_end_point;
			last_segment->end_rotation = new_end_rotation;
			last_segment->scale_ratio += scale_ratio;

		}
	}
	
	//if (stem->order <= 0) {
	for (int i = 0; i < old_segment_count - 1; i++) { // NOTE: we don't give the last segment even the possibility of having an end lateral
		StemSegment segment = stem->segments.data[i];
		float segment_resources = DS_ArrGet(resources_per_segment, i);
		if (segment_resources < 0.00001f) continue;

		if (segment.end_lateral) {
			StemGrow(plant, segment.end_lateral, segment_resources, temp_arena);
		}
		else {
			HMM_Quat new_bud_rot = {0, 0, 0, 1};
			new_bud_rot = HMM_QFromAxisAngle_RH({1.f, 0.f, 0.f}, HMM_AngleDeg(60.f)) * new_bud_rot;
			new_bud_rot = HMM_QFromAxisAngle_RH({0.f, 0.f, 1.f}, HMM_AngleRad(stem->next_bud_angle_rad)) * new_bud_rot;
			new_bud_rot = segment.end_rotation * new_bud_rot;

			const float golden_ratio_rad_increment = 1.61803398875f * 3.1415926f * 2.f;
			stem->next_bud_angle_rad += golden_ratio_rad_increment;
			stem->segments.data[i].end_lateral = GrowNewStem(plant, segment.end_point, new_bud_rot, segment_resources, temp_arena);
			stem->segments.data[i].end_lateral->order = stem->order + 1;
		}
	}
	//}
}

static Stem* GrowNewStem(Plant* plant, HMM_Vec3 base_point, HMM_Quat base_rotation, float resources, DS_Arena* temp_arena) {
	Stem* stem = DS_New(Stem, plant->arena);
	DS_ArrInit(&stem->segments, plant->arena);
	stem->base_point = base_point;
	stem->base_rotation = base_rotation;
	
	StemGrow(plant, stem, resources, temp_arena);
	
	DS_ArrPush(&plant->all_stems, stem);
	return stem;
}

static float PlantCalculateLight(Plant* plant, Stem* stem) {
	float lightness_main = 0.f;

	for (int i = stem->segments.length - 1; i >= 0; i--) {
		StemSegment* segment = &stem->segments.data[i];
		segment->end_lightness_lateral = 0.f;

		if (segment->end_lateral) {
			segment->end_lightness_lateral = PlantCalculateLight(plant, segment->end_lateral);
		}
		else {
			ShadowMapPoint shadow_p = PointToShadowMapSpace(plant, segment->end_point);
			uint8_t val = plant->shadow_volume[shadow_p.z*SHADOW_VOLUME_DIM*SHADOW_VOLUME_DIM + shadow_p.y*SHADOW_VOLUME_DIM + shadow_p.x];
			
			segment->end_lightness_lateral = 1.f - 200.f*(float)(val - 1.f) / 255.f;
			segment->end_lightness_main = lightness_main + segment->end_lightness_lateral;
		}

		lightness_main = segment->end_lightness_main + segment->end_lightness_lateral;
	}

	return lightness_main;
}

static void PlantDoGrowthIteration(Plant* plant, const PlantParameters* params, DS_Arena* temp_arena) {
	float resource_scale = 1.f;
	if (plant->base) {
		float total_light = PlantCalculateLight(plant, plant->base);
		StemGrow(plant, plant->base, resource_scale*total_light, temp_arena);
	}
	else {
		HMM_Vec3 start_point = {0.5f/SHADOW_VOLUME_DIM, 0.5f/SHADOW_VOLUME_DIM, 0.5f/SHADOW_VOLUME_DIM};
		plant->base = GrowNewStem(plant, start_point, {0, 0, 0, 1}, resource_scale, temp_arena);
	}
}

#if 0
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
	float step_size = 0.04f;
	float age = 1.f; // age in years
	float thickness = 0.1f;
	float drop_frequency = 0.05f;
	float drop_leaf_growth_ratio = 1.f;
	float drop_apical_growth_ratio = 0.5f;
	float axillary_drop_frequency = 0.04f;
	float axillary_drop_min_t = 0.3f;
	float axillary_drop_max_t = 0.3f;
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

static void AddStem(Plant* plant, uint32_t id, HMM_Vec3 base_point, HMM_Quat base_rotation, float age, bool is_leaf, const PlantParameters* params) {
	Stem stem{};
	DS_ArrInit(&stem.points, plant->arena);

	stem.end_leaf_expand = 0.f;
	if (is_leaf) {
		stem.end_leaf_expand = Approach(age, 1.f, params->leaf_growth_speed);
		if (stem.end_leaf_expand == 0.f) stem.end_leaf_expand = 0.00001f;
	}

	// sometimes, the growth direction can change. But it should usually tend back towards the optimal growth direction.
	HMM_Vec3 optimal_growth_direction = {0, 0, 1}; // this is the direction where the most sunlight comes from.

	//float max_growth = growth_scale*params->growth_scale;

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

	float default_step_dt = params->step_size;

	float completed_at_age = is_leaf ? 0.2f : 1000000.f;
	float clamped_age = HMM_MIN(age, completed_at_age);

	int num_segments = (int)ceilf(clamped_age / default_step_dt);
	if (num_segments < 1) num_segments = 1; // TODO: fix this

	float last_step_duration_ratio = (clamped_age - ((float)num_segments - 1) * default_step_dt) / default_step_dt;

	//float total_step_size = (float)num_points * single_step_size;
	//printf("last_step_size: %f\n", last_step_duration_ratio);

	// Ok, we don't actually need to increase the segment lengths. We can instead bias the total growth age.

	RandomGenerator rng = {id, 1234};

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
		
		// if we want to slow down growth over age, then we should do it here.
		step_length += params->equal_growth * Decelerate(segment_end_point_age, params->equal_growth_deceler) * step_dt;

		if (is_valid_segment && !is_leaf) {
			// drop a new leaf and a new stem from the bud?
			// TODO: the next step is to spawn new axilary buds that can start growing into their own stems.

			if (segment_start_point_t - last_leaf_drop_growth > params->drop_frequency) {
				// The drop pitch generally starts out as 0 and as the apical meristem grows in width, it pushes it out and the pitch increases.
				// Also, as the new stem grows and becomes more heavy, the pitch increases further.
				float leaf_drop_pitch = Approach(segment_start_point_age, params->leaf_drop_pitch, params->leaf_drop_pitch_speed);
				float leaf_lives_up_to_age = RandomFloat(&rng, 0.5f, 2.f);
				
				if (segment_start_point_age < leaf_lives_up_to_age) {
					HMM_Quat new_leaf_rot = {0, 0, 0, 1};
					new_leaf_rot = HMM_QFromAxisAngle_RH({1.f, 0.f, 0.f}, HMM_AngleDeg(leaf_drop_pitch)) * new_leaf_rot;
					new_leaf_rot = HMM_QFromAxisAngle_RH({0.f, 0.f, 1.f}, HMM_AngleRad(end_point_drop_leaf_yaw)) * new_leaf_rot;
					new_leaf_rot = end_rotation * new_leaf_rot;
					AddStem(plant, id + i, end_point, new_leaf_rot, segment_start_point_age * params->drop_leaf_growth_ratio, true, params);
				}

				//float start_point_growth_ratio = segment_start_point_t / age;
				if (segment_start_point_t > params->axillary_drop_min_t &&
					segment_start_point_t < age - params->axillary_drop_max_t &&
					segment_start_point_t - last_axillary_drop_growth > params->axillary_drop_frequency)
				{
					float axillary_bud_age = segment_start_point_age - params->axillary_price;
					if (axillary_bud_age > 0.f) {
						HMM_Quat new_axillary_bud_rot = {0, 0, 0, 1};
						new_axillary_bud_rot = HMM_QFromAxisAngle_RH({1.f, 0.f, 0.f}, HMM_AngleDeg(params->axillary_drop_pitch)) * new_axillary_bud_rot;
						new_axillary_bud_rot = HMM_QFromAxisAngle_RH({0.f, 0.f, 1.f}, HMM_AngleRad(end_point_drop_axillary_yaw)) * new_axillary_bud_rot;
						new_axillary_bud_rot = end_rotation * new_axillary_bud_rot;
						AddStem(plant, id + i, end_point, new_axillary_bud_rot, axillary_bud_age * params->drop_apical_growth_ratio, false, params);
					}
			
					end_point_drop_axillary_yaw += golden_ratio_rad_increment;
					last_axillary_drop_growth = segment_start_point_t;
				}

				end_point_drop_leaf_yaw += golden_ratio_rad_increment;
				last_leaf_drop_growth = segment_start_point_t;
			}
		}
		
		float this_thickness = 0.001f;
		//this_thickness = i % 2 == 0 ? 0.001f : 0.002f;
		if (!is_leaf) this_thickness += segment_start_point_age * params->thickness * 0.01f;

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

	AddStem(&plant, 0, {0, 0, 0}, {0, 0, 0, 1}, params->age, false, params);

	return plant;
}

#endif