
typedef struct CanvasViewer {
	UI_Vec2 window_center;
	UI_Vec2 center; // in canvas space
	float zoom;

	// These are updated in CanvasViewerUpdate
	UI_Vec2 mouse_pos_last_frame;
	UI_Rect screen_rect;
} CanvasViewer;

static const CanvasViewer CanvasViewer_Default = {{0, 0}, {0, 0}, 1.f, {0, 0}};

static UI_Vec2 CanvasToScreen(const CanvasViewer *viewer, UI_Vec2 p_canvas) {
	UI_Vec2 p_screen;
	p_screen.x = (p_canvas.x - viewer->center.x) * viewer->zoom + viewer->window_center.x;
	p_screen.y = (p_canvas.y - viewer->center.y) * viewer->zoom + viewer->window_center.y;
	p_screen.y = viewer->screen_rect.max.y - p_screen.y; // left-handed screen coordinates
	return p_screen;
}

static UI_Vec2 ScreenToCanvas(const CanvasViewer *viewer, UI_Vec2 p_screen) {
	p_screen.y = viewer->screen_rect.max.y - p_screen.y; // left-handed screen coordinates
	UI_Vec2 result;
	result.x = (p_screen.x - viewer->window_center.x) / viewer->zoom + viewer->center.x;
	result.y = (p_screen.y - viewer->window_center.y) / viewer->zoom + viewer->center.y;
	return result;
}

static void CanvasViewerUpdate(CanvasViewer *viewer, const OS_Inputs *inputs, UI_Rect screen_rect) {
	viewer->window_center.x = 0.5f*(screen_rect.min.x + screen_rect.max.x);
	viewer->window_center.y = 0.5f*(screen_rect.min.y + screen_rect.max.y);
	viewer->screen_rect = screen_rect;
	
	UI_Vec2 mouse_pos_canvas_space_old = ScreenToCanvas(viewer, viewer->mouse_pos_last_frame);
	UI_Vec2 mouse_pos_canvas_space = ScreenToCanvas(viewer, UI_STATE.mouse_pos);
	
	if (inputs->mouse_wheel_delta != 0) {
		viewer->zoom /= 1.f - 0.1f*inputs->mouse_wheel_delta;
	}
	
	if (OS_InputIsDown(inputs, OS_Input_MouseRight)) {
		UI_Vec2 delta = UI_SubV2(mouse_pos_canvas_space, mouse_pos_canvas_space_old);
		viewer->center = UI_SubV2(viewer->center, delta);
	}
	
	viewer->mouse_pos_last_frame = UI_STATE.mouse_pos;
}

static void CanvasViewerGetViewportMinMax(const CanvasViewer *viewer, UI_Vec2 *out_min, UI_Vec2 *out_max) {
	*out_min = ScreenToCanvas(viewer, UI_VEC2{viewer->screen_rect.min.x, viewer->screen_rect.max.y});
	*out_max = ScreenToCanvas(viewer, UI_VEC2{viewer->screen_rect.max.x, viewer->screen_rect.min.y});
}

// CanvasViewerUpdate must be called BEFORE this
static void CanvasViewerDrawGrid(const CanvasViewer *viewer, int grid_unit, UI_Color color) {
	// Let's say draw a grid of lines per every 1 canvas unit.
	
	UI_Vec2 min, max;
	CanvasViewerGetViewportMinMax(viewer, &min, &max);

	{
		int min_line = (int)floorf(min.x/grid_unit);
		int max_line = (int)ceilf(max.x/grid_unit);
	
		for (int i = min_line; i <= max_line; i++) {
			float x_value = CanvasToScreen(viewer, UI_VEC2{(float)i * grid_unit, 0}).x;
		
			UI_Rect line_rect = {{x_value - 1.f, viewer->screen_rect.min.y}, {x_value + 1.f, viewer->screen_rect.max.y}};
			UI_DrawRect(line_rect, color, NULL);
		}
	}

	{
		int min_line = (int)floorf(min.y/grid_unit);
		int max_line = (int)ceilf(max.y/grid_unit);

		for (int i = min_line; i <= max_line; i++) {
			float y_value = CanvasToScreen(viewer, UI_VEC2{0, (float)i * grid_unit}).y;

			UI_Rect line_rect = {{viewer->screen_rect.min.x, y_value - 1.f}, {viewer->screen_rect.max.x, y_value + 1.f}};
			UI_DrawRect(line_rect, color, NULL);
		}
	}
	
}
