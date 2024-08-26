// camera.h - Eero Mutka, 2023
// 
// * Depends on `fire.h` and `HandmadeMath.h`
// * Assumes a right-handed coordinate system.
// 
// USAGE:
// 1. Include this file
// 2. Make a zero-initialized Camera
// 3. on update, call `Camera_Update()`
// 4. take the cached `world_to_projection` matrix from the camera
//

typedef struct Camera {
	HMM_Vec3 pos;
	HMM_Quat ori;

	HMM_Vec3 lazy_pos;
	HMM_Quat lazy_ori;

	float aspect_ratio, z_near, z_far;

	// The following are cached in `Camera_Update`, do not set by hand
	HMM_Mat4 clip_from_world;
	HMM_Mat4 clip_from_view;
	HMM_Mat4 view_from_world;
	HMM_Mat4 view_from_clip;
	HMM_Mat4 world_from_view;
	HMM_Mat4 world_from_clip;
} Camera;

static HMM_Vec3 Camera_Right(const Camera* camera) { return camera->world_from_view.Columns[0].XYZ; }

// The world & camera coordinates are always right handed. It's either +Y is down +Z forward, or +Y up +Z back
#ifdef CAMERA_VIEW_SPACE_IS_POSITIVE_Y_DOWN
static HMM_Vec3 Camera_Up(const Camera* camera)        { return HMM_MulV3F(camera->world_from_view.Columns[1].XYZ, -1.f); }
static HMM_Vec3 Camera_Down(const Camera* camera)      { return camera->world_from_view.Columns[1].XYZ; }
static HMM_Vec3 Camera_Forward(const Camera* camera)   { return camera->world_from_view.Columns[2].XYZ; }
#else
static HMM_Vec3 Camera_Up(const Camera* camera)        { return camera->world_from_view.Columns[1].XYZ; }
static HMM_Vec3 Camera_Down(const Camera* camera)      { return HMM_MulV3F(camera->world_from_view.Columns[1].XYZ, -1.f); }
static HMM_Vec3 Camera_Forward(const Camera* camera)   { return HMM_MulV3F(camera->world_from_view.Columns[2].XYZ, -1.f); }
#endif

static HMM_Vec3 Camera_RotateV3(HMM_Quat q, HMM_Vec3 v) {
	// from https://stackoverflow.com/questions/44705398/about-glm-quaternion-rotation
	HMM_Vec3 a = HMM_MulV3F(v, q.W);
	HMM_Vec3 b = HMM_Cross(q.XYZ, v);
	HMM_Vec3 c = HMM_AddV3(b, a);
	HMM_Vec3 d = HMM_Cross(q.XYZ, c);
	return HMM_AddV3(v, HMM_MulV3F(d, 2.f));
}

