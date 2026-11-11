#define _POSIX_C_SOURCE 200809L

#include "compositor/output_layout_move.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <wayland-client.h>
#include <wayland-server-core.h>
#include <wlr/backend.h>
#include <wlr/backend/headless.h>
#include <wlr/render/allocator.h>
#include <wlr/render/pixman.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/box.h>
#include <wlr/util/log.h>

struct output_globals {
  int added;
  int removed;
  uint32_t names[8];
  int name_count;
};

static void
handle_global(void* data, struct wl_registry* registry, uint32_t name, const char* interface, uint32_t version) {
  (void)registry;
  (void)version;
  struct output_globals* globals = data;
  if (strcmp(interface, "wl_output") == 0 && globals->name_count < 8) {
    globals->names[globals->name_count++] = name;
    globals->added++;
  }
}

static void handle_global_remove(void* data, struct wl_registry* registry, uint32_t name) {
  (void)registry;
  struct output_globals* globals = data;
  for (int i = 0; i < globals->name_count; ++i) {
    if (globals->names[i] == name) {
      globals->removed++;
    }
  }
}

static const struct wl_registry_listener registry_listener = {
    .global = handle_global,
    .global_remove = handle_global_remove,
};

static void handle_sync_done(void* data, struct wl_callback* callback, uint32_t serial) {
  (void)serial;
  *(bool*)data = true;
  wl_callback_destroy(callback);
}

static const struct wl_callback_listener sync_listener = {.done = handle_sync_done};

static void sync_client(struct wl_display* server, struct wl_display* client) {
  bool done = false;
  struct wl_callback* callback = wl_display_sync(client);
  wl_callback_add_listener(callback, &sync_listener, &done);
  while (!done) {
    wl_display_flush(client);
    wl_event_loop_dispatch(wl_display_get_event_loop(server), 0);
    wl_display_flush_clients(server);
    wl_display_dispatch(client);
  }
}

static bool expect_int(const char* name, int actual, int expected) {
  if (actual == expected) {
    return true;
  }
  fprintf(stderr, "%s: expected %d, got %d\n", name, expected, actual);
  return false;
}

int main(void) {
  wlr_log_init(WLR_ERROR, NULL);

  struct wl_display* server = wl_display_create();
  struct wlr_backend* backend = wlr_headless_backend_create(wl_display_get_event_loop(server));
  struct wlr_renderer* renderer = wlr_pixman_renderer_create();
  struct wlr_allocator* allocator =
      backend != NULL && renderer != NULL ? wlr_allocator_autocreate(backend, renderer) : NULL;
  if (allocator == NULL || !wlr_backend_start(backend)) {
    fprintf(stderr, "failed to start the headless backend\n");
    return EXIT_FAILURE;
  }
  struct wlr_output* output = wlr_headless_add_output(backend, 1280, 720);
  if (output == NULL || !wlr_output_init_render(output, allocator, renderer)) {
    fprintf(stderr, "failed to create a headless output\n");
    return EXIT_FAILURE;
  }

  struct wlr_output_layout* layout = wlr_output_layout_create(server);
  struct wlr_scene* scene = wlr_scene_create();
  struct wlr_scene_output_layout* scene_layout = wlr_scene_attach_output_layout(scene, layout);
  struct wlr_scene_output* scene_output = wlr_scene_output_create(scene, output);
  struct wlr_output_layout_output* layout_output = wlr_output_layout_add(layout, output, 0, 0);
  wlr_scene_output_layout_add_output(scene_layout, layout_output, scene_output);

  int fds[2];
  if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, fds) != 0) {
    perror("socketpair");
    return EXIT_FAILURE;
  }
  wl_client_create(server, fds[0]);
  struct wl_display* client = wl_display_connect_to_fd(fds[1]);
  struct output_globals globals = {0};
  struct wl_registry* registry = wl_display_get_registry(client);
  wl_registry_add_listener(registry, &registry_listener, &globals);
  sync_client(server, client);

  const bool moved = output_layout_move(layout, scene_layout, output, scene_output, &layout_output, 1280, 0);
  sync_client(server, client);

  struct wlr_box box;
  wlr_output_layout_get_box(layout, output, &box);

  bool passed = true;
  passed = expect_int("output move succeeds", moved, true) && passed;
  passed = expect_int("moving an output announces no second wl_output global", globals.added, 1) && passed;
  passed = expect_int("moving an output keeps its wl_output global", globals.removed, 0) && passed;
  passed = expect_int("moving an output places it at the new x", box.x, 1280) && passed;
  passed = expect_int("moving an output moves its scene output", scene_output->x, 1280) && passed;

  wl_display_disconnect(client);
  wl_display_destroy_clients(server);
  wlr_scene_output_destroy(scene_output);
  wlr_output_layout_destroy(layout);
  wlr_backend_destroy(backend);
  wlr_allocator_destroy(allocator);
  wlr_renderer_destroy(renderer);
  wl_display_destroy(server);
  return passed ? EXIT_SUCCESS : EXIT_FAILURE;
}
