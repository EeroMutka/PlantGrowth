
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

static RandomGenerator RandomInit(uint64_t seed) {
	RandomGenerator rng = {0, (seed << 1) | 1};
	RandomU32(&rng);
	rng.state += seed;
	RandomU32(&rng);
	return rng;
}

static float RandomFloat(RandomGenerator* rng, float min, float max) {
	return min + (RandomU32(rng) / (float)UINT_MAX) * (max - min);
}

struct Bud;
struct StemSegment;

struct ShadowMapPoint {
	int x, y, z;
};

struct Bud { // a bud can have grown into a branch, but we still call it a bud
	uint32_t id;
	HMM_Vec3 base_point;
	HMM_Quat base_rotation;
	ShadowMapPoint end_sample_point;
	DS_DynArray(StemSegment) segments;
	bool is_dead;
	float leaf_growth;
	//float leaf_expanded;
	int order;
	float total_lightness;
	float max_lightness; // max lightness of any single bud
	float next_bud_angle_rad; // incremented by golden ratio angle
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

#define SHADOW_VOLUME_DIM 64

struct Plant {
	DS_Arena* arena;
	Bud root;
	uint32_t next_bud_id;
	int age;
	float shadow_volume_half_extent;
	uint8_t shadow_volume[SHADOW_VOLUME_DIM*SHADOW_VOLUME_DIM*SHADOW_VOLUME_DIM]; // each voxel stores the number of buds inside it
};

struct PlantParameters {
	int _;
};

static void BudGrow(Plant* plant, Bud* bud, float vigor, DS_Arena* temp_arena);

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
	
	HMM_Vec3 dir = {};
	float dir_weight = 0.f;

	for (int z = min_z; z <= max_z; z++) {
		for (int y = min_y; y <= max_y; y++) {
			for (int x = min_x; x <= max_x; x++) {
				int local_x = x - shadow_p.x, local_y = y - shadow_p.y, local_z = z - shadow_p.z;
				if (local_x == 0 && local_y == 0 && local_z == 0) continue;
				
				uint8_t val = plant->shadow_volume[z*SHADOW_VOLUME_DIM*SHADOW_VOLUME_DIM + y*SHADOW_VOLUME_DIM + x];
				float weight = 1.f - (float)val / 255.f;
				HMM_Vec3 this_dir = {(float)local_x, (float)local_y, (float)local_z};
				
				dir += this_dir * weight;
				dir_weight += weight;
			}
		}
	}

	if (dir_weight == 0.f) return false;
	dir /= dir_weight;
	
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

