// Intuitive 2D and 3D math utilities for problems that belong to a space.
// Builds upon HandmadeMath.h and adopts the HMM_ prefix.
//
// !!! EXPERIMENTAL !!!
// The idea is to put everything I can think of here to build a biiiiiiiig library which will never truly be finished. (though I can release stable versions)

// Naming conventions:
// WS - world space
// VS - view space
// CS - clip space
typedef struct HMM_PerspectiveCamera {
	HMM_Mat4 clip_from_world;
	HMM_Mat4 world_from_clip;
	HMM_Vec3 position;
	HMM_Vec3 forward_dir;
	HMM_Vec3 right_dir;
	HMM_Vec3 up_dir;
} HMM_PerspectiveCamera;

// Implicit plane; stores the coefficients A, B, C and D to the plane equation A*x + B*y + C*z + D = 0
typedef HMM_Vec4 HMM_Plane;


// if from = -to, then fallback_axis will be used as the axis of rotation.
static HMM_Quat HMM_ShortestRotationBetweenUnitVectors(HMM_Vec3 from, HMM_Vec3 to, HMM_Vec3 fallback_axis);

static HMM_Vec3 HMM_RotateV3(HMM_Vec3 vector, HMM_Quat rotator);
static HMM_Mat3 HMM_QToM3(HMM_Quat q, float scale);

static HMM_Plane HMM_PlaneFromPointAndNormal(HMM_Vec3 plane_p, HMM_Vec3 plane_n);
static HMM_Plane HMM_FlipPlane(HMM_Plane plane);

static HMM_Vec3 HMM_ProjectPointOntoPlane(HMM_Vec3 p, HMM_Vec3 plane_p, HMM_Vec3 plane_n);

static HMM_Vec3 HMM_ProjectPointOntoLine(HMM_Vec3 p, HMM_Vec3 line_p, HMM_Vec3 line_dir);

static bool HMM_RayTriangleIntersect(HMM_Vec3 ro, HMM_Vec3 rd, HMM_Vec3 a, HMM_Vec3 b, HMM_Vec3 c, float* out_t, float* out_hit_dir);
static bool HMM_RayPlaneIntersect(HMM_Vec3 ro, HMM_Vec3 rd, HMM_Plane plane, float* out_t, HMM_Vec3* out_p);

// static HMM_PerspectiveCamera HMM_MakePerspectiveCamera(HMM_Vec3 position, HMM_Quat rotation,
//	float FOV_radians, float aspect_ratio_x_over_y, float z_near, float z_far);

static inline HMM_Vec2 HMM_CSToSS(HMM_Vec4 p_cs, HMM_Vec2 window_size);
static inline HMM_Vec4 HMM_SSToCS(HMM_Vec2 p_ss, HMM_Vec2 one_over_window_size);
static inline HMM_Vec2 HMM_NDCToSS(HMM_Vec2 p_ndc, HMM_Vec2 window_size);
static inline HMM_Vec2 HMM_SSToNDC(HMM_Vec2 p_ss, HMM_Vec2 one_over_window_size);

static HMM_Vec3 HMM_RayDirectionFromSSPoint(const HMM_PerspectiveCamera* camera, HMM_Vec2 p_ss, HMM_Vec2 one_over_window_size);
static HMM_Vec3 HMM_RayDirectionFromCSPoint(const HMM_PerspectiveCamera* camera, HMM_Vec4 p_cs);

static bool HMM_GetPointScreenSpaceScale(const HMM_PerspectiveCamera* camera, HMM_Vec3 p_ws, float* out_scale);

// Signed Distance Functions
static float HMM_DistanceToLineSegment2D(HMM_Vec2 p, HMM_Vec2 a, HMM_Vec2 b);
static float HMM_DistanceToPolygon2D(HMM_Vec2 p, const HMM_Vec2* v, int v_count);

static float HMM_SignedDistanceToPlane(HMM_Vec3 p, HMM_Plane plane);

// ---------------------------------------------

