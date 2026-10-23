#include "compositor/output_layout_move.h"

#include <stddef.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>

bool output_layout_move(
    struct wlr_output_layout* layout, struct wlr_scene_output_layout* scene_layout, struct wlr_output* output,
    struct wlr_scene_output* scene_output, struct wlr_output_layout_output** layout_output, int x, int y
) {
  // Re-adding moves the output in place; removing it first would destroy its wl_output global, so clients tear
  // down and recreate their surfaces for that output.
  struct wlr_output_layout_output* moved = wlr_output_layout_add(layout, output, x, y);
  if (moved == NULL) {
    return false;
  }
  if (moved != *layout_output) {
    *layout_output = moved;
    if (scene_output != NULL) {
      wlr_scene_output_layout_add_output(scene_layout, moved, scene_output);
    }
  }
  return true;
}
