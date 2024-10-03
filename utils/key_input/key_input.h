// key_input.h - interface for keyboard and mouse input.

typedef enum INPUT_Key {
	INPUT_Key_Invalid = 0,

	INPUT_Key_Space = 32,
	INPUT_Key_Apostrophe = 39,   /* ' */
	INPUT_Key_Comma = 44,        /* , */
	INPUT_Key_Minus = 45,        /* - */
	INPUT_Key_Period = 46,       /* . */
	INPUT_Key_Slash = 47,        /* / */

	INPUT_Key_0 = 48,
	INPUT_Key_1 = 49,
	INPUT_Key_2 = 50,
	INPUT_Key_3 = 51,
	INPUT_Key_4 = 52,
	INPUT_Key_5 = 53,
	INPUT_Key_6 = 54,
	INPUT_Key_7 = 55,
	INPUT_Key_8 = 56,
	INPUT_Key_9 = 57,

	INPUT_Key_Semicolon = 59,    /* ; */
	INPUT_Key_Equal = 61,        /* = */
	INPUT_Key_LeftBracket = 91,  /* [ */
	INPUT_Key_Backslash = 92,    /* \ */
	INPUT_Key_RightBracket = 93, /* ] */
	INPUT_Key_GraveAccent = 96,  /* ` */

	INPUT_Key_A = 65,
	INPUT_Key_B = 66,
	INPUT_Key_C = 67,
	INPUT_Key_D = 68,
	INPUT_Key_E = 69,
	INPUT_Key_F = 70,
	INPUT_Key_G = 71,
	INPUT_Key_H = 72,
	INPUT_Key_I = 73,
	INPUT_Key_J = 74,
	INPUT_Key_K = 75,
	INPUT_Key_L = 76,
	INPUT_Key_M = 77,
	INPUT_Key_N = 78,
	INPUT_Key_O = 79,
	INPUT_Key_P = 80,
	INPUT_Key_Q = 81,
	INPUT_Key_R = 82,
	INPUT_Key_S = 83,
	INPUT_Key_T = 84,
	INPUT_Key_U = 85,
	INPUT_Key_V = 86,
	INPUT_Key_W = 87,
	INPUT_Key_X = 88,
	INPUT_Key_Y = 89,
	INPUT_Key_Z = 90,

	INPUT_Key_Escape = 256,
	INPUT_Key_Enter = 257,
	INPUT_Key_Tab = 258,
	INPUT_Key_Backspace = 259,
	INPUT_Key_Insert = 260,
	INPUT_Key_Delete = 261,
	INPUT_Key_Right = 262,
	INPUT_Key_Left = 263,
	INPUT_Key_Down = 264,
	INPUT_Key_Up = 265,
	INPUT_Key_PageUp = 266,
	INPUT_Key_PageDown = 267,
	INPUT_Key_Home = 268,
	INPUT_Key_End = 269,
	INPUT_Key_CapsLock = 280,
	INPUT_Key_ScrollLock = 281,
	INPUT_Key_NumLock = 282,
	INPUT_Key_PrintScreen = 283,
	INPUT_Key_Pause = 284,

	INPUT_Key_F1 = 290,
	INPUT_Key_F2 = 291,
	INPUT_Key_F3 = 292,
	INPUT_Key_F4 = 293,
	INPUT_Key_F5 = 294,
	INPUT_Key_F6 = 295,
	INPUT_Key_F7 = 296,
	INPUT_Key_F8 = 297,
	INPUT_Key_F9 = 298,
	INPUT_Key_F10 = 299,
	INPUT_Key_F11 = 300,
	INPUT_Key_F12 = 301,
	INPUT_Key_F13 = 302,
	INPUT_Key_F14 = 303,
	INPUT_Key_F15 = 304,
	INPUT_Key_F16 = 305,
	INPUT_Key_F17 = 306,
	INPUT_Key_F18 = 307,
	INPUT_Key_F19 = 308,
	INPUT_Key_F20 = 309,
	INPUT_Key_F21 = 310,
	INPUT_Key_F22 = 311,
	INPUT_Key_F23 = 312,
	INPUT_Key_F24 = 313,
	INPUT_Key_F25 = 314,

	INPUT_Key_KP_0 = 320,
	INPUT_Key_KP_1 = 321,
	INPUT_Key_KP_2 = 322,
	INPUT_Key_KP_3 = 323,
	INPUT_Key_KP_4 = 324,
	INPUT_Key_KP_5 = 325,
	INPUT_Key_KP_6 = 326,
	INPUT_Key_KP_7 = 327,
	INPUT_Key_KP_8 = 328,
	INPUT_Key_KP_9 = 329,

	INPUT_Key_KP_Decimal = 330,
	INPUT_Key_KP_Divide = 331,
	INPUT_Key_KP_Multiply = 332,
	INPUT_Key_KP_Subtract = 333,
	INPUT_Key_KP_Add = 334,
	INPUT_Key_KP_Enter = 335,
	INPUT_Key_KP_Equal = 336,

	INPUT_Key_LeftShift = 340,
	INPUT_Key_LeftControl = 341,
	INPUT_Key_LeftAlt = 342,
	INPUT_Key_LeftSuper = 343,
	INPUT_Key_RightShift = 344,
	INPUT_Key_RightControl = 345,
	INPUT_Key_RightAlt = 346,
	INPUT_Key_RightSuper = 347,
	INPUT_Key_Menu = 348,

	// Events for these four modifier keys shouldn't be generated, nor should they be used in the `key_is_down` table.
	// They're purely for convenience when calling the INPUT_IsDown/WentDown/WentDownOrRepeat/WentUp functions.
	INPUT_Key_Shift = 349,
	INPUT_Key_Control = 350,
	INPUT_Key_Alt = 351,
	INPUT_Key_Super = 352,

	INPUT_Key_MouseLeft = 353,
	INPUT_Key_MouseRight = 354,
	INPUT_Key_MouseMiddle = 355,
	INPUT_Key_Mouse_4 = 356,
	INPUT_Key_Mouse_5 = 357,
	INPUT_Key_Mouse_6 = 358,
	INPUT_Key_Mouse_7 = 359,
	INPUT_Key_Mouse_8 = 360,

	INPUT_Key_COUNT,
} INPUT_Key;