static bool HMM_GetPointScreenSpaceScale(const HMM_PerspectiveCamera* camera, HMM_Vec3 p, float* out_scale) {
	HMM_Vec4 p_clip = HMM_MulM4V4(camera->clip_from_world, HMM_V4V(p, 1.f));
	*out_scale = p_clip.W;
	return p_clip.Z > 0;
}

static HMM_Vec3 HMM_RayDirectionFromSSPoint(const HMM_PerspectiveCamera* camera, HMM_Vec2 p_ss, HMM_Vec2 one_over_window_size) {
	HMM_Vec4 p_cs = HMM_SSToCS(p_ss, one_over_window_size);
	HMM_Vec3 dir = HMM_RayDirectionFromCSPoint(camera, p_cs);
	return dir;
}

static HMM_Vec3 HMM_RayDirectionFromCSPoint(const HMM_PerspectiveCamera* camera, HMM_Vec4 p_cs) {
	HMM_Mat4 world_from_clip = HMM_InvGeneralM4(camera->clip_from_world);

	HMM_Vec4 point_in_front = HMM_MulM4V4(world_from_clip, p_cs);
	point_in_front = HMM_MulV4F(point_in_front, 1.f / point_in_front.W);

	HMM_Vec3 dir = HMM_NormV3(HMM_SubV3(point_in_front.XYZ, camera->position));
	return dir;
}

static float HMM_DistanceToLineSegment2D(HMM_Vec2 p, HMM_Vec2 a, HMM_Vec2 b) {
	// See sdSegment from https://iquilezles.org/articles/distfunctions2d/
	HMM_Vec2 ap = HMM_SubV2(p, a);
	HMM_Vec2 ab = HMM_SubV2(b, a);
	float h = HMM_Clamp(HMM_DotV2(ap, ab) / HMM_DotV2(ab, ab), 0.f, 1.f);
	return HMM_LenV2(HMM_SubV2(ap, HMM_MulV2F(ab, h)));
}

static float HMM_DistanceToPolygon2D(HMM_Vec2 p, const HMM_Vec2* v, int v_count) {
	// See sdPolygon from https://iquilezles.org/articles/distfunctions2d/
	assert(v_count >= 3);

	float d = HMM_DotV2(p - v[0], p - v[0]);
	float s = 1.f;
	for (int i=0, j=v_count-1; i<v_count; j=i, i++) {
		HMM_Vec2 e = v[j] - v[i];
		HMM_Vec2 w = p - v[i];
		HMM_Vec2 b = HMM_SubV2(w, HMM_MulV2F(e, HMM_Clamp(HMM_DotV2(w,e) / HMM_DotV2(e,e), 0.f, 1.f)));
		d = min(d, HMM_DotV2(b, b));
		
		bool cx = p.Y >= v[i].Y;
		bool cy = p.Y < v[j].Y;
		bool cz = e.X*w.Y > e.Y*w.X;
		if ((cx && cy && cz) || (!cx && !cy && !cz)) s *= -1.f;
	}
	return s * sqrtf(d);
}

static inline HMM_Vec4 HMM_SSToCS(HMM_Vec2 p_ss, HMM_Vec2 one_over_window_size) {
	HMM_Vec4 p_cs = {0.f, 0.f, 0.f, 1.f};
	p_cs.X = 2.f * p_ss.X * one_over_window_size.X - 1.f;
#ifdef HMM_CLIP_SPACE_INVERT_Y
	p_cs.Y = 2.f - 2.f * p_ss.Y * one_over_window_size.Y - 1.f;
#else
	p_cs.Y = 2.f * p_ss.Y * one_over_window_size.Y - 1.f;
#endif
	return p_cs;
}

static inline HMM_Vec2 HMM_SSToNDC(HMM_Vec2 p_ss, HMM_Vec2 one_over_window_size) {
	HMM_Vec2 p_ndc;
	p_ndc.X = 2.f * p_ss.X * one_over_window_size.X - 1.f;
#ifdef HMM_CLIP_SPACE_INVERT_Y
	p_ndc.Y = 2.f - 2.f * p_ss.Y * one_over_window_size.Y - 1.f;
#else
	p_ndc.Y = 2.f * p_ss.Y * one_over_window_size.Y - 1.f;
#endif
	return p_ndc;
}

