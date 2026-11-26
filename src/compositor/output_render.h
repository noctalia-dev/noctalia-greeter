#pragma once

#include <stdbool.h>

struct wlr_output;
struct wlr_output_state;

bool output_render_black(struct wlr_output* output, struct wlr_output_state* state);
