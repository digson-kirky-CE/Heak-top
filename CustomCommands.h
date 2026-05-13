#ifndef CUSTOMCOMMANDS_H
#define CUSTOMCOMMANDS_H

#include <stdbool.h>
#include <stddef.h>

typedef struct ScreenManager_struct ScreenManager;

bool handle_custom_command(const char* input, ScreenManager* scr);

#endif