static inline HMM_Vec2 HMM_NDCToSS(HMM_Vec2 p_ndc, HMM_Vec2 window_size) {
	HMM_Vec2 p_ss;
	p_ss.X = (0.5f * p_ndc.X + 0.5f) * window_size.X;
#ifdef HMM_CLIP_SPACE_INVERT_Y
	p_ss.Y = (0.5f - 0.5f * p_ndc.Y) * window_size.Y;
#else
	p_ss.Y = (0.5f * p_ndc.Y + 0.5f) * window_size.Y;
#endif
	return p_ss;
}

static inline HMM_Vec2 HMM_CSToSS(HMM_Vec4 p_cs, HMM_Vec2 window_size) {
	HMM_Vec2 result;
	result.X = (0.5f * p_cs.X / p_cs.W + 0.5f) * window_size.X;
#ifdef GIZMOS_CLIP_SPACE_INVERT_Y
	result.Y = (0.5f - 0.5f * p_cs.Y / p_cs.W) * window_size.Y;
#else
	result.Y = (0.5f * p_cs.Y / p_cs.W + 0.5f) * window_size.Y;
#endif
	return result;
}

static HMM_Plane HMM_FlipPlane(HMM_Plane plane) {
	HMM_Plane flipped = {-plane.X, -plane.Y, -plane.Z, -plane.W};
	return flipped;
}

static HMM_Plane HMM_PlaneFromPointAndNormal(HMM_Vec3 plane_p, HMM_Vec3 plane_n) {
	HMM_Plane p;
	p.XYZ = plane_n;
	p.W = -HMM_DotV3(plane_p, plane_n);
	return p;
}

static HMM_Vec3 HMM_ProjectPointOntoLine(HMM_Vec3 p, HMM_Vec3 line_p, HMM_Vec3 line_dir) {
	float t = HMM_DotV3(HMM_SubV3(p, line_p), line_dir);
	HMM_Vec3 projected = HMM_AddV3(line_p, HMM_MulV3F(line_dir, t));
	return projected;
}

static HMM_Vec3 HMM_ProjectPointOntoPlane(HMM_Vec3 p, HMM_Vec3 plane_p, HMM_Vec3 plane_n) {
	HMM_Vec3 p_rel = HMM_SubV3(p, plane_p);
	float d = HMM_DotV3(p_rel, plane_n);
	p_rel = HMM_SubV3(p_rel, HMM_MulV3F(plane_n, d));
	return HMM_AddV3(plane_p, p_rel);
}

static HMM_Vec3 HMM_RotateV3(HMM_Vec3 vector, HMM_Quat rotator) {
	// from https://stackoverflow.com/questions/44705398/about-glm-quaternion-rotation
	HMM_Vec3 a = HMM_MulV3F(vector, rotator.W);
	HMM_Vec3 b = HMM_Cross(rotator.XYZ, vector);
	HMM_Vec3 c = HMM_AddV3(b, a);
	HMM_Vec3 d = HMM_Cross(rotator.XYZ, c);
	return HMM_AddV3(vector, HMM_MulV3F(d, 2.f));
}

