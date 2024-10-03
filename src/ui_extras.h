
static void UI_CurveInit(Curve* curve, DS_Allocator* allocator) {
	DS_ArrInit(&curve->points, allocator);
}

static void UI_CurveDeinit(Curve* curve) {
	DS_ArrDeinit(&curve->points);
}

static void UI_PlugValCurve(UI_Box* box, Curve* curve) {
	// for drawing, we need the actual curve.
	//UI_DrawRect(box->computed_rect, UI_PINK);
	//UI_DrawBoxDefault(box);

	struct RetainedData {
		int selected_point_num;
	};
	RetainedData* data;
	UI_BoxGetRetainedVar(box, UI_KEY(), &data);

	UI_DrawRectLines(box->computed_rect, 2.f, UI_BLACK);

	if (UI_InputWasPressed(UI_Input_MouseLeft) && data->selected_point_num != 0) {
		data->selected_point_num = 0;
	}

	if (UI_InputWasPressed(UI_Input_Delete) && data->selected_point_num && curve->points.count > 2) {
		DS_ArrRemove(&curve->points, data->selected_point_num - 1);
		data->selected_point_num = 0;
	}

	if (UI_DoubleClickedAnywhere() && UI_PointIsInRect(box->computed_rect, UI_STATE.mouse_pos)) {
		for (int i = 0; i < curve->points.count; i++) {
			HMM_Vec2* curve_p = &curve->points[i];
			float x = curve_p->X*(box->computed_rect.max.x - box->computed_rect.min.x) + box->computed_rect.min.x; // lerp
			if (UI_STATE.mouse_pos.x < x) { // add before this point!
				HMM_Vec2 new_curve_p = {curve_p->X, 0.f};
				DS_ArrInsert(&curve->points, i, new_curve_p);
				data->selected_point_num = i + 1;
				break;
			}
		}
	}

	UI_Vec2 prev_p = {0, 0};
	float prev_curve_p_x = 0.f;
	for (int i = 0; i < curve->points.count; i++) {
		HMM_Vec2* curve_p = &curve->points[i];

		UI_Vec2 p = {
			curve_p->X*(box->computed_rect.max.x - box->computed_rect.min.x) + box->computed_rect.min.x, // lerp
			(1.f - curve_p->Y)*(box->computed_rect.max.y - box->computed_rect.min.y) + box->computed_rect.min.y, // lerp
		};

		bool is_hovered = false;
		if (data->selected_point_num == 0) {
			UI_Vec2 p_to_mouse = UI_SubV2(UI_STATE.mouse_pos, p);
			is_hovered = p_to_mouse.x*p_to_mouse.x + p_to_mouse.y*p_to_mouse.y < 5.f*5.f;
			if (is_hovered) {
				if (UI_InputWasPressed(UI_Input_MouseLeft)) {
					data->selected_point_num = i + 1;
				}
			}
		}

		bool is_selected = data->selected_point_num == i + 1;

		if (is_selected && UI_InputIsDown(UI_Input_MouseLeft)) { // Dragging
			p = UI_STATE.mouse_pos;
			p.x = UI_Max(UI_Min(p.x, box->computed_rect.max.x), box->computed_rect.min.x);
			p.y = UI_Max(UI_Min(p.y, box->computed_rect.max.y), box->computed_rect.min.y);

			curve_p->X = (p.x - box->computed_rect.min.x) / (box->computed_rect.max.x - box->computed_rect.min.x); // inverse-lerp
			curve_p->Y = 1.f - (p.y - box->computed_rect.min.y) / (box->computed_rect.max.y - box->computed_rect.min.y); // inverse-lerp
		}

		if (i == 0) curve_p->X = 0.f; // snap start to 0
		if (i == curve->points.count - 1) curve_p->X = 1.f; // snap end to 1

		if (curve_p->X < prev_curve_p_x) {
			curve_p->X = prev_curve_p_x;
		}
		p.x = curve_p->X*(box->computed_rect.max.x - box->computed_rect.min.x) + box->computed_rect.min.x; // lerp

		if (i > 0) {
			UI_DrawLine(prev_p, p, 2.f, UI_LIME);
		}

		float r = is_hovered || is_selected ? 4.f : 3.f;
		UI_DrawRect({UI_SubV2(p, UI_VEC2{r, r}), UI_AddV2(p, UI_VEC2{r, r})}, is_selected ? UI_WHITE : UI_LIME);
		//UI_DrawCircle(p, r, 8, is_selected ? UI_WHITE : (is_hovered ? UI_LIGHTGRAY : UI_GRAY));
		prev_p = p;
		prev_curve_p_x = curve_p->X;
	}
}

// IDEA: The curve UI could be used for arbitrary plotting and for example outputting profiling data!
// And an animation curves editor.

/*
static void UI_AddValCurve(UI_Key key, UI_Curve* curve, UI_Size w, UI_Size h) {
	UI_Box* box = UI_AddBox(key, w, h, UI_BoxFlag_DrawBorder);
	//box->draw = UI_ValCurveDraw;
	//box->draw_args_custom = curve;

	// hmm... the curve pointer must stay valid for the entire frame.
	// That sucks.
	// I think the "fire-UI" way to deal with this is to use the prev frame rect for curve editing inputs.

	// is there some way I could make this easier? The biggest annoyance here is that I have to deep-clone all the data into the frame arena and do drawing separately from input handling.
	// To solve this, we *need* the rect when calling this function. And usually, it is obtainable! Only with smart layout it isn't.
	// New UI lib API idea: smart layouting

	UI_Box* root = UI_BeginLayout();
	UI_Box* my_box_1 = UI_AddBox(root, "flex", "none");
	UI_Box* my_box_2 = UI_AddBox(root, "fit", "none");
	UI_EndLayout(root);

	UI_PopulateBoxToCurve(my_box_1->rect)
}*/