#pragma once

#include <stdbool.h>

struct wlr_output;
struct wlr_output_layout;
struct wlr_output_layout_output;
struct wlr_scene_output;
struct wlr_scene_output_layout;

bool output_layout_move(
    struct wlr_output_layout* layout, struct wlr_scene_output_layout* scene_layout, struct wlr_output* output,
    struct wlr_scene_output* scene_output, struct wlr_output_layout_output** layout_output, int x, int y
);