static HMM_Mat3 HMM_QToM3(HMM_Quat q, float scale) {
	HMM_Mat3 Result;

	float XX, YY, ZZ,
		XY, XZ, YZ,
		WX, WY, WZ;

	XX = q.X * q.X;
	YY = q.Y * q.Y;
	ZZ = q.Z * q.Z;
	XY = q.X * q.Y;
	XZ = q.X * q.Z;
	YZ = q.Y * q.Z;
	WX = q.W * q.X;
	WY = q.W * q.Y;
	WZ = q.W * q.Z;

	Result.Elements[0][0] = scale * (1.0f - 2.0f * (YY + ZZ));
	Result.Elements[0][1] = scale * (2.0f * (XY + WZ));
	Result.Elements[0][2] = scale * (2.0f * (XZ - WY));

	Result.Elements[1][0] = scale * (2.0f * (XY - WZ));
	Result.Elements[1][1] = scale * (1.0f - 2.0f * (XX + ZZ));
	Result.Elements[1][2] = scale * (2.0f * (YZ + WX));

	Result.Elements[2][0] = scale * (2.0f * (XZ + WY));
	Result.Elements[2][1] = scale * (2.0f * (YZ - WX));
	Result.Elements[2][2] = scale * (1.0f - 2.0f * (XX + YY));

	return Result;
}

static float HMM_SignedDistanceToPlane(HMM_Vec3 p, HMM_Plane plane) {
	return HMM_DotV3(p, plane.XYZ) + plane.W;
}

static HMM_Quat HMM_ShortestRotationBetweenUnitVectors(HMM_Vec3 from, HMM_Vec3 to, HMM_Vec3 fallback_axis) {
	float k_cos_theta = HMM_DotV3(from, to);
	float k = sqrtf(HMM_DotV3(from, from) * HMM_DotV3(to, to));
	HMM_Quat q;
	if (k_cos_theta == -k) {
		q.XYZ = fallback_axis;
		q.W = 0.f;
	}
	else {
		q.XYZ = HMM_Cross(from, to);
		q.W = k_cos_theta + k;
		q = HMM_NormQ(q);
	}
	return q;
}

static bool HMM_RayTriangleIntersect(HMM_Vec3 ro, HMM_Vec3 rd, HMM_Vec3 a, HMM_Vec3 b, HMM_Vec3 c, float* out_t, float* out_hit_dir) {
	// see https://en.wikipedia.org/wiki/M%C3%B6ller%E2%80%93Trumbore_intersection_algorithm
	HMM_Vec3 edge1 = HMM_SubV3(b, a);
	HMM_Vec3 edge2 = HMM_SubV3(c, a);
	HMM_Vec3 ray_cross_e2 = HMM_Cross(rd, edge2);
	float det = HMM_DotV3(edge1, ray_cross_e2);
	if (det == 0.f) return false;

	float inv_det = 1.f / det;
	HMM_Vec3 s = HMM_SubV3(ro, a);
	float u = inv_det * HMM_DotV3(s, ray_cross_e2);
	if (u < 0.f || u > 1.f) return false;

	HMM_Vec3 s_cross_e1 = HMM_Cross(s, edge1);
	float v = inv_det * HMM_DotV3(rd, s_cross_e1);

	if (v < 0.f || u + v > 1.f) return false;

	float t = inv_det * HMM_DotV3(edge2, s_cross_e1);
	*out_t = t;
	*out_hit_dir = det;
	return t > 0.f;
}

static bool HMM_RayPlaneIntersect(HMM_Vec3 ro, HMM_Vec3 rd, HMM_Vec4 plane, float* out_t, HMM_Vec3* out_p) {
	// We know that  ro + rd*t = p  AND  dot(p, plane.XYZ) + plane.W = 0
	//
	//                 dot(ro + rd*t, plane.XYZ) + plane.W = 0
	// dot(ro, plane.XYZ) + dot(rd*t, plane.XYZ) + plane.W = 0
	// dot(ro, plane.XYZ) + dot(rd, plane.XYZ)*t + plane.W = 0
	// t = (-plane.W - dot(ro, plane.XYZ)) / dot(rd, plane.XYZ)

	float t_num = -HMM_DotV3(ro, plane.XYZ) - plane.W;
	float t_denom = HMM_DotV3(rd, plane.XYZ);
	if (t_denom == 0.f) return false;

	float t = t_num / t_denom;
	if (out_t) *out_t = t;
	if (out_p) *out_p = HMM_AddV3(ro, HMM_MulV3F(rd, t));
	return t > 0.f;
}
