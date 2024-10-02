
static void UI_INPUT_ApplyInputs(UI_Inputs* ui_inputs, INPUT_Frame* inputs) {
	memset(ui_inputs, 0, sizeof(*ui_inputs));
	
	for (int i = 0; i < inputs->events_count; i++) {
		INPUT_Event* event = &inputs->events[i];
		if (event->kind == INPUT_EventKind_Press || event->kind == INPUT_EventKind_Repeat || event->kind == INPUT_EventKind_Release) {
			UI_Input ui_input = UI_Input_Invalid;
			switch (event->key) {
			default: break;
			case INPUT_Key_MouseLeft: { ui_input = UI_Input_MouseLeft; } break;
			case INPUT_Key_MouseRight: { ui_input = UI_Input_MouseRight; } break;
			case INPUT_Key_MouseMiddle: { ui_input = UI_Input_MouseMiddle; } break;
			case INPUT_Key_LeftShift: { ui_input = UI_Input_Shift; } break;
			case INPUT_Key_RightShift: { ui_input = UI_Input_Shift; } break;
			case INPUT_Key_LeftControl: { ui_input = UI_Input_Control; } break;
			case INPUT_Key_RightControl: { ui_input = UI_Input_Control; } break;
			case INPUT_Key_LeftAlt: { ui_input = UI_Input_Alt; } break;
			case INPUT_Key_RightAlt: { ui_input = UI_Input_Alt; } break;
			case INPUT_Key_Tab: { ui_input = UI_Input_Tab; } break;
			case INPUT_Key_Escape: { ui_input = UI_Input_Escape; } break;
			case INPUT_Key_Enter: { ui_input = UI_Input_Enter; } break;
			case INPUT_Key_Delete: { ui_input = UI_Input_Delete; } break;
			case INPUT_Key_Backspace: { ui_input = UI_Input_Backspace; } break;
			case INPUT_Key_A: { ui_input = UI_Input_A; } break;
			case INPUT_Key_C: { ui_input = UI_Input_C; } break;
			case INPUT_Key_V: { ui_input = UI_Input_V; } break;
			case INPUT_Key_X: { ui_input = UI_Input_X; } break;
			case INPUT_Key_Y: { ui_input = UI_Input_Y; } break;
			case INPUT_Key_Z: { ui_input = UI_Input_Z; } break;
			case INPUT_Key_Home: { ui_input = UI_Input_Home; } break;
			case INPUT_Key_End: { ui_input = UI_Input_End; } break;
			case INPUT_Key_Left: { ui_input = UI_Input_Left; } break;
			case INPUT_Key_Right: { ui_input = UI_Input_Right; } break;
			case INPUT_Key_Up: { ui_input = UI_Input_Up; } break;
			case INPUT_Key_Down: { ui_input = UI_Input_Down; } break;
			}
			if (ui_input) {
				if (event->kind == INPUT_EventKind_Press) {
					ui_inputs->input_events[ui_input] |= UI_InputEvent_Press | UI_InputEvent_PressOrRepeat;
				} else if (event->kind == INPUT_EventKind_Repeat) {
					ui_inputs->input_events[ui_input] |= UI_InputEvent_PressOrRepeat;
				} else {
					ui_inputs->input_events[ui_input] |= UI_InputEvent_Release;
				}
			}
		}
		if (event->kind == INPUT_EventKind_TextCharacter) {
			if (ui_inputs->text_input_utf32_length < DS_ArrayCount(ui_inputs->text_input_utf32)) {
				ui_inputs->text_input_utf32[ui_inputs->text_input_utf32_length] = event->text_character;
				ui_inputs->text_input_utf32_length++;
			}
		}
	}
	
	ui_inputs->mouse_wheel_delta = inputs->mouse_wheel_input[1];
	ui_inputs->mouse_raw_delta = UI_VEC2{inputs->raw_mouse_input[0], inputs->raw_mouse_input[1]};
}
