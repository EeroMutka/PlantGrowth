// A common interface for keyboard, mouse and gamepad input.

typedef enum Input_Key {
	Input_Key_Invalid = 0,

	Input_Key_Space = 32,
	Input_Key_Apostrophe = 39,   /* ' */
	Input_Key_Comma = 44,        /* , */
	Input_Key_Minus = 45,        /* - */
	Input_Key_Period = 46,       /* . */
	Input_Key_Slash = 47,        /* / */

	Input_Key_0 = 48,
	Input_Key_1 = 49,
	Input_Key_2 = 50,
	Input_Key_3 = 51,
	Input_Key_4 = 52,
	Input_Key_5 = 53,
	Input_Key_6 = 54,
	Input_Key_7 = 55,
	Input_Key_8 = 56,
	Input_Key_9 = 57,

	Input_Key_Semicolon = 59,    /* ; */
	Input_Key_Equal = 61,        /* = */
	Input_Key_LeftBracket = 91,  /* [ */
	Input_Key_Backslash = 92,    /* \ */
	Input_Key_RightBracket = 93, /* ] */
	Input_Key_GraveAccent = 96,  /* ` */

	Input_Key_A = 65,
	Input_Key_B = 66,
	Input_Key_C = 67,
	Input_Key_D = 68,
	Input_Key_E = 69,
	Input_Key_F = 70,
	Input_Key_G = 71,
	Input_Key_H = 72,
	Input_Key_I = 73,
	Input_Key_J = 74,
	Input_Key_K = 75,
	Input_Key_L = 76,
	Input_Key_M = 77,
	Input_Key_N = 78,
	Input_Key_O = 79,
	Input_Key_P = 80,
	Input_Key_Q = 81,
	Input_Key_R = 82,
	Input_Key_S = 83,
	Input_Key_T = 84,
	Input_Key_U = 85,
	Input_Key_V = 86,
	Input_Key_W = 87,
	Input_Key_X = 88,
	Input_Key_Y = 89,
	Input_Key_Z = 90,

	Input_Key_Escape = 256,
	Input_Key_Enter = 257,
	Input_Key_Tab = 258,
	Input_Key_Backspace = 259,
	Input_Key_Insert = 260,
	Input_Key_Delete = 261,
	Input_Key_Right = 262,
	Input_Key_Left = 263,
	Input_Key_Down = 264,
	Input_Key_Up = 265,
	Input_Key_PageUp = 266,
	Input_Key_PageDown = 267,
	Input_Key_Home = 268,
	Input_Key_End = 269,
	Input_Key_CapsLock = 280,
	Input_Key_ScrollLock = 281,
	Input_Key_NumLock = 282,
	Input_Key_PrintScreen = 283,
	Input_Key_Pause = 284,

	Input_Key_F1 = 290,
	Input_Key_F2 = 291,
	Input_Key_F3 = 292,
	Input_Key_F4 = 293,
	Input_Key_F5 = 294,
	Input_Key_F6 = 295,
	Input_Key_F7 = 296,
	Input_Key_F8 = 297,
	Input_Key_F9 = 298,
	Input_Key_F10 = 299,
	Input_Key_F11 = 300,
	Input_Key_F12 = 301,
	Input_Key_F13 = 302,
	Input_Key_F14 = 303,
	Input_Key_F15 = 304,
	Input_Key_F16 = 305,
	Input_Key_F17 = 306,
	Input_Key_F18 = 307,
	Input_Key_F19 = 308,
	Input_Key_F20 = 309,
	Input_Key_F21 = 310,
	Input_Key_F22 = 311,
	Input_Key_F23 = 312,
	Input_Key_F24 = 313,
	Input_Key_F25 = 314,

	Input_Key_KP_0 = 320,
	Input_Key_KP_1 = 321,
	Input_Key_KP_2 = 322,
	Input_Key_KP_3 = 323,
	Input_Key_KP_4 = 324,
	Input_Key_KP_5 = 325,
	Input_Key_KP_6 = 326,
	Input_Key_KP_7 = 327,
	Input_Key_KP_8 = 328,
	Input_Key_KP_9 = 329,

	Input_Key_KP_Decimal = 330,
	Input_Key_KP_Divide = 331,
	Input_Key_KP_Multiply = 332,
	Input_Key_KP_Subtract = 333,
	Input_Key_KP_Add = 334,
	Input_Key_KP_Enter = 335,
	Input_Key_KP_Equal = 336,

	Input_Key_LeftShift = 340,
	Input_Key_LeftControl = 341,
	Input_Key_LeftAlt = 342,
	Input_Key_LeftSuper = 343,
	Input_Key_RightShift = 344,
	Input_Key_RightControl = 345,
	Input_Key_RightAlt = 346,
	Input_Key_RightSuper = 347,
	Input_Key_Menu = 348,

	// Events for these four modifier keys shouldn't be generated, nor should they be used in the `key_is_down` table.
	// They're purely for convenience when calling the Input_IsDown/WentDown/WentDownOrRepeat/WentUp functions.
	Input_Key_Shift = 349,
	Input_Key_Control = 350,
	Input_Key_Alt = 351,
	Input_Key_Super = 352,

	Input_Key_MouseLeft = 353,
	Input_Key_MouseRight = 354,
	Input_Key_MouseMiddle = 355,
	Input_Key_Mouse_4 = 356,
	Input_Key_Mouse_5 = 357,
	Input_Key_Mouse_6 = 358,
	Input_Key_Mouse_7 = 359,
	Input_Key_Mouse_8 = 360,

	Input_Key_COUNT,
} Input_Key;

