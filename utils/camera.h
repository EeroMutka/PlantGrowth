
struct Camera {
	HMM_Vec3 pos;
	HMM_Quat ori;
	
	HMM_Vec3 lazy_pos;
	HMM_Quat lazy_ori;

	float aspect_ratio, z_near, z_far;

	HMM_PerspectiveCamera cached; // Cached in `Camera_Update`
};

static void Camera_Update(Camera* camera, INPUT_Frame* inputs, float movement_speed, float mouse_speed,
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

	bool has_focus = INPUT_IsDown(inputs, INPUT_Key_MouseRight) ||
		INPUT_IsDown(inputs, INPUT_Key_LeftControl) ||
		INPUT_IsDown(inputs, INPUT_Key_RightControl);

	if (INPUT_IsDown(inputs, INPUT_Key_MouseRight)) {
		// we need to make rotators for the pitch delta and the yaw delta, and then just multiply the camera's orientation with it!

		float yaw_delta = -mouse_speed * (float)inputs->raw_mouse_input[0];
		float pitch_delta = -mouse_speed * (float)inputs->raw_mouse_input[1];

		// So for this, we need to figure out the "right" axis of the camera.
		HMM_Vec3 cam_right_old = HMM_RotateV3(HMM_V3(1, 0, 0), camera->ori);
		HMM_Quat pitch_rotator = HMM_QFromAxisAngle_RH(cam_right_old, pitch_delta);
		camera->ori = HMM_MulQ(pitch_rotator, camera->ori);

		HMM_Quat yaw_rotator = HMM_QFromAxisAngle_RH(HMM_V3(0, 0, 1), yaw_delta);
		camera->ori = HMM_MulQ(yaw_rotator, camera->ori);
		camera->ori = HMM_NormQ(camera->ori);
	}

	if (has_focus) {
		// TODO: have a way to return `Camera_Forward`, `camera_right` and `camera_up` straight from the `world_from_view` matrix
		if (INPUT_IsDown(inputs, INPUT_Key_Shift)) {
			movement_speed *= 3.f;
		}
		if (INPUT_IsDown(inputs, INPUT_Key_Control)) {
			movement_speed *= 0.1f;
		}
		if (INPUT_IsDown(inputs, INPUT_Key_W)) {
			camera->pos += camera->cached.forward_dir * movement_speed;
		}
		if (INPUT_IsDown(inputs, INPUT_Key_S)) {
			camera->pos += camera->cached.forward_dir * -movement_speed;
		}
		if (INPUT_IsDown(inputs, INPUT_Key_D)) {
			camera->pos += camera->cached.right_dir * movement_speed;
		}
		if (INPUT_IsDown(inputs, INPUT_Key_A)) {
			camera->pos += camera->cached.right_dir * -movement_speed;
		}
		if (INPUT_IsDown(inputs, INPUT_Key_E)) {
			camera->pos += HMM_V3(0, 0, movement_speed);
		}
		if (INPUT_IsDown(inputs, INPUT_Key_Q)) {
			camera->pos += HMM_V3(0, 0, -movement_speed);
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

	HMM_Mat4 world_from_view = HMM_MulM4(HMM_Translate(camera->lazy_pos), HMM_QToM4(camera->lazy_ori));
	HMM_Mat4 view_from_world = HMM_InvGeneralM4(world_from_view);//HMM_QToM4(HMM_InvQ(camera->ori)) * HMM_Translate(-1.f * camera->pos);
	
	// "_RH_ZO" means "right handed, zero-to-one NDC z range"
#ifdef CAMERA_VIEW_SPACE_IS_POSITIVE_Y_DOWN
	// Somewhat counter-intuitively, the HMM_Perspective_LH_ZO function is the true right-handed perspective function. RH_ZO requires a Z flip, RH_NO doesn't, and LH_ZO doesn't either.
	HMM_Mat4 clip_from_view = HMM_Perspective_LH_ZO(HMM_AngleDeg(FOV), camera->aspect_ratio, camera->z_near, camera->z_far);
#else
	HMM_Mat4 clip_from_view = HMM_Perspective_RH_ZO(HMM_AngleDeg(FOV), camera->aspect_ratio, camera->z_near, camera->z_far);
#endif

	HMM_Mat4 view_from_clip = HMM_InvGeneralM4(clip_from_view);
	
	HMM_Mat4 clip_from_world = HMM_MulM4(clip_from_view, view_from_world);
	HMM_Mat4 world_from_clip = HMM_InvGeneralM4(clip_from_world); //camera->view_to_world * camera->clip_to_view;

	camera->cached.clip_from_world = clip_from_world;
	camera->cached.world_from_clip = world_from_clip;
	camera->cached.position = camera->lazy_pos;
	#ifdef CAMERA_VIEW_SPACE_IS_POSITIVE_Y_DOWN
	camera->cached.right_dir = world_from_view.Columns[0].XYZ;
	camera->cached.up_dir = world_from_view.Columns[1].XYZ * -1.f;
	camera->cached.forward_dir = world_from_view.Columns[2].XYZ;
	#else
	camera->cached.right_dir = world_from_view.Columns[0].XYZ;
	camera->cached.up_dir = world_from_view.Columns[1].XYZ;
	camera->cached.forward_dir = world_from_view.Columns[2].XYZ * -1.f;
	#endif
}