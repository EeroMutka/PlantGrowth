#include <stdlib.h> // for malloc

typedef struct {
	OS_String file;
	OS_String function;
	uint32_t line;
} AllocStackEntry;

typedef struct {
	AllocStackEntry* entries;
	int entries_count;
} AllocInfo;

static DS_Arena g_allocation_metadata_arena;
static OS_Arena g_allocation_temp_arena;
static bool g_inside_allocation_validation = false;
static DS_Map(uint64_t, OS_String) g_allocation_string_table = {&g_allocation_metadata_arena};
static DS_Map(void*, AllocInfo) g_alive_allocations = {&g_allocation_metadata_arena};

static void LeakDetectorInit() {
	g_inside_allocation_validation = true;
	DS_ArenaInit(&g_allocation_metadata_arena, DS_KIB(64));
	OS_ArenaInit(&g_allocation_temp_arena, DS_KIB(1));
	g_inside_allocation_validation = false;
}

static void LeakDetectorDeinit() {
	g_inside_allocation_validation = true;
	
	DS_ForMapEach(void*, AllocInfo, &g_alive_allocations, it) {
		printf("Unfreed allocation detected at address %p. Callstack:\n", *it.key);
		
		AllocInfo info = *it.value;
		for (int i = 0; i < info.entries_count; i++) {
			AllocStackEntry entry = info.entries[i];
			
			//const char* file = StrToCStr(&g_allocation_temp_arena, entry.file);
			// const char* function = StrToCStr(&g_allocation_temp_arena, entry.function);
			printf("    %.*s:%d\n", entry.function.size, entry.function.data, entry.line);
		}

		//__debugbreak();
	}
	
	g_inside_allocation_validation = false;
}

char* DS_Realloc_Impl(char* old_ptr, int new_size, int new_alignment) {
	if (old_ptr != NULL && !g_inside_allocation_validation) {
		g_inside_allocation_validation = true;
		
		bool was_alive = DS_MapRemove(&g_alive_allocations, (void*)old_ptr);
		// TODO: call free() on the entries array. If we wanted to be fancy, we could embed this array to the end of the user allocation.
		DS_CHECK(was_alive);
		
		g_inside_allocation_validation = false;
	}

#ifdef LEAK_DETECTOR_DETERMINISTIC_ARENA
	void* result = NULL;
	if (new_size > 0) {
		static uint64_t buffer[DS_MIB(1)]; static size_t buffer_offset = 0;
		if (buffer_offset > sizeof(buffer)) __debugbreak();
	
		buffer_offset = DS_AlignUpPow2(buffer_offset, new_alignment);
		//buffer_offset = AlignUpPow2(buffer_offset, 16);
		result = (char*)buffer + buffer_offset;
		printf("buffer_offset %llu\n", buffer_offset);
		buffer_offset += new_size;
	}
#else
	void* result = _aligned_realloc(old_ptr, new_size, new_alignment);
#endif

	if (new_size > 0 && !g_inside_allocation_validation) {
		DS_CHECK(result != NULL);
		g_inside_allocation_validation = true;
	
		AllocInfo* alloc_info = NULL;
		bool newly_added = DS_MapGetOrAddPtr(&g_alive_allocations, result, &alloc_info);
		DS_CHECK(newly_added);
		
		if (newly_added) {
			// add to the allocation metadata table.

			OS_ArenaReset(&g_allocation_temp_arena);
			OS_DebugStackFrameArray stack_trace = OS_DebugGetStackTrace(&g_allocation_temp_arena);

			AllocStackEntry* entries = (AllocStackEntry*)DS_MemAlloc(stack_trace.length * sizeof(AllocStackEntry));

			for (int i = 0; i < stack_trace.length; i++) {
				OS_String file = stack_trace.data[i].file;
				OS_String function = stack_trace.data[i].function;
				uint64_t file_hash = DS_MurmurHash64A(file.data, file.size, 0);
				uint64_t function_hash = DS_MurmurHash64A(function.data, function.size, 0);
				
				OS_String* file_interned;
				if (DS_MapGetOrAddPtr(&g_allocation_string_table, file_hash, &file_interned)) {
					*file_interned = file;
					file_interned->data = (char*)DS_CloneSize(&g_allocation_metadata_arena, file_interned->data, file_interned->size);
				}
				OS_String* function_interned;
				if (DS_MapGetOrAddPtr(&g_allocation_string_table, function_hash, &function_interned)) {
					*function_interned = function;
					function_interned->data = (char*)DS_CloneSize(&g_allocation_metadata_arena, function.data, function.size);
				}
				
				entries[i].file = *file_interned;
				entries[i].function = *function_interned;
				entries[i].line = stack_trace.data[i].line;
			}
			
			alloc_info->entries = entries;
			alloc_info->entries_count = stack_trace.length;
		}
	
		g_inside_allocation_validation = false;
	}

	return (char*)result;
}