	// TODO: stop with the distribution crap and just have an apical control parameter
	/*
	DS_DynArray(SortElem) sort_elems = {temp_arena};
	DS_DynArray(float) weights = {temp_arena};
	DS_ArrReserve(&sort_elems, bud->segments.length);
	DS_ArrResizeUndef(&weights, bud->segments.length);
	
	float apical_vigor = vigor;
	float total_weight_inv = 1.f;

	if (bud->segments.length > 0) {
		for (int i = 0; i < bud->segments.length; i++) {
			StemSegment segment = bud->segments.data[i];
			DS_ArrPush(&sort_elems, {segment.end_point_lightness, i});
		}
		DS_ArrPeek(sort_elems).sort_key = 100000000.f; // always put the apical bud as the first element in the list
		//DS_ArrPeek(sort_elems).sort_key += 0.05f; // bias towards the apical bud

		qsort(sort_elems.data, sort_elems.length, sizeof(SortElem), SortElemQSortCompare);

		if (bud->order <= 0) { // share vigor evenly across lateral buds for lateral growth
			float total_weight = 0.f;
			for (int i = 0; i < sort_elems.length; i++) {
				SortElem sort_elem = sort_elems.data[i];
				StemSegment segment = bud->segments.data[sort_elem.index];
		
				//int weight_param_i = i < DS_ArrayCount(weight_params) ? i : DS_ArrayCount(weight_params) - 1;
				//float weight = segment.end_point_lightness * weight_params[weight_param_i];
				float weight = 1.f;//segment.end_point_lightness * DistributionWeight((float)i, (float)sort_elems.length);
		
				weights.data[sort_elem.index] = weight;
				total_weight += weight;
			}
		
			total_weight_inv = 1.f / total_weight;
			apical_vigor *= DS_ArrPeek(weights) * total_weight_inv;
		}
	}*/

static void UpdateBudSamplePoint(Plant* plant, Bud* bud) {
	HMM_Vec3 bud_end_point = bud->segments.length > 0 ? DS_ArrPeek(bud->segments).end_point : bud->base_point;
	HMM_Quat bud_end_rotation = bud->segments.length > 0 ? DS_ArrPeek(bud->segments).end_rotation : bud->base_rotation;
	bud->end_sample_point = PointToShadowMapSpace(plant, bud_end_point + HMM_RotateV3({0, 0, 1.5f/(float)SHADOW_VOLUME_DIM}, bud_end_rotation));
}

static void ApicalGrowth(Plant* plant, Bud* bud, float vigor) {
	for (float f = vigor; f > 0.f; f -= 1.f) {
		float step_scale = HMM_MIN(f, 1.f);
		float step_length = step_scale / (float)SHADOW_VOLUME_DIM;

		HMM_Vec3 bud_end_point = bud->segments.length > 0 ? DS_ArrPeek(bud->segments).end_point : bud->base_point;
		HMM_Quat bud_end_rotation = bud->segments.length > 0 ? DS_ArrPeek(bud->segments).end_rotation : bud->base_rotation;

		HMM_Vec3 old_dir = HMM_RotateV3({0, 0, 1}, bud_end_rotation);

		HMM_Vec3 optimal_direction;
		bool optimal_direction_ok = FindOptimalGrowthDirection(plant, bud_end_point, &optimal_direction);
		if (!optimal_direction_ok) optimal_direction = old_dir;
		
		// Twist
		optimal_direction += HMM_RotateV3({0.1f, 0, 0}, bud_end_rotation);
		//optimal_direction += HMM_RotateV3({0.f, 0.1f, 0}, bud_end_rotation);
		
		HMM_Vec3 new_dir = HMM_LerpV3(old_dir, 1.f * step_scale, optimal_direction);
		new_dir.Z -= 0.2f * step_scale;
		//new_dir.X -= 0.05f * step_scale;
		new_dir = HMM_NormV3(new_dir);

		HMM_Quat rotator_to_new_dir = HMM_ShortestRotationBetweenUnitVectors(old_dir, new_dir, {0, 0, 1});

		HMM_Quat new_end_rotation = rotator_to_new_dir * bud_end_rotation;

		HMM_Vec3 new_end_point = bud_end_point + step_length * new_dir;

		ShadowMapPoint shadow_p = PointToShadowMapSpace(plant, new_end_point);

		if (shadow_p.x >= 0 && shadow_p.x < SHADOW_VOLUME_DIM &&
			shadow_p.y >= 0 && shadow_p.y < SHADOW_VOLUME_DIM &&
			shadow_p.z >= 0 && shadow_p.z < SHADOW_VOLUME_DIM)
		{
			const float golden_ratio_rad_increment = 1.61803398875f * 3.1415926f * 2.f;

			if (bud->segments.length == 0 || DS_ArrPeek(bud->segments).step_scale + step_scale > 1.f) {
				// Add a lateral bud for the last segment
				if (bud->segments.length > 0) {
					StemSegment* last_segment = DS_ArrPeekPtr(bud->segments);
					//assert(last_segment->end_lateral == NULL);

					HMM_Quat new_bud_rot = {0, 0, 0, 1};
					new_bud_rot = HMM_QFromAxisAngle_RH({1.f, 0.f, 0.f}, HMM_AngleDeg(60.f)) * new_bud_rot;
					new_bud_rot = HMM_QFromAxisAngle_RH({0.f, 0.f, 1.f}, HMM_AngleRad(bud->next_bud_angle_rad)) * new_bud_rot;
					new_bud_rot = last_segment->end_rotation * new_bud_rot;
					bud->next_bud_angle_rad += golden_ratio_rad_increment;
			
					Bud* new_bud = DS_New(Bud, plant->arena);
					new_bud->id = plant->next_bud_id++;
					new_bud->base_point = last_segment->end_point;
					new_bud->base_rotation = new_bud_rot;
					new_bud->next_bud_angle_rad = bud->next_bud_angle_rad + golden_ratio_rad_increment;
					new_bud->order = bud->order + 1;
					DS_ArrInit(&new_bud->segments, plant->arena);
			
					UpdateBudSamplePoint(plant, new_bud);
			
					last_segment->end_lateral = new_bud;
				}

				StemSegment new_segment{};
				DS_ArrPush(&bud->segments, new_segment);
				
				IncrementShadowValueClampedSquare(plant, shadow_p.x - 1, shadow_p.x + 1, shadow_p.y - 1, shadow_p.y + 1, shadow_p.z-0, 8);
				IncrementShadowValueClampedSquare(plant, shadow_p.x - 1, shadow_p.x + 1, shadow_p.y - 1, shadow_p.y + 1, shadow_p.z-1, 6);
				IncrementShadowValueClampedSquare(plant, shadow_p.x - 2, shadow_p.x + 2, shadow_p.y - 2, shadow_p.y + 2, shadow_p.z-2, 3);
				IncrementShadowValueClampedSquare(plant, shadow_p.x - 3, shadow_p.x + 3, shadow_p.y - 3, shadow_p.y + 3, shadow_p.z-3, 2);
			}
			
			{
				StemSegment* last_segment = DS_ArrPeekPtr(bud->segments);
				last_segment->end_point = new_end_point;
				last_segment->end_rotation = new_end_rotation;
				last_segment->step_scale += step_scale;
			}

			//IncrementShadowValueClampedSquare(plant, shadow_p.x - 4, shadow_p.x + 4, shadow_p.y - 4, shadow_p.y + 4, shadow_p.z-4, 1);
		
			UpdateBudSamplePoint(plant, bud);
		}
	}
}

static void BudGrow(Plant* plant, Bud* bud, float vigor, DS_Arena* temp_arena) {
	//if (bud->order >= 3) return;
	//if (bud->order >= 2) return;
	
	// shed the branch?
	if (bud->segments.length > 0) {
		//float branch_lightness = bud->segments.data[0].end_lightness_main + bud->segments.data[0].end_lightness_lateral;
		//if ((float)bud->segments.length * 0.7 > branch_lightness) {
		//	bud->is_dead = true;
		//	DS_ArrClear(&bud->segments);
		//}
	}
	
	if (!bud->is_dead) {
		// let's try to model a birch accurately.
		// so a birch for example.
		// When young, the apex has full control.
		// After it has a proper form, it starts dividing its resources equally among its main branches. The main branches are chosen as the
		// few most promising buds. Other buds are ignored.
		
		// Let's calculate the average lightness and use this as a binary threshold for giving resources.
		//float 

		// Internal signals to determine which buds to distribute resources to
		float threshold = 0.2f;
		HMM_Vec3 prev_active_bud_direction = {0, 0, -1};

		DS_DynArray(Bud*) active_buds = {temp_arena};

		// Why does a birch tree want a bit of extra space at the bottom?
		// A: so that the first branches won't need to compete with bushes and ground plants
		// B: so that the crown can reach higher and will be able to compete better against neighboring trees
		// In forests, leaves at the bottom of the tree contribute very little as they receive very little sunlight, so it's smart to never grow them in the first place.
		// In an open field, this doesn't matter.

		// we want apical control to be a ratio.
		// 0 means all resources are given equally to all lateral buds, and nothing is given to the apical bud.
		// 1 means all resources are given equally to all buds, including apical bud.
		float apical_control = 1.f;

		int first_possible_active_bud = 3;
		if (bud->order == 0) {
			if (bud->segments.length > 20) {
				apical_control = 0.f;
			}
			first_possible_active_bud = 10;
			threshold = 0.5f;
		}
		if (bud->order == 1) {
			apical_control = 0.8f;
			if (bud->segments.length > 4) apical_control = 0.3f;
			threshold = 0.5f;
		}
		if (bud->order == 2) {
			apical_control = 0.2f;
			//first_possible_active_bud = 0;
			threshold = 0.2f;
		}
		if (bud->order >= 3) {
			apical_control = 1.f;
			//first_possible_active_bud = 0;
			threshold = 1.f;
		}

		float vigor_after_leaf_growth = vigor;

		// leaf growth
		for (int i = 0; i < bud->segments.length - 1; i++) { // NOTE: the last segment may not ever have an active lateral bud!
			StemSegment* segment = &bud->segments.data[i];
			Bud* end_lateral = segment->end_lateral;
			bool in_leaf_stage = end_lateral->segments.length == 0;
			
			if (in_leaf_stage) {
				end_lateral->leaf_growth += 0.5f*vigor_after_leaf_growth;
				if (end_lateral->leaf_growth > 1.f) end_lateral->leaf_growth = 1.f;
				//else vigor_after_leaf_growth *= 0.5f;
			}
			
			// kill leaf?
			if (end_lateral->segments.length > 3 || segment->width > 0.0015f) {
				end_lateral->leaf_growth = 0;
			}
		}

		for (int i = first_possible_active_bud; i < bud->segments.length - 1; i++) { // NOTE: the last segment may not ever have an active lateral bud!
			StemSegment* segment = &bud->segments.data[i];
			Bud* end_lateral = segment->end_lateral;
			//if (segment.end_total_lightness + segment.end_lateral->total_lightness == 0.f) break;
			HMM_Vec3 lateral_dir = HMM_RotateV3({0, 0, 1}, end_lateral->base_rotation); // @speed

			// The problem now is that if we have a trunk with equal light in each segment,
			// it's impossible to "pick" only a few buds to start growing with just a threshold.
			// I think, per bud, we should assign a random number like "bud quality". Still, then there will be a difference between lateral branches and branches going upwards.

			// for now, ignore the lightness and just say it's totally random.

			RandomGenerator rng = RandomInit(end_lateral->id);
			float bud_random_strength_bias = RandomFloat(&rng, 0.f, 1.f);

			// is this an active bud?
			if (/*end_lateral->max_lightness + */bud_random_strength_bias > threshold &&
				HMM_DotV3(lateral_dir, prev_active_bud_direction) < 0.f)
			{
				DS_ArrPush(&active_buds, end_lateral);
				//float v_main_weight = apical_control * segment.end_total_lightness;
				//float v_lateral_weight = (1.f - apical_control) * end_lateral->total_lightness;
				//float mult = vigor_left / (v_main_weight + v_lateral_weight);
				//float v_lateral = v_lateral_weight * mult;
				//vigor_left = v_main_weight * mult;

				//vigor_left += BudGrow(plant, end_lateral, v_lateral, temp_arena);
				
				prev_active_bud_direction = lateral_dir;
			}
		}
			
		float v_lateral = HMM_Clamp(2.f - 2.f*apical_control, 0.f, 1.f) * vigor_after_leaf_growth / ((float)active_buds.length + 2.f*apical_control);
		float v_main = vigor_after_leaf_growth - v_lateral*(float)active_buds.length;
			
		// how much vigor to give to lateral buds?
		for (int i = 0; i < active_buds.length; i++) {
			Bud* lateral_bud = active_buds.data[i];
			BudGrow(plant, lateral_bud, v_lateral, temp_arena);
		}

		ApicalGrowth(plant, bud, v_main);
	}
}

// returns the total length of all segments combined
static float PlantCalculateLight(Plant* plant, Bud* bud, float* out_width) {
	ShadowMapPoint shadow_p = bud->end_sample_point;
	uint8_t val = plant->shadow_volume[shadow_p.z*SHADOW_VOLUME_DIM*SHADOW_VOLUME_DIM + shadow_p.y*SHADOW_VOLUME_DIM + shadow_p.x];
	
	float total_lightness = 1.f - ((float)val-1.f)/255.f;
	total_lightness = HMM_MAX(total_lightness, 0.f);
	
	float max_lightness = total_lightness;

	//const float bud_width = 0.002f;
	//const float bud_width = 0.00025f;
	//float width = bud_width;

	float total_length = 0.f;
	//float max_total_length = 0.f;
	//float width_linear = 0.0f;
	float width = 0.f;

	for (int i = bud->segments.length - 1; i >= 0; i--) {
		StemSegment* segment = &bud->segments.data[i];
		segment->end_total_lightness = total_lightness;

		if (segment->end_lateral) {
			float lateral_width = 0.f;
			total_length += PlantCalculateLight(plant, segment->end_lateral, &lateral_width);
			//width_linear += lateral_width;
			//width_linear = HMM_MAX(width, width_linear); // we need to accomodate the lateral
			
			//width = total_length;

			//float lateral_width = segment->end_lateral->segments.length > 0 ? segment->end_lateral->segments.data[0].width : bud_width;
			//assert(lateral_width >= bud_width);
			//width = sqrtf(width*width + lateral_width*lateral_width);
			//width += lateral_width*1.f;
			
			max_lightness = HMM_MAX(max_lightness, segment->end_lateral->max_lightness);
			total_lightness += segment->end_lateral->total_lightness;
		}

		// width grows over time as well...
		//width_linear += 0.0002f*segment->step_scale;
		//width = 0.1f*sqrtf(width_linear);//Decelerate(width_linear, 50.f);
		//segment->width = sqrtf(Decelerate(total_length, 0.0002f)) * 0.0003f + 0.0001f;
		segment->width = sqrtf(total_length) * 0.0003f + 0.0001f;
		
		total_length += segment->step_scale;
	}
	
	//*out_width = width_linear;
	bud->total_lightness = total_lightness;
	bud->max_lightness = max_lightness;
	return total_length;
}

static void PlantInit(Plant* plant, DS_Arena* arena) {
	memset(plant, 0, sizeof(*plant));
	plant->arena = arena;
	plant->root.id = plant->next_bud_id++;
	plant->root.base_point = {0.5f/SHADOW_VOLUME_DIM, 0.5f/SHADOW_VOLUME_DIM, 0.5f/SHADOW_VOLUME_DIM};
	plant->root.base_rotation = {0, 0, 0, 1};
	plant->shadow_volume_half_extent = 0.5f;
	DS_ArrInit(&plant->root.segments, arena);
}

// returns "true" if modifications were made
static bool PlantDoGrowthIteration(Plant* plant, const PlantParameters* params, DS_Arena* temp_arena) {
	//float vigor_scale = 0.001f;
	//float vigor_scale = 0.01f;
	float vigor_scale = 0.005f;
	
	//float vigor_scale = 10.f;
	float _width;
	float total_length = 10.f + PlantCalculateLight(plant, &plant->root, &_width);
	if (total_length > 2000.f) return false;

 	BudGrow(plant, &plant->root, vigor_scale * total_length, temp_arena);
	plant->age++;

	return true;
	//if (plant->base) {
	//	//float total_light = PlantCalculateLight(plant, plant->base);
	//	//printf("total_light: %f\n", total_light);
	//}
	//else {
	//	HMM_Vec3 start_point = {0.5f/SHADOW_VOLUME_DIM, 0.5f/SHADOW_VOLUME_DIM, 0.5f/SHADOW_VOLUME_DIM};
	//	plant->base = GrowNewBud(plant, start_point, {0, 0, 0, 1}, vigor_scale, temp_arena);
	//}
}

#if 0
struct BudPoint {
	HMM_Vec3 point;
	float thickness;
	HMM_Quat rotation; // +Z is up
};

struct Bud {
	DS_DynArray(BudPoint) points;
	float end_leaf_expand; // 0 if the end is a bud, between 0 and 1 means a ratio of how grown a leaf is
};

struct Plant {
	DS_Arena* arena;
	DS_DynArray(Bud) buds;
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

static void AddBud(Plant* plant, uint32_t id, HMM_Vec3 base_point, HMM_Quat base_rotation, float age, bool is_leaf, const PlantParameters* params) {
	Bud bud{};
	DS_ArrInit(&bud.points, plant->arena);

	bud.end_leaf_expand = 0.f;
	if (is_leaf) {
		bud.end_leaf_expand = Approach(age, 1.f, params->leaf_growth_speed);
		if (bud.end_leaf_expand == 0.f) bud.end_leaf_expand = 0.00001f;
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
	// - why aren't all buds growing equally fast?
	// - why do new leaves and buds only start growing from the top?

	// The apical bud is very special thing. Where there is an apical bud, the bud just keeps on growing and growing to push the apical bud out and to give space for new leaves.
	// If it's just a bud with a leaf at the end, and the leaf is in a good position, then the bud doesn't need to waste vigor by growing further.

	// it's all about resource allocation. Where would it make sense to grow a new apical meribud?

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
			// drop a new leaf and a new bud from the bud?
			// TODO: the next step is to spawn new axilary buds that can start growing into their own buds.

			if (segment_start_point_t - last_leaf_drop_growth > params->drop_frequency) {
				// The drop pitch generally starts out as 0 and as the apical meribud grows in width, it pushes it out and the pitch increases.
				// Also, as the new bud grows and becomes more heavy, the pitch increases further.
				float leaf_drop_pitch = Approach(segment_start_point_age, params->leaf_drop_pitch, params->leaf_drop_pitch_speed);
				float leaf_lives_up_to_age = RandomFloat(&rng, 0.5f, 2.f);
				
				if (segment_start_point_age < leaf_lives_up_to_age) {
					HMM_Quat new_leaf_rot = {0, 0, 0, 1};
					new_leaf_rot = HMM_QFromAxisAngle_RH({1.f, 0.f, 0.f}, HMM_AngleDeg(leaf_drop_pitch)) * new_leaf_rot;
					new_leaf_rot = HMM_QFromAxisAngle_RH({0.f, 0.f, 1.f}, HMM_AngleRad(end_point_drop_leaf_yaw)) * new_leaf_rot;
					new_leaf_rot = end_rotation * new_leaf_rot;
					AddBud(plant, id + i, end_point, new_leaf_rot, segment_start_point_age * params->drop_leaf_growth_ratio, true, params);
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
						AddBud(plant, id + i, end_point, new_axillary_bud_rot, axillary_bud_age * params->drop_apical_growth_ratio, false, params);
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

		DS_ArrPush(&bud.points, {end_point, this_thickness, end_rotation});
		
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
			//	DS_ArrPush(&bud.points, {end_point, this_thickness, end_rotation});
			//	break;
			//}
		}
	}
	
	assert(bud.points.length >= 2);
	DS_ArrPush(&plant->buds, bud);
}

static Plant GeneratePlant(DS_Arena* arena, const PlantParameters* params) {
	Plant plant{};
	plant.arena = arena;
	DS_ArrInit(&plant.buds, arena);

	AddBud(&plant, 0, {0, 0, 0}, {0, 0, 0, 1}, params->age, false, params);

	return plant;
}

#endif