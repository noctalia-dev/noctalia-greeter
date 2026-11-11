#include "compositor/output_render.h"

#include <stddef.h>
#include <wlr/render/pass.h>
#include <wlr/types/wlr_output.h>
#include <wlr/util/log.h>

bool output_render_black(struct wlr_output* output, struct wlr_output_state* state) {
  int width = output->width;
  int height = output->height;
  if (state->committed & WLR_OUTPUT_STATE_MODE) {
    if (state->mode_type == WLR_OUTPUT_STATE_MODE_FIXED) {
      width = state->mode->width;
      height = state->mode->height;
    } else {
      width = state->custom_mode.width;
      height = state->custom_mode.height;
    }
  }
  // Connected DRM outputs can arrive before their modes. wlroots asserts if
  // asked to allocate a swapchain at zero size; retry when the backend has a mode.
  if (width <= 0 || height <= 0) {
    wlr_log(WLR_ERROR, "skipping output %s: no usable mode (%dx%d)", output->name, width, height);
    return false;
  }

  struct wlr_render_pass* pass = wlr_output_begin_render_pass(output, state, NULL);
  if (pass == NULL) {
    wlr_log(WLR_ERROR, "failed to begin black frame for output %s", output->name);
    return false;
  }

  wlr_render_pass_add_rect(
      pass,
      &(struct wlr_render_rect_options){
          .box = {.width = state->buffer->width, .height = state->buffer->height},
          .color = {0.0f, 0.0f, 0.0f, 1.0f},
          .blend_mode = WLR_RENDER_BLEND_MODE_NONE,
      }
  );
  if (!wlr_render_pass_submit(pass)) {
    wlr_log(WLR_ERROR, "failed to render black frame for output %s", output->name);
    return false;
  }
  return true;
}
