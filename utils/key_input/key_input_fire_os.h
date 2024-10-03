
typedef struct INPUT_OS_Events {
	INPUT_Frame* frame;
	DS_DynArray(INPUT_Event) events;
} INPUT_OS_Events;

static void INPUT_OS_BeginEvents(INPUT_OS_Events* state, INPUT_Frame* frame, DS_Arena* frame_arena) {
	memset(state, 0, sizeof(*state));
	state->frame = frame;
	state->frame->mouse_wheel_input[0] = 0.f;
	state->frame->mouse_wheel_input[1] = 0.f;
	state->frame->raw_mouse_input[0] = 0.f;
	state->frame->raw_mouse_input[1] = 0.f;
	DS_ArrInit(&state->events, frame_arena);
}

static void INPUT_OS_EndEvents(INPUT_OS_Events* state) {
	state->frame->events = state->events.data;
	state->frame->events_count = state->events.count;
}

static void INPUT_OS_AddEvent(INPUT_OS_Events* state, const OS_WINDOW_Event* event) {
	if (event->kind == OS_WINDOW_EventKind_Press) {
		INPUT_Key key = (INPUT_Key)event->key; // NOTE: OS_Key and INPUT_Key must be kept in sync!
		state->frame->key_is_down[event->key] = true;
		
		INPUT_Event input_event = {0};
		input_event.kind = event->is_repeat ? INPUT_EventKind_Repeat : INPUT_EventKind_Press;
		input_event.key = key;
		input_event.mouse_click_index = event->mouse_click_index;
		DS_ArrPush(&state->events, input_event);
	}
	if (event->kind == OS_WINDOW_EventKind_Release) {
		INPUT_Key key = (INPUT_Key)event->key; // NOTE: OS_Key and INPUT_Key must be kept in sync!
		state->frame->key_is_down[event->key] = false;

		INPUT_Event input_event = {0};
		input_event.kind = INPUT_EventKind_Release;
		input_event.key = key;
		DS_ArrPush(&state->events, input_event);
	}
	if (event->kind == OS_WINDOW_EventKind_TextCharacter) {
		INPUT_Event input_event = {0};
		input_event.kind = INPUT_EventKind_TextCharacter;
		input_event.text_character = event->text_character;
		DS_ArrPush(&state->events, input_event);
	}
	if (event->kind == OS_WINDOW_EventKind_MouseWheel) {
		state->frame->mouse_wheel_input[1] += event->mouse_wheel;
	}
	if (event->kind == OS_WINDOW_EventKind_RawMouseInput) {
		state->frame->raw_mouse_input[0] += event->raw_mouse_input[0];
		state->frame->raw_mouse_input[1] += event->raw_mouse_input[1];
	}
}
