
struct Curve {
	// For now, just support linear interpolation. In the future I want this to support bezier curves and catmull-rom splines
	// The points are points between (0, 0) and (1, 1) and must be sorted along the X axis from left to right.
	DS_DynArray(HMM_Vec2) points;
};

static float CurveEvalAtX(const Curve* curve, float x) {
	for (int i = 1; i < curve->points.count; i++) {
		HMM_Vec2 p = curve->points[i];
		if (x <= curve->points[i].X) {
			HMM_Vec2 prev_p = curve->points[i - 1];
			float t = (x - prev_p.X) / (p.X - prev_p.X);
			float y = prev_p.Y + t * (p.Y - prev_p.Y);
			return y;
		}
	}
	return DS_ArrPeek(curve->points).Y;
}