static void Camera_Update(Camera* camera, Input_Frame* inputs, float movement_speed, float mouse_speed,
	float FOV, float aspect_ratio_x_over_y, float z_near, float z_far)
{
	if (camera->ori.X == 0 && camera->ori.Y == 0 && camera->ori.Z == 0 && camera->ori.W == 0) { // reset ori?

#ifdef CAMERA_VIEW_SPACE_IS_POSITIVE_Y_DOWN
		camera->ori = HMM_QFromAxisAngle_RH(HMM_V3(1, 0, 0), -HMM_PI32 / 2.f); // Rotate the camera to face +Y (zero rotation would be facing +Z)
#else
		camera->ori = HMM_QFromAxisAngle_RH(HMM_V3(1, 0, 0), HMM_PI32 / 2.f); // the HMM_PI32 / 2.f is to rotate the camera to face +Y (zero rotation would be facing -Z)
#endif
		//camera->lazy_ori = camera->ori;
	}

	bool has_focus = Input_IsDown(inputs, Input_Key_MouseRight) ||
		Input_IsDown(inputs, Input_Key_LeftControl) ||
		Input_IsDown(inputs, Input_Key_RightControl);

	if (Input_IsDown(inputs, Input_Key_MouseRight)) {
		// we need to make rotators for the pitch delta and the yaw delta, and then just multiply the camera's orientation with it!

		float yaw_delta = -mouse_speed * (float)inputs->raw_mouse_input[0];
		float pitch_delta = -mouse_speed * (float)inputs->raw_mouse_input[1];

		// So for this, we need to figure out the "right" axis of the camera.
		HMM_Vec3 cam_right_old = Camera_RotateV3(camera->ori, HMM_V3(1, 0, 0));
		HMM_Quat pitch_rotator = HMM_QFromAxisAngle_RH(cam_right_old, pitch_delta);
		camera->ori = HMM_MulQ(pitch_rotator, camera->ori);

		HMM_Quat yaw_rotator = HMM_QFromAxisAngle_RH(HMM_V3(0, 0, 1), yaw_delta);
		camera->ori = HMM_MulQ(yaw_rotator, camera->ori);
		camera->ori = HMM_NormQ(camera->ori);
	}

	if (has_focus) {
		// TODO: have a way to return `Camera_Forward`, `camera_right` and `camera_up` straight from the `world_from_view` matrix
		if (Input_IsDown(inputs, Input_Key_Shift)) {
			movement_speed *= 3.f;
		}
		if (Input_IsDown(inputs, Input_Key_Control)) {
			movement_speed *= 0.1f;
		}
		if (Input_IsDown(inputs, Input_Key_W)) {
			camera->pos = HMM_AddV3(camera->pos,
				HMM_MulV3F(Camera_Forward(camera), movement_speed));
		}
		if (Input_IsDown(inputs, Input_Key_S)) {
			camera->pos = HMM_AddV3(camera->pos,
				HMM_MulV3F(Camera_Forward(camera), -movement_speed));
		}
		if (Input_IsDown(inputs, Input_Key_D)) {
			camera->pos = HMM_AddV3(camera->pos,
				HMM_MulV3F(Camera_Right(camera), movement_speed));
		}
		if (Input_IsDown(inputs, Input_Key_A)) {
			camera->pos = HMM_AddV3(camera->pos,
				HMM_MulV3F(Camera_Right(camera), -movement_speed));
		}
		if (Input_IsDown(inputs, Input_Key_E)) {
			camera->pos = HMM_AddV3(camera->pos,
				HMM_V3(0, 0, movement_speed));
		}
		if (Input_IsDown(inputs, Input_Key_Q)) {
			camera->pos = HMM_AddV3(camera->pos,
				HMM_V3(0, 0, -movement_speed));
		}
	}

	// Smoothly interpolate lazy position and ori
	camera->lazy_pos = HMM_LerpV3(camera->lazy_pos, 0.2f, camera->pos);
	camera->lazy_ori = HMM_SLerp(camera->lazy_ori, 0.2f, camera->ori);
	//camera->lazy_pos = HMM_LerpV3(camera->lazy_pos, 1.f, camera->pos);
	//camera->lazy_ori = HMM_SLerp(camera->lazy_ori, 1.f, camera->ori);

	camera->aspect_ratio = aspect_ratio_x_over_y;
	camera->z_near = z_near;
	camera->z_far = z_far;

#if 1
	camera->world_from_view = HMM_MulM4(HMM_Translate(camera->lazy_pos), HMM_QToM4(camera->lazy_ori));
#else
	camera->world_from_view = HMM_MulM4(HMM_Translate(camera->pos), HMM_QToM4(camera->ori));
#endif
	camera->view_from_world = HMM_InvGeneralM4(camera->world_from_view);//HMM_QToM4(HMM_InvQ(camera->ori)) * HMM_Translate(-1.f * camera->pos);
	
	// "_RH_ZO" means "right handed, zero-to-one NDC z range"
#ifdef CAMERA_VIEW_SPACE_IS_POSITIVE_Y_DOWN
	// Somewhat counter-intuitively, the HMM_Perspective_LH_ZO function is the true right-handed perspective function. RH_ZO requires a Z flip, RH_NO doesn't, and LH_ZO doesn't either.
	camera->clip_from_view = HMM_Perspective_LH_ZO(HMM_AngleDeg(FOV), camera->aspect_ratio, camera->z_near, camera->z_far);
#else
	camera->clip_from_view = HMM_Perspective_RH_ZO(HMM_AngleDeg(FOV), camera->aspect_ratio, camera->z_near, camera->z_far);
#endif

	camera->view_from_clip = HMM_InvGeneralM4(camera->clip_from_view);
		//HMM_InvPerspective_RH(camera->view_to_clip);
	
	camera->clip_from_world = HMM_MulM4(camera->clip_from_view, camera->view_from_world);
	camera->world_from_clip = HMM_InvGeneralM4(camera->clip_from_world);//camera->view_to_world * camera->clip_to_view;
}