typedef uint8_t INPUT_EventKind;
enum {
	INPUT_EventKind_Press,
	INPUT_EventKind_Repeat,
	INPUT_EventKind_Release,
	INPUT_EventKind_TextCharacter,
};

typedef struct INPUT_Event {
	INPUT_EventKind kind;
	uint8_t mouse_click_index; // 0 for regular click, 1 for double-click, 2 for triple-click
	union {
		INPUT_Key key; // for Press, Repeat, Release events
		unsigned int text_character; // unicode character for TextCharacter event
	};
} INPUT_Event;

typedef struct INPUT_Frame {
	INPUT_Event* events;
	int events_count;
	bool key_is_down[INPUT_Key_COUNT]; // This is the key down state after the events for this frame have been applied
	float mouse_wheel_input[2]; // +1.0 means the wheel was rotated forward by one detent (scroll step)
	float raw_mouse_input[2];
} INPUT_Frame;

static inline bool INPUT_IsDown(const INPUT_Frame* input, INPUT_Key key);
static inline bool INPUT_WentDown(const INPUT_Frame* input, INPUT_Key key);
static inline bool INPUT_WentDownOrRepeat(const INPUT_Frame* input, INPUT_Key key);
static inline bool INPUT_WentUp(const INPUT_Frame* input, INPUT_Key key);
static inline bool INPUT_DoubleClicked(const INPUT_Frame* input);
static inline bool INPUT_TripleClicked(const INPUT_Frame* input);

// ---------------------------------------------------------------------------------

static inline bool INPUT_KeyIsA(INPUT_Key key, INPUT_Key other_key) {
	if (key == other_key) return true;
	if (other_key >= INPUT_Key_Shift && other_key <= INPUT_Key_Super) {
		switch (other_key) {
		case INPUT_Key_Shift:    return key == INPUT_Key_LeftShift   || key == INPUT_Key_RightShift;
		case INPUT_Key_Control:  return key == INPUT_Key_LeftControl || key == INPUT_Key_RightControl;
		case INPUT_Key_Alt:      return key == INPUT_Key_LeftAlt     || key == INPUT_Key_RightAlt;
		case INPUT_Key_Super:    return key == INPUT_Key_LeftSuper   || key == INPUT_Key_RightSuper;
		default: break;
		}
	}
	return false;
}

static inline bool INPUT_IsDown(const INPUT_Frame* input, INPUT_Key key) {
	if (key >= INPUT_Key_Shift && key <= INPUT_Key_Super) {
		switch (key) {
		case INPUT_Key_Shift:    return input->key_is_down[INPUT_Key_LeftShift]   || input->key_is_down[INPUT_Key_RightShift];
		case INPUT_Key_Control:  return input->key_is_down[INPUT_Key_LeftControl] || input->key_is_down[INPUT_Key_RightControl];
		case INPUT_Key_Alt:      return input->key_is_down[INPUT_Key_LeftAlt]     || input->key_is_down[INPUT_Key_RightAlt];
		case INPUT_Key_Super:    return input->key_is_down[INPUT_Key_LeftSuper]   || input->key_is_down[INPUT_Key_RightSuper];
		default: break;
		}
	}
	return input->key_is_down[key];
};

static inline bool INPUT_DoubleClicked(const INPUT_Frame* input) {
	for (int i = 0; i < input->events_count; i++) {
		if (input->events[i].kind == INPUT_EventKind_Press &&
			input->events[i].mouse_click_index == 1) return true;
	}
	return false;
}

static inline bool INPUT_TripleClicked(const INPUT_Frame* input) {
	for (int i = 0; i < input->events_count; i++) {
		if (input->events[i].kind == INPUT_EventKind_Press &&
			input->events[i].mouse_click_index == 2) return true;
	}
	return false;
}

static inline bool INPUT_WentDown(const INPUT_Frame* input, INPUT_Key key) {
	if (INPUT_IsDown(input, key)) {
		for (int i = 0; i < input->events_count; i++) {
			if (input->events[i].kind == INPUT_EventKind_Press && INPUT_KeyIsA(input->events[i].key, key)) {
				return true;
			}
		}
	}
	return false;
}

static inline bool INPUT_WentDownOrRepeat(const INPUT_Frame* input, INPUT_Key key) {
	if (INPUT_IsDown(input, key)) {
		for (int i = 0; i < input->events_count; i++) {
			if ((input->events[i].kind == INPUT_EventKind_Press || input->events[i].kind == INPUT_EventKind_Repeat) && INPUT_KeyIsA(input->events[i].key, key)) {
				return true;
			}
		}
	}
	return false;
}

static inline bool INPUT_WentUp(const INPUT_Frame* input, INPUT_Key key) {
	if (!INPUT_IsDown(input, key)) {
		for (int i = 0; i < input->events_count; i++) {
			if (input->events[i].kind == INPUT_EventKind_Release && INPUT_KeyIsA(input->events[i].key, key)) {
				return true;
			}
		}
	}
	return false;
}
