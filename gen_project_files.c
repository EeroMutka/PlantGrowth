#include "Fire/fire_build.h"
#define ArrCount(x) sizeof(x) / sizeof(x[0])

int main() {
	
	BUILD_ProjectOptions opts = {
		.debug_info = true,
		.c_runtime_library_dll = true, // glslang.lib uses /MD
		.disable_aslr = true,
	};

	BUILD_Project plant_growth;
	BUILD_InitProject(&plant_growth, "plant_growth", &opts);
	BUILD_AddIncludeDir(&plant_growth, ".."); // Repository root folder
	BUILD_AddSourceFile(&plant_growth, "../src/main.cpp");
	
	BUILD_Project* projects[] = {&plant_growth};
	BUILD_CreateDirectory("build");
	
	if (!BUILD_CreateVisualStudioSolution("build", ".", "plant_growth.sln", projects, ArrCount(projects), BUILD_GetConsole())) {
		return 1;
	}
	
	return 0;
}
