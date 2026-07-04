#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

struct greeter_compositor_config {
  char preferred_output[128];
  float manual_scale;
  char cursor_theme[128];
  int cursor_size;
  char cursor_path[512];
  char keyboard_layout[128];
  char keyboard_variant[128];
  char keyboard_options[256];
  int keyboard_numlock;
  char output_layout[2048];
};

void greeter_compositor_config_load(const char* state_dir, struct greeter_compositor_config* out);

#ifdef __cplusplus
}
#endif
