#define _POSIX_C_SOURCE 200809L

#include "compositor/output_render.h"

#include <stdio.h>
#include <stdlib.h>
#include <wayland-server-core.h>
#include <wlr/backend.h>
#include <wlr/backend/headless.h>
#include <wlr/interfaces/wlr_output.h>
#include <wlr/render/allocator.h>
#include <wlr/render/pixman.h>
#include <wlr/types/wlr_output.h>
#include <wlr/util/log.h>

static bool expect(const char* name, bool condition) {
  if (!condition) {
    fprintf(stderr, "%s\n", name);
  }
  return condition;
}

static bool render_and_commit(struct wlr_output* output) {
  struct wlr_output_state state;
  wlr_output_state_init(&state);
  wlr_output_state_set_enabled(&state, true);
  const bool rendered = output_render_black(output, &state);
  const bool committed = rendered && wlr_output_commit_state(output, &state);
  wlr_output_state_finish(&state);
  return committed;
}

static bool commit_fixed_output(struct wlr_output* output, const struct wlr_output_state* state) {
  (void)output;
  (void)state;
  return true;
}

static const struct wlr_output_impl fixed_output_impl = {.commit = commit_fixed_output};

int main(void) {
  wlr_log_init(WLR_ERROR, NULL);
  struct wl_display* display = wl_display_create();
  struct wlr_backend* backend = wlr_headless_backend_create(wl_display_get_event_loop(display));
  struct wlr_renderer* renderer = wlr_pixman_renderer_create();
  struct wlr_allocator* allocator =
      backend != NULL && renderer != NULL ? wlr_allocator_autocreate(backend, renderer) : NULL;
  if (allocator == NULL || !wlr_backend_start(backend)) {
    fprintf(stderr, "failed to start headless output renderer\n");
    return EXIT_FAILURE;
  }

  struct wlr_output* unavailable = wlr_headless_add_output(backend, 0, 0);
  struct wlr_output* usable = wlr_headless_add_output(backend, 1280, 720);
  if (unavailable == NULL
      || usable == NULL
      || !wlr_output_init_render(unavailable, allocator, renderer)
      || !wlr_output_init_render(usable, allocator, renderer)) {
    fprintf(stderr, "failed to initialize headless outputs\n");
    return EXIT_FAILURE;
  }

  bool passed = true;
  passed = expect("zero-sized output must be skipped without aborting", !render_and_commit(unavailable)) && passed;
  passed = expect("skipped output remains disabled", !unavailable->enabled) && passed;
  passed = expect("other output without advertised modes remains usable", render_and_commit(usable)) && passed;

  // A backend may announce its fixed mode before the output has a current size.
  // Headless outputs only support custom modes, so use a virtual fixed-mode
  // output with the same real pixman renderer and allocator for this case.
  struct wlr_output fixed;
  struct wlr_output_state state;
  wlr_output_state_init(&state);
  wlr_output_init(&fixed, backend, &fixed_output_impl, wl_display_get_event_loop(display), &state);
  wlr_output_state_finish(&state);
  wlr_output_set_name(&fixed, "FIXED-1");
  if (!wlr_output_init_render(&fixed, allocator, renderer)) {
    fprintf(stderr, "failed to initialize fixed-mode output\n");
    return EXIT_FAILURE;
  }
  struct wlr_output_mode mode = {.width = 640, .height = 480, .refresh = 60000};
  wlr_output_state_init(&state);
  wlr_output_state_set_enabled(&state, true);
  wlr_output_state_set_mode(&state, &mode);
  passed =
      expect("pending fixed mode supplies the first frame's dimensions", output_render_black(&fixed, &state)) && passed;
  passed = expect(
               "fixed-mode buffer has the pending size",
               state.buffer != NULL && state.buffer->width == 640 && state.buffer->height == 480
           )
      && passed;
  passed = expect("pending fixed mode can be enabled", wlr_output_commit_state(&fixed, &state)) && passed;
  wlr_output_state_finish(&state);

  // Custom-mode backends can also recover after first announcing an empty output.
  wlr_output_state_init(&state);
  wlr_output_state_set_enabled(&state, true);
  wlr_output_state_set_custom_mode(&state, 800, 600, 60000);
  const bool recovered = output_render_black(unavailable, &state) && wlr_output_commit_state(unavailable, &state);
  passed = expect("output recovers when a usable custom mode arrives", recovered) && passed;
  wlr_output_state_finish(&state);
  passed = expect("recovered output can render using its current dimensions", render_and_commit(unavailable)) && passed;

  // Invalid pending dimensions must not fall back to the valid current mode.
  wlr_output_state_init(&state);
  wlr_output_state_set_custom_mode(&state, 0, 600, 60000);
  passed = expect("pending zero width must be rejected", !output_render_black(usable, &state)) && passed;
  passed = expect("rejected state has no rendered buffer", state.buffer == NULL) && passed;
  wlr_output_state_finish(&state);

  wlr_output_state_init(&state);
  wlr_output_state_set_custom_mode(&state, 800, 0, 60000);
  passed = expect("pending zero height must be rejected", !output_render_black(usable, &state)) && passed;
  wlr_output_state_finish(&state);

  struct wlr_output_mode invalid_mode = {.width = 0, .height = 480, .refresh = 60000};
  wlr_output_state_init(&state);
  wlr_output_state_set_mode(&state, &invalid_mode);
  passed = expect("pending zero-sized fixed mode must be rejected", !output_render_black(usable, &state)) && passed;
  wlr_output_state_finish(&state);

  wlr_output_finish(&fixed);
  wlr_backend_destroy(backend);
  wlr_allocator_destroy(allocator);
  wlr_renderer_destroy(renderer);
  wl_display_destroy(display);
  return passed ? EXIT_SUCCESS : EXIT_FAILURE;
}
