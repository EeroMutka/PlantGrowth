// depends on key_input.h, fire_ds.h and stdio.h

#include <stdio.h>

typedef struct Replay {
	bool playing;
	int frame_index;
	bool is_finished;
	FILE* file;
} Replay;

typedef struct { float x, y; } ReplayMousePos;

static void ReplayInit(Replay* replay, const char* filepath, bool record);
static void ReplayDeinit(Replay* replay);
static void ReplayProcessFrame(Replay* replay, DS_Arena* frame_arena, Input_Frame* inputs, ReplayMousePos* mouse_pos);

static void ReplayInit(Replay* replay, const char* filepath, bool play) {
	replay->file = fopen(filepath, play ? "rb" : "wb");
	if (replay->file == NULL) {
		play = false;
		replay->file = fopen(filepath, "wb"); // Begin a new recording if a recording doesn't exist
	}
	assert(replay->file);

	replay->playing = play;
	replay->is_finished = false;
}

static void ReplayDeinit(Replay* replay) {
	fclose(replay->file);
	replay->file = NULL;
}

static void ReplaySerializeData(Replay* replay, void* data, int size) {
	if (replay->playing) {
		replay->is_finished = fread(data, size, 1, replay->file) != 1;
	} else {
		fwrite(data, size, 1, replay->file);
	}
}

#define ReplaySerializeX(REPLAY, X) ReplaySerializeData(REPLAY, (X), sizeof(*(X)))

static void ReplayProcessFrame(Replay* replay, DS_Arena* frame_arena, Input_Frame* inputs, ReplayMousePos* mouse_pos) {
	replay->frame_index++; // starts at frame 1

	int32_t magic_number = 52039023;
	ReplaySerializeX(replay, &magic_number);
	assert(magic_number == 52039023);
	if (replay->is_finished) return;
	
	ReplaySerializeX(replay, &inputs->events_count);
	if (replay->playing) inputs->events = (Input_Event*)DS_ArenaPush(frame_arena, inputs->events_count * sizeof(Input_Event));
	ReplaySerializeData(replay, inputs->events, inputs->events_count * sizeof(Input_Event));
	
	if (replay->playing) {
		int32_t down_keys_count;
		ReplaySerializeX(replay, &down_keys_count);
		Input_Key* down_keys = (Input_Key*)DS_ArenaPush(frame_arena, down_keys_count * sizeof(Input_Key));
		ReplaySerializeData(replay, down_keys, down_keys_count * sizeof(Input_Key));
		
		memset(inputs->key_is_down, 0, sizeof(inputs->key_is_down));
		for (int i = 0; i < down_keys_count; i++) {
			inputs->key_is_down[down_keys[i]] = true;
		}
	} else {
		DS_DynArray(Input_Key) down_keys = {frame_arena};
		for (int i = 0; i < Input_Key_COUNT; i++) {
			if (inputs->key_is_down[i]) {
				DS_ArrPush(&down_keys, (Input_Key)i);
			}
		}
		ReplaySerializeX(replay, &down_keys.length);
		ReplaySerializeData(replay, down_keys.data, down_keys.length * sizeof(Input_Key));
	}
	
	ReplaySerializeX(replay, &inputs->mouse_wheel_input);
	ReplaySerializeX(replay, &inputs->raw_mouse_input);
	ReplaySerializeX(replay, mouse_pos);

	fflush(replay->file); // ...in case the program crashes and fclose is never called
}
