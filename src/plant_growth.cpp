#include "Fire/fire_ds.h"
#include "third_party/HandmadeMath.h"
#include "utils/space_math.h"

#include "curves.h"
#include "plant_growth.h"

// -- Constants ---------------------------------------------------------------
#define SHADOW_VOLUME_SIZE 64
// ----------------------------------------------------------------------------

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

static uint32_t RandomU32(uint32_t seed) { // pcg_hash from https://www.reedbeta.com/blog/hash-functions-for-gpu-rendering/
	uint32_t state = seed * 747796405u + 2891336453u;
	uint32_t word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
	return (word >> 22u) ^ word;
}

static float RandomFloat(uint32_t seed, float min, float max) {
	return min + (RandomU32(seed) / (float)0xFFFFFFFF) * (max - min);
}

static void IncrementShadowValue(Plant* plant, int x, int y, int z, uint8_t amount) {
	uint8_t* val = &plant->shadow_volume[z*SHADOW_VOLUME_SIZE*SHADOW_VOLUME_SIZE + y*SHADOW_VOLUME_SIZE + x];
	uint8_t val_before = *val;
	*val = val_before + amount;
	if (*val < val_before) *val = 255; // overflow
}

static void IncrementShadowValueClampedSquare(Plant* plant, int min_x, int max_x, int min_y, int max_y, int z, uint8_t amount) {
	min_x = HMM_MIN(HMM_MAX(min_x, 0), SHADOW_VOLUME_SIZE - 1);
	max_x = HMM_MIN(HMM_MAX(max_x, 0), SHADOW_VOLUME_SIZE - 1);
	min_y = HMM_MIN(HMM_MAX(min_y, 0), SHADOW_VOLUME_SIZE - 1);
	max_y = HMM_MIN(HMM_MAX(max_y, 0), SHADOW_VOLUME_SIZE - 1);
	z = HMM_MIN(HMM_MAX(z, 0), SHADOW_VOLUME_SIZE - 1);

	for (int y = min_y; y <= max_y; y++) {
		for (int x = min_x; x <= max_x; x++) {
			IncrementShadowValue(plant, x, y, z, amount);
		}
	}
}

static ShadowMapPoint PointToShadowMapSpace(Plant* plant, HMM_Vec3 point) {
	int x = (int)((point.X + 0.5f) * SHADOW_VOLUME_SIZE);
	int y = (int)((point.Y + 0.5f) * SHADOW_VOLUME_SIZE);
	int z = (int)((point.Z) * SHADOW_VOLUME_SIZE);
	return {x, y, z};
}

