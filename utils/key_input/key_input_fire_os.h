
typedef struct Input_OS_State {
	Input_Frame* frame;
	DS_DynArray(Input_Event) events;
} Input_OS_State;

static void Input_OS_BeginEvents(Input_OS_State* state, Input_Frame* frame, DS_Arena* frame_arena) {
	memset(state, 0, sizeof(*state));
	state->frame = frame;
	state->frame->mouse_wheel_input[0] = 0.f;
	state->frame->mouse_wheel_input[1] = 0.f;
	state->frame->raw_mouse_input[0] = 0.f;
	state->frame->raw_mouse_input[1] = 0.f;
	DS_ArrInit(&state->events, frame_arena);
}

static void Input_OS_EndEvents(Input_OS_State* state) {
	state->frame->events = state->events.data;
	state->frame->events_count = state->events.length;
}

static void Input_OS_AddEvent(Input_OS_State* state, const OS_WINDOW_Event* event) {
	if (event->kind == OS_WINDOW_EventKind_Press) {
		Input_Key key = (Input_Key)event->key; // NOTE: OS_Key and Input_Key must be kept in sync!
		state->frame->key_is_down[event->key] = true;
		
		Input_Event input_event = {0};
		input_event.kind = event->is_repeat ? Input_EventKind_Repeat : Input_EventKind_Press;
		input_event.key = key;
		DS_ArrPush(&state->events, input_event);
	}
	if (event->kind == OS_WINDOW_EventKind_Release) {
		Input_Key key = (Input_Key)event->key; // NOTE: OS_Key and Input_Key must be kept in sync!
		state->frame->key_is_down[event->key] = false;

		Input_Event input_event = {0};
		input_event.kind = Input_EventKind_Release;
		input_event.key = key;
		DS_ArrPush(&state->events, input_event);
	}
	if (event->kind == OS_WINDOW_EventKind_TextCharacter) {
		Input_Event input_event = {0};
		input_event.kind = Input_EventKind_TextCharacter;
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
