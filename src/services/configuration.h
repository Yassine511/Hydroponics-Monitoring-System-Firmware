// Configuration: validated commands, versioned config, sole NVS writer.
// Work package WP8 (Stage 5). Architecture §15.
#pragma once
#include <stdbool.h>

bool configurationInit(void);  // load the last valid committed configuration