static bool FindOptimalGrowthDirection(Plant* plant, HMM_Vec3 point, HMM_Vec3* out_direction) {
	ShadowMapPoint shadow_p = PointToShadowMapSpace(plant, point);
	int min_x = HMM_MAX(shadow_p.x - 1, 0), max_x = HMM_MIN(shadow_p.x + 1, SHADOW_VOLUME_SIZE-1);
	int min_y = HMM_MAX(shadow_p.y - 1, 0), max_y = HMM_MIN(shadow_p.y + 1, SHADOW_VOLUME_SIZE-1);
	int min_z = HMM_MAX(shadow_p.z - 1, 0), max_z = HMM_MIN(shadow_p.z + 1, SHADOW_VOLUME_SIZE-1);
	
	HMM_Vec3 dir = {};
	float dir_weight = 0.f;

	for (int z = min_z; z <= max_z; z++) {
		for (int y = min_y; y <= max_y; y++) {
			for (int x = min_x; x <= max_x; x++) {
				int local_x = x - shadow_p.x, local_y = y - shadow_p.y, local_z = z - shadow_p.z;
				if (local_x == 0 && local_y == 0 && local_z == 0) continue;
				
				uint8_t val = plant->shadow_volume[z*SHADOW_VOLUME_SIZE*SHADOW_VOLUME_SIZE + y*SHADOW_VOLUME_SIZE + x];
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

static void UpdateBudSamplePoint(Plant* plant, Bud* bud) {
	HMM_Vec3 bud_end_point = bud->segments.count > 0 ? DS_ArrPeek(bud->segments).end_point : bud->base_point;
	HMM_Quat bud_end_rotation = bud->segments.count > 0 ? DS_ArrPeek(bud->segments).end_rotation : bud->base_rotation;
	bud->end_sample_point = PointToShadowMapSpace(plant, bud_end_point + HMM_RotateV3({0, 0, 1.5f/(float)SHADOW_VOLUME_SIZE}, bud_end_rotation));
}

static void ApicalGrowth(Plant* plant, Bud* bud, float vigor) {
	for (float f = vigor; f > 0.f; f -= 1.f) {
		float step_scale = HMM_MIN(f, 1.f);
		float step_length = step_scale / (float)SHADOW_VOLUME_SIZE;

		HMM_Vec3 bud_end_point = bud->segments.count > 0 ? DS_ArrPeek(bud->segments).end_point : bud->base_point;
		HMM_Quat bud_end_rotation = bud->segments.count > 0 ? DS_ArrPeek(bud->segments).end_rotation : bud->base_rotation;

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

		if (shadow_p.x >= 0 && shadow_p.x < SHADOW_VOLUME_SIZE &&
			shadow_p.y >= 0 && shadow_p.y < SHADOW_VOLUME_SIZE &&
			shadow_p.z >= 0 && shadow_p.z < SHADOW_VOLUME_SIZE)
		{
			const float golden_ratio_rad_increment = 1.61803398875f * 3.1415926f * 2.f;

			if (bud->segments.count == 0 || DS_ArrPeek(bud->segments).step_scale + step_scale > 1.f) {
				// Add a lateral bud for the last segment
				if (bud->segments.count > 0) {
					StemSegment* last_segment = DS_ArrPeekPtr(bud->segments);
					//assert(last_segment->end_lateral == NULL);

					HMM_Quat new_bud_rot = {0, 0, 0, 1};
					new_bud_rot = HMM_QFromAxisAngle_RH({1.f, 0.f, 0.f}, HMM_AngleDeg(60.f)) * new_bud_rot;
					new_bud_rot = HMM_QFromAxisAngle_RH({0.f, 0.f, 1.f}, HMM_AngleRad(bud->next_bud_angle_rad)) * new_bud_rot;
					new_bud_rot = last_segment->end_rotation * new_bud_rot;
					bud->next_bud_angle_rad += golden_ratio_rad_increment;

					Bud* new_bud = DS_New(Bud, plant->arena);
					new_bud->distance_from_root = bud->distance_from_root + (float)bud->segments.count;
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

static void BudGrow(Plant* plant, DS_Arena* temp, Bud* bud, float vigor, const PlantParameters* params) {
	//if (bud->order >= 3) return;
	//if (bud->order >= 2) return;
	
	// shed the branch?
	if (bud->segments.count > 0) {
		//float branch_lightness = bud->segments[0].end_lightness_main + bud->segments[0].end_lightness_lateral;
		//if ((float)bud->segments.count * 0.7 > branch_lightness) {
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

		DS_DynArray(Bud*) active_buds = {temp};

		// Why does a birch tree want a bit of extra space at the bottom?
		// A: so that the first branches won't need to compete with bushes and ground plants
		// B: so that the crown can reach higher and will be able to compete better against neighboring trees
		// In forests, leaves at the bottom of the tree contribute very little as they receive very little sunlight, so it's smart to never grow them in the first place.
		// In an open field, this doesn't matter.

		// we want apical control to be a ratio.
		// 0 means all resources are given equally to all lateral buds, and nothing is given to the apical bud.
		// 1 means all resources are given equally to all buds, including apical bud.

		//if (bud->segments.count > 5)  apical_control = 0.5f;
		
		
		// so we need apical control curves... how should they work?
		// Let's try to tie apical control directly to f1(distance_from_root) + f2(segments_count)    we might want to decrease apical control
		
		// so we might want to have a drop...

		// what if we have "apical control over bud distance?"

		float stem_length = (float)bud->segments.count;
		//float bud_dist = bud->distance_from_root + stem_length; // this could be also made more accurate with half-segments at the end
		float base_dist = bud->distance_from_root; // this could be also made more accurate with half-segments at the end
		
		// We could blend between "apical_control_over_dist" and "apical_control_over_stem_length" with a lerp.
		// Or we could add some value to "bud_dist".

		// We can have two weights here, or even three!
		// TODO: try apical control over literal time in the simulation?
		float apical_control_x = (base_dist*params->ac_base_dist_factor + stem_length*params->ac_stem_length_factor + (float)bud->order*params->ac_order_factor) * params->ac_overall_factor;
		float apical_control = CurveEvalAtX(params->apical_control_curve, apical_control_x);

		/*if (bud->distance_from_root > 30.f) {
			if (bud->segments.count > 5) {
				apical_control = 0.3f;
			}
			//apical_control = 0.5f;
		}
		else {
			if (bud->segments.count > 40) apical_control = 0.f;
		}*/
		
		int first_possible_active_bud = 5;

		// how can we get the "order" as a float?
		// smooth_order? if something splits from the base, then it's the same as the parent thing. If something splits from the end, then it's very different.
		// so maybe it's like the "base distance?"
		
		// 
		//if (leafness > 0.7f) {
		//	if (bud->segments.count > 5) apical_control = 0.1f;
		//}
		//else {
		//}
		
		// apical control over leafness curve...
		// apical control threshold speedup over leafness

		//float maturity_ratio = bud->segments[0].width / plant->root.segments[0].width;

		// how to parametrize this neatly?
		// Instead of talking about order, should we talk about the maturity of the bud?
		// how mature is this bud?

		// so we want the following behaviour:
		// high apical control in the beginning, then when reaching a point of maturity, enable it more again.
		// we can use bud->segments.count to approximate the maturity of the bud.

		/*
		if (bud->order == 0) {
			if (bud->segments.count > 20) {
				apical_control = 0.f;
			}
			first_possible_active_bud = 10;
			threshold = 0.5f;
		}
		if (bud->order == 1) {
			apical_control = 0.8f;
			if (bud->segments.count > 4) apical_control = 0.3f;
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
		}*/

		float vigor_after_leaf_growth = vigor;

		// leaf growth
		for (int i = 0; i < bud->segments.count - 1; i++) { // NOTE: the last segment may not ever have an active lateral bud!
			StemSegment* segment = &bud->segments[i];
			Bud* end_lateral = segment->end_lateral;
			bool in_leaf_stage = end_lateral->segments.count == 0;
			
			if (in_leaf_stage) {
				end_lateral->leaf_growth += 0.5f*vigor_after_leaf_growth;
				if (end_lateral->leaf_growth > 1.f) end_lateral->leaf_growth = 1.f;
				//else vigor_after_leaf_growth *= 0.5f;
			}
			
			// kill leaf?
			if (end_lateral->segments.count > 3 || segment->width > 0.0015f) {
				end_lateral->leaf_growth = 0;
			}
		}

		for (int i = first_possible_active_bud; i < bud->segments.count - 1; i++) { // NOTE: the last segment may not ever have an active lateral bud!
			StemSegment* segment = &bud->segments[i];
			Bud* end_lateral = segment->end_lateral;
			//if (segment.end_total_lightness + segment.end_lateral->total_lightness == 0.f) break;
			HMM_Vec3 lateral_dir = HMM_RotateV3({0, 0, 1}, end_lateral->base_rotation); // @speed

			float bud_random_strength_bias = RandomFloat(params->random_seed + end_lateral->id, 0.f, 1.f);

			// is this an active bud?
			if (bud_random_strength_bias > threshold &&
				HMM_DotV3(lateral_dir, prev_active_bud_direction) < 0.f)
			{
				DS_ArrPush(&active_buds, end_lateral);
				prev_active_bud_direction = lateral_dir;
			}
		}
			
		float v_lateral = HMM_Clamp(2.f - 2.f*apical_control, 0.f, 1.f) * vigor_after_leaf_growth / ((float)active_buds.count + 2.f*apical_control);
		float v_main = vigor_after_leaf_growth - v_lateral*(float)active_buds.count;
			
		// how much vigor to give to lateral buds?
		for (int i = 0; i < active_buds.count; i++) {
			Bud* lateral_bud = active_buds[i];
			BudGrow(plant, temp, lateral_bud, v_lateral, params);
		}

		ApicalGrowth(plant, bud, v_main);
	}
}

static float CalculateTotalLengthAndApplySegmentWidth(Plant* plant, Bud* bud) {
	ShadowMapPoint shadow_p = bud->end_sample_point;
	uint8_t val = plant->shadow_volume[shadow_p.z*SHADOW_VOLUME_SIZE*SHADOW_VOLUME_SIZE + shadow_p.y*SHADOW_VOLUME_SIZE + shadow_p.x];

	float total_length = 0.f;
	float width = 0.f;

	for (int i = bud->segments.count - 1; i >= 0; i--) {
		StemSegment* segment = &bud->segments[i];
		if (segment->end_lateral) {
			total_length += CalculateTotalLengthAndApplySegmentWidth(plant, segment->end_lateral);
		}

		segment->width = sqrtf(total_length) * 0.0003f + 0.0001f;
		total_length += segment->step_scale;
	}
	
	return total_length;
}

void PlantInit(Plant* plant, DS_Arena* arena) {
	*plant = {};
	plant->arena = arena;
	plant->root.id = plant->next_bud_id++;
	plant->root.base_point = {0.5f/SHADOW_VOLUME_SIZE, 0.5f/SHADOW_VOLUME_SIZE, 0.5f/SHADOW_VOLUME_SIZE};
	plant->root.base_rotation = {0, 0, 0, 1};
	plant->shadow_volume = (uint8_t*)DS_ArenaPushZero(arena, SHADOW_VOLUME_SIZE * SHADOW_VOLUME_SIZE * SHADOW_VOLUME_SIZE * sizeof(uint8_t));
	DS_ArrInit(&plant->root.segments, arena);
}

bool PlantDoGrowthIteration(Plant* plant, DS_Arena* temp, const PlantParameters* params) {
	float age = 10.f + CalculateTotalLengthAndApplySegmentWidth(plant, &plant->root);
	if (age > params->max_age) return false;

 	BudGrow(plant, temp, &plant->root, params->vigor_scale * age, params);
	plant->age++;

	return true;
}

float GetLightnessAtPoint(Plant* plant, HMM_Vec3 p) {
	ShadowMapPoint shadow_p = PointToShadowMapSpace(plant, p);
	assert(shadow_p.x >= 0 && shadow_p.y >= 0 && shadow_p.z >= 0);
	assert(shadow_p.x < SHADOW_VOLUME_SIZE && shadow_p.y < SHADOW_VOLUME_SIZE && shadow_p.z < SHADOW_VOLUME_SIZE);
	
	int voxel_index = shadow_p.z*SHADOW_VOLUME_SIZE*SHADOW_VOLUME_SIZE + shadow_p.y*SHADOW_VOLUME_SIZE + shadow_p.x;
	float lightness = 1.f - 2.f*(float)plant->shadow_volume[voxel_index] / 255.f;
	return HMM_MAX(lightness, 0.f);
}