typedef enum Input_EventKind {
	Input_EventKind_Press,
	Input_EventKind_Repeat,
	Input_EventKind_Release,
	Input_EventKind_TextCharacter,
} Input_EventKind;

typedef struct Input_Event {
	Input_EventKind kind;
	union {
		Input_Key key; // for Press, Repeat, Release events
		unsigned int text_character; // unicode character for TextCharacter event
	};
} Input_Event;

typedef struct Input_Frame {
	Input_Event* events;
	int events_count;
	bool key_is_down[Input_Key_COUNT]; // This is the key down state after the events for this frame have been applied
	float mouse_wheel_input[2]; // +1.0 means the wheel was rotated forward by one detent (scroll step)
	float raw_mouse_input[2];
} Input_Frame;

static inline bool Input_IsDown(const Input_Frame* input, Input_Key key);
static inline bool Input_WentDown(const Input_Frame* input, Input_Key key);
static inline bool Input_WentDownOrRepeat(const Input_Frame* input, Input_Key key);
static inline bool Input_WentUp(const Input_Frame* input, Input_Key key);

// ---------------------------------------------------------------------------------

static inline bool Input_KeyIsA(Input_Key key, Input_Key other_key) {
	if (key == other_key) return true;
	if (other_key >= Input_Key_Shift && other_key <= Input_Key_Super) {
		switch (other_key) {
		case Input_Key_Shift:    return key == Input_Key_LeftShift   || key == Input_Key_RightShift;
		case Input_Key_Control:  return key == Input_Key_LeftControl || key == Input_Key_RightControl;
		case Input_Key_Alt:      return key == Input_Key_LeftAlt     || key == Input_Key_RightAlt;
		case Input_Key_Super:    return key == Input_Key_LeftSuper   || key == Input_Key_RightSuper;
		default: break;
		}
	}
	return false;
}

static inline bool Input_IsDown(const Input_Frame* input, Input_Key key) {
	if (key >= Input_Key_Shift && key <= Input_Key_Super) {
		switch (key) {
		case Input_Key_Shift:    return input->key_is_down[Input_Key_LeftShift]   || input->key_is_down[Input_Key_RightShift];
		case Input_Key_Control:  return input->key_is_down[Input_Key_LeftControl] || input->key_is_down[Input_Key_RightControl];
		case Input_Key_Alt:      return input->key_is_down[Input_Key_LeftAlt]     || input->key_is_down[Input_Key_RightAlt];
		case Input_Key_Super:    return input->key_is_down[Input_Key_LeftSuper]   || input->key_is_down[Input_Key_RightSuper];
		default: break;
		}
	}
	return input->key_is_down[key];
};

static inline bool Input_WentDown(const Input_Frame* input, Input_Key key) {
	if (Input_IsDown(input, key)) {
		for (int i = 0; i < input->events_count; i++) {
			if (input->events[i].kind == Input_EventKind_Press && Input_KeyIsA(input->events[i].key, key)) {
				return true;
			}
		}
	}
	return false;
}

static inline bool Input_WentDownOrRepeat(const Input_Frame* input, Input_Key key) {
	if (Input_IsDown(input, key)) {
		for (int i = 0; i < input->events_count; i++) {
			if ((input->events[i].kind == Input_EventKind_Press || input->events[i].kind == Input_EventKind_Repeat) && Input_KeyIsA(input->events[i].key, key)) {
				return true;
			}
		}
	}
	return false;
}

static inline bool Input_WentUp(const Input_Frame* input, Input_Key key) {
	if (!Input_IsDown(input, key)) {
		for (int i = 0; i < input->events_count; i++) {
			if (input->events[i].kind == Input_EventKind_Release && Input_KeyIsA(input->events[i].key, key)) {
				return true;
			}
		}
	}
	return false;
}
