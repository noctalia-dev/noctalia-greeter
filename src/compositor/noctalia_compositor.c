#define _POSIX_C_SOURCE 200809L

#include "greeter/greeter_config_io.h"

#include <ctype.h>
#include <libinput.h>
#include <limits.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include <wayland-server-core.h>
#include <wayland-server-protocol.h>
#include <wlr/backend.h>
#include <wlr/backend/libinput.h>
#include <wlr/backend/session.h>
#include <wlr/interfaces/wlr_keyboard.h>
#include <wlr/render/allocator.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_fractional_scale_v1.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_viewporter.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/log.h>
#include <xkbcommon/xkbcommon-keysyms.h>
#include <xkbcommon/xkbcommon-names.h>
#include <xkbcommon/xkbcommon.h>

#ifndef NOCTALIA_GREETER_INSTALLED_BINDIR
#define NOCTALIA_GREETER_INSTALLED_BINDIR "/usr/local/bin"
#endif

struct greeter_server;

struct greeter_output {
  struct wl_list link;
  struct greeter_server* server;
  struct wlr_output* wlr_output;
  struct wlr_scene_output* scene_output;
  struct wlr_output_layout_output* layout_output;
  struct wl_listener frame;
  struct wl_listener request_state;
  struct wl_listener destroy;
  struct greeter_view* view;
  bool active;
  bool render_initialized;
};

struct greeter_keyboard {
  struct wl_list link;
  struct greeter_server* server;
  struct wlr_keyboard* wlr_keyboard;
  struct wl_listener modifiers;
  struct wl_listener key;
  struct wl_listener destroy;
};

struct greeter_view {
  struct greeter_server* server;
  struct greeter_output* output;
  struct wlr_xdg_toplevel* toplevel;
  struct wlr_scene_tree* scene_tree;
  struct wl_listener map;
  struct wl_listener unmap;
  struct wl_listener destroy;
  struct wl_listener commit;
  bool mapped;
};

struct greeter_output_placement {
  char name[128];
  int x;
  int y;
};

struct greeter_server {
  struct wl_display* display;
  struct wlr_backend* backend;
  struct wlr_session* session;
  struct wlr_renderer* renderer;
  struct wlr_allocator* allocator;
  struct wlr_scene* scene;
  struct wlr_output_layout* output_layout;
  struct wlr_scene_output_layout* scene_output_layout;
  struct wlr_xdg_shell* xdg_shell;
  struct wlr_cursor* cursor;
  struct wlr_xcursor_manager* cursor_mgr;
  struct wlr_seat* seat;
  struct wl_list outputs;
  struct wl_list keyboards;
  struct wl_listener new_output;
  struct wl_listener new_input;
  struct wl_listener new_toplevel;
  struct wl_listener cursor_motion;
  struct wl_listener cursor_motion_absolute;
  struct wl_listener cursor_button;
  struct wl_listener cursor_axis;
  struct wl_listener cursor_frame;
  struct wl_listener request_set_cursor;
  struct wl_listener request_set_selection;
  struct wl_event_source* sigchld_source;
  struct wl_event_source* launch_timer;
  pid_t child_pid;
  bool child_launched;
  int child_argc;
  char** child_argv_ptr;
  char preferred_output[128];
  float manual_scale;
  char cursor_theme[128];
  int cursor_size;
  char cursor_path[512];
  char keyboard_layout[128];
  char keyboard_variant[128];
  char keyboard_options[256];
  int keyboard_numlock;
  struct greeter_output_placement output_placements[16];
  size_t output_placement_count;
  struct wlr_fractional_scale_manager_v1* fractional_scale;
  bool shutting_down;
};

static char* trim(char* value) {
  while (isspace((unsigned char)*value)) {
    value++;
  }
  char* end = value + strlen(value);
  while (end > value && isspace((unsigned char)end[-1])) {
    *--end = '\0';
  }
  if ((*value == '"' || *value == '\'') && end > value + 1 && end[-1] == *value) {
    value++;
    *--end = '\0';
  }
  return value;
}

static float clamp_scale(float value) {
  if (value < 1.0f) {
    return 1.0f;
  }
  if (value > 2.0f) {
    return 2.0f;
  }
  return value;
}

static float dpi_from_axis(int pixels, int phys_mm) {
  if (pixels <= 0 || phys_mm <= 0) {
    return 0.0f;
  }
  return (float)pixels / ((float)phys_mm / 25.4f);
}

static float effective_dpi(const struct wlr_output* output) {
  const float dpi_x = dpi_from_axis(output->width, output->phys_width);
  const float dpi_y = dpi_from_axis(output->height, output->phys_height);
  if (dpi_x > 0.0f && dpi_y > 0.0f) {
    return (dpi_x + dpi_y) * 0.5f;
  }
  if (dpi_x > 0.0f) {
    return dpi_x;
  }
  if (dpi_y > 0.0f) {
    return dpi_y;
  }
  return 0.0f;
}

static float fallback_scale_for_resolution(const struct wlr_output* output) {
  if (output->width >= 3840 || output->height >= 2160) {
    return 1.5f;
  }
  if (output->width >= 2560) {
    return 1.25f;
  }
  return 1.0f;
}

static float output_ui_scale(const struct wlr_output* output, float manual) {
  if (manual >= 1.0f) {
    return clamp_scale(manual);
  }

  const float dpi = effective_dpi(output);
  if (dpi > 96.0f) {
    return clamp_scale(dpi / 96.0f);
  }
  return clamp_scale(fallback_scale_for_resolution(output));
}

static bool parse_output_layout_entry(const char* token, struct greeter_output_placement* out) {
  char buf[256];
  snprintf(buf, sizeof(buf), "%s", token);
  char* colon = strrchr(buf, ':');
  if (colon == NULL || colon == buf) {
    return false;
  }
  *colon = '\0';
  char* name = trim(buf);
  char* coords = trim(colon + 1);
  char* comma = strchr(coords, ',');
  if (comma == NULL || name[0] == '\0' || coords[0] == '\0') {
    return false;
  }
  *comma = '\0';
  char* end = NULL;
  const long x = strtol(coords, &end, 10);
  if (end == coords) {
    return false;
  }
  const long y = strtol(comma + 1, &end, 10);
  if (end == comma + 1) {
    return false;
  }
  snprintf(out->name, sizeof(out->name), "%s", name);
  out->x = (int)x;
  out->y = (int)y;
  return true;
}

static void parse_output_layout_value(struct greeter_server* server, char* value) {
  server->output_placement_count = 0;
  for (char* p = value; *p != '\0'; ++p) {
    if (*p == ';') {
      *p = ' ';
    }
  }

  char* saveptr = NULL;
  for (char* token = strtok_r(value, " \t", &saveptr); token != NULL; token = strtok_r(NULL, " \t", &saveptr)) {
    if (server->output_placement_count >= sizeof(server->output_placements) / sizeof(server->output_placements[0])) {
      wlr_log(
          WLR_ERROR, "output_layout: too many entries (max %zu)",
          sizeof(server->output_placements) / sizeof(server->output_placements[0])
      );
      break;
    }
    struct greeter_output_placement entry;
    if (!parse_output_layout_entry(token, &entry)) {
      wlr_log(WLR_ERROR, "output_layout: invalid entry '%s'", token);
      continue;
    }
    server->output_placements[server->output_placement_count++] = entry;
  }
}

static void read_greeter_config(struct greeter_server* server) {
  server->preferred_output[0] = '\0';
  server->manual_scale = 0.0f;
  server->cursor_theme[0] = '\0';
  server->cursor_size = 0;
  server->cursor_path[0] = '\0';
  server->keyboard_layout[0] = '\0';
  server->keyboard_variant[0] = '\0';
  server->keyboard_options[0] = '\0';
  server->output_placement_count = 0;

  const char* state_dir = getenv("NOCTALIA_GREETER_STATE_DIR");
  struct greeter_compositor_config config;
  greeter_compositor_config_load(state_dir, &config);

  if (config.preferred_output[0] != '\0') {
    snprintf(server->preferred_output, sizeof(server->preferred_output), "%s", config.preferred_output);
  }
  if (config.manual_scale >= 1.0f) {
    server->manual_scale = config.manual_scale;
  }
  if (config.cursor_theme[0] != '\0') {
    snprintf(server->cursor_theme, sizeof(server->cursor_theme), "%s", config.cursor_theme);
  }
  if (config.cursor_size > 0) {
    server->cursor_size = config.cursor_size;
  }
  if (config.cursor_path[0] != '\0') {
    snprintf(server->cursor_path, sizeof(server->cursor_path), "%s", config.cursor_path);
  }
  if (config.keyboard_layout[0] != '\0') {
    snprintf(server->keyboard_layout, sizeof(server->keyboard_layout), "%s", config.keyboard_layout);
  }
  if (config.keyboard_variant[0] != '\0') {
    snprintf(server->keyboard_variant, sizeof(server->keyboard_variant), "%s", config.keyboard_variant);
  }
  if (config.keyboard_options[0] != '\0') {
    snprintf(server->keyboard_options, sizeof(server->keyboard_options), "%s", config.keyboard_options);
  }
  server->keyboard_numlock = config.keyboard_numlock;
  if (config.output_layout[0] != '\0' && server->output_placement_count == 0) {
    char layout[2048];
    snprintf(layout, sizeof(layout), "%s", config.output_layout);
    parse_output_layout_value(server, layout);
  }
}

static struct xkb_keymap* compose_keyboard_keymap(struct xkb_context* context, const struct greeter_server* server) {
  if (server->keyboard_layout[0] == '\0') {
    return xkb_keymap_new_from_names(context, NULL, XKB_KEYMAP_COMPILE_NO_FLAGS);
  }

  struct xkb_rule_names names = {0};
  names.layout = server->keyboard_layout;
  if (server->keyboard_variant[0] != '\0') {
    names.variant = server->keyboard_variant;
  }
  if (server->keyboard_options[0] != '\0') {
    names.options = server->keyboard_options;
  }

  struct xkb_keymap* keymap = xkb_keymap_new_from_names(context, &names, XKB_KEYMAP_COMPILE_NO_FLAGS);
  if (keymap == NULL) {
    wlr_log(
        WLR_ERROR, "keyboard: failed to compile keymap (layout=%s variant=%s options=%s); using system default",
        names.layout, names.variant != NULL ? names.variant : "", names.options != NULL ? names.options : ""
    );
    return xkb_keymap_new_from_names(context, NULL, XKB_KEYMAP_COMPILE_NO_FLAGS);
  }

  wlr_log(
      WLR_INFO, "keyboard: layout=%s variant=%s options=%s", names.layout,
      names.variant != NULL ? names.variant : "(default)", names.options != NULL ? names.options : "(none)"
  );
  return keymap;
}

static void
notify_surface_scale(struct greeter_server* server, struct wlr_surface* surface, const struct greeter_output* output) {
  if (server->fractional_scale == NULL || surface == NULL || output == NULL || !output->active) {
    return;
  }
  wlr_fractional_scale_v1_notify_scale(surface, output->wlr_output->scale);
}

static bool use_all_outputs(const struct greeter_server* server) { return server->preferred_output[0] == '\0'; }

static struct greeter_output* output_by_name(struct greeter_server* server, const char* name) {
  struct greeter_output* output;
  wl_list_for_each(output, &server->outputs, link) {
    if (strcmp(output->wlr_output->name, name) == 0) {
      return output;
    }
  }
  return NULL;
}

static bool any_output_active(const struct greeter_server* server) {
  const struct greeter_output* output;
  wl_list_for_each(output, &server->outputs, link) {
    if (output->active) {
      return true;
    }
  }
  return false;
}

static int compare_greeter_output_ptr(const void* a, const void* b) {
  const struct greeter_output* const* oa = a;
  const struct greeter_output* const* ob = b;
  return strcmp((*oa)->wlr_output->name, (*ob)->wlr_output->name);
}

static const struct greeter_server* g_layout_sort_server;

static int configured_output_index(const struct greeter_server* server, const char* name) {
  for (size_t i = 0; i < server->output_placement_count; ++i) {
    if (strcmp(server->output_placements[i].name, name) == 0) {
      return (int)i;
    }
  }
  return -1;
}

static int compare_greeter_output_by_config(const void* a, const void* b) {
  const struct greeter_output* const* oa = a;
  const struct greeter_output* const* ob = b;
  const int ia = configured_output_index(g_layout_sort_server, (*oa)->wlr_output->name);
  const int ib = configured_output_index(g_layout_sort_server, (*ob)->wlr_output->name);
  if (ia >= 0 && ib >= 0) {
    return ia - ib;
  }
  if (ia >= 0) {
    return -1;
  }
  if (ib >= 0) {
    return 1;
  }
  return strcmp((*oa)->wlr_output->name, (*ob)->wlr_output->name);
}

static size_t collect_outputs(struct greeter_server* server, struct greeter_output** out, size_t out_cap) {
  size_t count = 0;
  struct greeter_output* output;
  wl_list_for_each(output, &server->outputs, link) {
    if (count >= out_cap) {
      break;
    }
    out[count++] = output;
  }
  if (count > 1) {
    if (server->output_placement_count > 0) {
      g_layout_sort_server = server;
      qsort(out, count, sizeof(out[0]), compare_greeter_output_by_config);
      g_layout_sort_server = NULL;
    } else {
      qsort(out, count, sizeof(out[0]), compare_greeter_output_ptr);
    }
  }
  return count;
}

static int layout_extents_max_x(struct greeter_server* server) {
  int max_x = 0;
  struct greeter_output* output;
  wl_list_for_each(output, &server->outputs, link) {
    if (!output->active) {
      continue;
    }
    struct wlr_box box;
    wlr_output_layout_get_box(server->output_layout, output->wlr_output, &box);
    if (box.width > 0) {
      const int right = box.x + box.width;
      if (right > max_x) {
        max_x = right;
      }
    }
  }
  return max_x;
}

static bool layout_output_at(struct greeter_output* output, int layout_x, int layout_y);

struct configured_layout_entry {
  struct greeter_output* output;
  struct greeter_output_placement cfg;
};

static int compare_configured_layout_entry(const void* a, const void* b) {
  const struct configured_layout_entry* ea = a;
  const struct configured_layout_entry* eb = b;
  if (ea->cfg.y != eb->cfg.y) {
    return ea->cfg.y - eb->cfg.y;
  }
  if (ea->cfg.x != eb->cfg.x) {
    return ea->cfg.x - eb->cfg.x;
  }
  return strcmp(ea->cfg.name, eb->cfg.name);
}

static void layout_outputs_from_config(struct greeter_server* server) {
  struct configured_layout_entry entries[16];
  size_t count = 0;
  for (size_t i = 0; i < server->output_placement_count; ++i) {
    const struct greeter_output_placement* cfg = &server->output_placements[i];
    struct greeter_output* output = output_by_name(server, cfg->name);
    if (output == NULL) {
      wlr_log(WLR_INFO, "output_layout: '%s' not connected", cfg->name);
      continue;
    }
    entries[count].output = output;
    entries[count].cfg = *cfg;
    ++count;
  }
  if (count == 0) {
    return;
  }

  qsort(entries, count, sizeof(entries[0]), compare_configured_layout_entry);

  int row_cfg_y = entries[0].cfg.y;
  int layout_x = 0;
  int layout_y = 0;
  int row_max_effective_height = 0;
  for (size_t i = 0; i < count; ++i) {
    if (i > 0 && entries[i].cfg.y != row_cfg_y) {
      layout_y += row_max_effective_height;
      layout_x = 0;
      row_cfg_y = entries[i].cfg.y;
      row_max_effective_height = 0;
    }

    if (!layout_output_at(entries[i].output, layout_x, layout_y)) {
      continue;
    }

    int effective_width = 0;
    int effective_height = 0;
    wlr_output_effective_resolution(entries[i].output->wlr_output, &effective_width, &effective_height);
    wlr_log(
        WLR_INFO, "greeter output: %s at (%d,%d) (configured %s:%d,%d; effective %dx%d)", entries[i].cfg.name, layout_x,
        layout_y, entries[i].cfg.name, entries[i].cfg.x, entries[i].cfg.y, effective_width, effective_height
    );

    if (effective_height > row_max_effective_height) {
      row_max_effective_height = effective_height;
    }
    if (effective_width > 0) {
      layout_x += effective_width;
    }
  }

  int fallback_x = layout_extents_max_x(server);
  struct greeter_output* outputs[16];
  const size_t output_count = collect_outputs(server, outputs, 16);
  for (size_t i = 0; i < output_count; ++i) {
    struct greeter_output* output = outputs[i];
    struct wlr_box box;
    wlr_output_layout_get_box(server->output_layout, output->wlr_output, &box);
    if (box.width > 0) {
      continue;
    }
    if (layout_output_at(output, fallback_x, 0)) {
      wlr_log(WLR_INFO, "output_layout: '%s' not listed; placed at (%d,0)", output->wlr_output->name, fallback_x);
      int width = 0;
      int height = 0;
      wlr_output_effective_resolution(output->wlr_output, &width, &height);
      if (width > 0) {
        fallback_x += width;
      }
    }
  }
}

static struct greeter_output* output_for_next_view(struct greeter_server* server) {
  struct greeter_output* outputs[16];
  const size_t count = collect_outputs(server, outputs, 16);
  for (size_t i = 0; i < count; ++i) {
    if (outputs[i]->active && outputs[i]->view == NULL) {
      return outputs[i];
    }
  }
  return NULL;
}

static void configure_view(struct greeter_view* view);

static void focus_view(struct greeter_view* view) {
  if (view == NULL || !view->mapped) {
    return;
  }

  struct greeter_server* server = view->server;
  struct wlr_keyboard* keyboard = wlr_seat_get_keyboard(server->seat);
  wlr_xdg_toplevel_set_activated(view->toplevel, true);
  if (keyboard != NULL) {
    wlr_seat_keyboard_notify_enter(
        server->seat, view->toplevel->base->surface, keyboard->keycodes, keyboard->num_keycodes, &keyboard->modifiers
    );
  }
}

static void schedule_output_frames(struct greeter_server* server) {
  struct greeter_output* output;
  wl_list_for_each(output, &server->outputs, link) {
    if (output->active) {
      wlr_output_schedule_frame(output->wlr_output);
    }
  }
}

static void handle_output_frame(struct wl_listener* listener, void* data) {
  (void)data;
  struct greeter_output* output = wl_container_of(listener, output, frame);
  struct wlr_scene_output* scene_output = output->scene_output;

  if (scene_output == NULL) {
    return;
  }
  if (!wlr_scene_output_commit(scene_output, NULL)) {
    wlr_log(WLR_ERROR, "failed to commit output %s", output->wlr_output->name);
    return;
  }

  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);
  wlr_scene_output_send_frame_done(scene_output, &now);
}

static void handle_output_request_state(struct wl_listener* listener, void* data) {
  struct greeter_output* output = wl_container_of(listener, output, request_state);
  const struct wlr_output_event_request_state* event = data;
  wlr_output_commit_state(output->wlr_output, event->state);
}

static void disable_output(struct greeter_output* output) {
  if (!output->active && !output->wlr_output->enabled) {
    return;
  }

  if (output->layout_output != NULL) {
    wlr_output_layout_remove(output->server->output_layout, output->wlr_output);
    output->layout_output = NULL;
  }
  if (output->scene_output != NULL) {
    wlr_scene_output_destroy(output->scene_output);
    output->scene_output = NULL;
  }

  struct wlr_output_state state;
  wlr_output_state_init(&state);
  wlr_output_state_set_enabled(&state, false);
  wlr_output_commit_state(output->wlr_output, &state);
  wlr_output_state_finish(&state);
  output->active = false;
  if (output->view != NULL) {
    struct greeter_view* view = output->view;
    output->view = NULL;
    view->output = NULL;
    wlr_xdg_toplevel_send_close(view->toplevel);
  }
}

static struct wlr_output_mode* select_output_mode(struct wlr_output* wlr_output) {
  struct wlr_output_mode* preferred = wlr_output_preferred_mode(wlr_output);
  if (preferred == NULL) {
    return NULL;
  }

  // EDID may list several refresh rates at the preferred resolution; pick the
  // highest among those matching the preferred mode's size.
  struct wlr_output_mode* best = preferred;
  struct wlr_output_mode* mode;
  wl_list_for_each(mode, &wlr_output->modes, link) {
    if (mode->width != preferred->width || mode->height != preferred->height) {
      continue;
    }
    if (mode->refresh > best->refresh) {
      best = mode;
    }
  }
  return best;
}

static bool commit_output_enabled(struct greeter_output* output) {
  struct greeter_server* server = output->server;
  if (!output->render_initialized) {
    if (!wlr_output_init_render(output->wlr_output, server->allocator, server->renderer)) {
      wlr_log(WLR_ERROR, "failed to initialize renderer for %s", output->wlr_output->name);
      return false;
    }
    output->render_initialized = true;
  }

  struct wlr_output_state state;
  wlr_output_state_init(&state);
  wlr_output_state_set_enabled(&state, true);
  struct wlr_output_mode* mode = select_output_mode(output->wlr_output);
  if (mode != NULL) {
    wlr_output_state_set_mode(&state, mode);
    wlr_log(WLR_INFO, "selected output mode: %dx%d @ %.3f Hz", mode->width, mode->height, mode->refresh / 1000.0);
  }
  const float scale = output_ui_scale(output->wlr_output, output->server->manual_scale);
  wlr_output_state_set_scale(&state, scale);
  bool ok = wlr_output_commit_state(output->wlr_output, &state);
  wlr_output_state_finish(&state);
  if (!ok) {
    wlr_log(WLR_ERROR, "failed to enable output %s", output->wlr_output->name);
    return false;
  }

  wlr_log(WLR_INFO, "output %s scale=%.2f", output->wlr_output->name, scale);
  return true;
}

static bool enable_layout_output(struct greeter_output* output, int layout_x, int layout_y);

static bool layout_output_at(struct greeter_output* output, int layout_x, int layout_y) {
  if (output->active) {
    struct greeter_server* server = output->server;
    if (output->layout_output != NULL) {
      wlr_output_layout_remove(server->output_layout, output->wlr_output);
      output->layout_output = NULL;
    }
    output->layout_output = wlr_output_layout_add(server->output_layout, output->wlr_output, layout_x, layout_y);
    if (output->layout_output == NULL) {
      wlr_log(WLR_ERROR, "failed to reposition output %s in layout", output->wlr_output->name);
      return false;
    }
    if (output->scene_output != NULL) {
      wlr_scene_output_layout_add_output(server->scene_output_layout, output->layout_output, output->scene_output);
    }
    if (output->view != NULL && output->view->mapped) {
      configure_view(output->view);
    }
    wlr_output_schedule_frame(output->wlr_output);
    return true;
  }

  return enable_layout_output(output, layout_x, layout_y);
}

static bool enable_layout_output(struct greeter_output* output, int layout_x, int layout_y) {
  struct greeter_server* server = output->server;
  if (!commit_output_enabled(output)) {
    return false;
  }

  output->layout_output = wlr_output_layout_add(server->output_layout, output->wlr_output, layout_x, layout_y);
  if (output->layout_output == NULL) {
    wlr_log(WLR_ERROR, "failed to add output %s to layout", output->wlr_output->name);
    return false;
  }

  output->scene_output = wlr_scene_output_create(server->scene, output->wlr_output);
  if (output->scene_output == NULL) {
    wlr_log(WLR_ERROR, "failed to create scene output for %s", output->wlr_output->name);
    return false;
  }
  wlr_scene_output_layout_add_output(server->scene_output_layout, output->layout_output, output->scene_output);

  output->active = true;
  wlr_xcursor_manager_load(server->cursor_mgr, output->wlr_output->scale);
  wlr_output_schedule_frame(output->wlr_output);
  return true;
}

static void warp_cursor_to_output_center(struct greeter_server* server, struct greeter_output* output) {
  if (output == NULL || !output->active) {
    return;
  }

  struct wlr_box box;
  wlr_output_layout_get_box(server->output_layout, output->wlr_output, &box);
  if (box.width <= 0 || box.height <= 0) {
    return;
  }

  const double x = box.x + box.width * 0.5;
  const double y = box.y + box.height * 0.5;
  wlr_cursor_warp(server->cursor, NULL, x, y);
  wlr_log(WLR_INFO, "warped cursor to center of %s (%.0f, %.0f)", output->wlr_output->name, x, y);
}

static void warp_cursor_to_initial_position(struct greeter_server* server) {
  if (!use_all_outputs(server)) {
    struct greeter_output* pinned = output_by_name(server, server->preferred_output);
    if (pinned != NULL && pinned->active) {
      warp_cursor_to_output_center(server, pinned);
      return;
    }
  }

  struct greeter_output* outputs[16];
  const size_t count = collect_outputs(server, outputs, 16);
  for (size_t i = 0; i < count; ++i) {
    if (outputs[i]->active) {
      warp_cursor_to_output_center(server, outputs[i]);
      return;
    }
  }
}

static void configure_view(struct greeter_view* view) {
  if (view == NULL || !view->mapped || view->output == NULL || !view->output->active) {
    return;
  }

  struct greeter_server* server = view->server;
  struct greeter_output* output = view->output;
  struct wlr_box layout_box;
  wlr_output_layout_get_box(server->output_layout, output->wlr_output, &layout_box);
  wlr_scene_node_set_position(&view->scene_tree->node, layout_box.x, layout_box.y);

  int width = 0;
  int height = 0;
  wlr_output_effective_resolution(output->wlr_output, &width, &height);
  if (width > 0 && height > 0) {
    wlr_xdg_toplevel_set_size(view->toplevel, width, height);
  }
  notify_surface_scale(server, view->toplevel->base->surface, output);
  focus_view(view);
  schedule_output_frames(server);
}

static void try_launch_greeter(void* data);
static bool start_child(struct greeter_server* server, char** argv, bool free_argv);

static bool all_outputs_active(struct greeter_server* server) {
  if (wl_list_empty(&server->outputs)) {
    return false;
  }

  struct greeter_output* output;
  wl_list_for_each(output, &server->outputs, link) {
    if (!output->active) {
      return false;
    }
  }
  return true;
}

static int launch_timer_fired(void* data) {
  struct greeter_server* server = data;
  server->launch_timer = NULL;
  try_launch_greeter(server);
  return 0;
}

static void schedule_launch(struct greeter_server* server) {
  if (server->child_launched) {
    return;
  }

  if (use_all_outputs(server)) {
    if (!all_outputs_active(server) && !any_output_active(server)) {
      return;
    }
  } else if (!any_output_active(server)) {
    return;
  }

  struct wl_event_loop* loop = wl_display_get_event_loop(server->display);
  if (server->launch_timer != NULL) {
    wl_event_source_timer_update(server->launch_timer, 100);
    return;
  }

  server->launch_timer = wl_event_loop_add_timer(loop, launch_timer_fired, server);
  if (server->launch_timer != NULL) {
    wl_event_source_timer_update(server->launch_timer, 100);
  }
}

static void choose_outputs(struct greeter_server* server) {
  bool use_all = use_all_outputs(server);
  struct greeter_output* pinned = NULL;
  if (!use_all) {
    pinned = output_by_name(server, server->preferred_output);
    if (pinned == NULL) {
      wlr_log(WLR_INFO, "output '%s' not connected; using all outputs", server->preferred_output);
      use_all = true;
    }
  }

  struct greeter_output* output;
  wl_list_for_each(output, &server->outputs, link) {
    const bool want = use_all || pinned == NULL || output == pinned;
    if (!want && output->active) {
      disable_output(output);
    }
  }

  if (use_all) {
    if (server->output_placement_count > 0) {
      layout_outputs_from_config(server);
    } else {
      struct greeter_output* outputs[16];
      const size_t count = collect_outputs(server, outputs, 16);
      int layout_x = 0;
      for (size_t i = 0; i < count; ++i) {
        if (!layout_output_at(outputs[i], layout_x, 0)) {
          continue;
        }
        wlr_log(WLR_INFO, "greeter output: %s at (%d,0)", outputs[i]->wlr_output->name, layout_x);
        int width = 0;
        int height = 0;
        wlr_output_effective_resolution(outputs[i]->wlr_output, &width, &height);
        if (width > 0) {
          layout_x += width;
        }
      }
    }
  } else if (pinned != NULL && !pinned->active && enable_layout_output(pinned, 0, 0)) {
    wlr_log(WLR_INFO, "greeter pinned output: %s", pinned->wlr_output->name);
  }

  wl_list_for_each(output, &server->outputs, link) {
    if (output->view != NULL && output->view->mapped) {
      configure_view(output->view);
    }
  }

  if (any_output_active(server)) {
    warp_cursor_to_initial_position(server);
  }
  schedule_launch(server);
  schedule_output_frames(server);
}

static void handle_output_destroy(struct wl_listener* listener, void* data) {
  (void)data;
  struct greeter_output* output = wl_container_of(listener, output, destroy);
  struct greeter_server* server = output->server;

  wl_list_remove(&output->frame.link);
  wl_list_remove(&output->request_state.link);
  wl_list_remove(&output->destroy.link);
  wl_list_remove(&output->link);
  free(output);
  if (!server->shutting_down) {
    choose_outputs(server);
  }
}

static void handle_new_output(struct wl_listener* listener, void* data) {
  struct greeter_server* server = wl_container_of(listener, server, new_output);
  struct wlr_output* wlr_output = data;

  struct greeter_output* output = calloc(1, sizeof(*output));
  if (output == NULL) {
    return;
  }
  output->server = server;
  output->wlr_output = wlr_output;

  output->frame.notify = handle_output_frame;
  wl_signal_add(&wlr_output->events.frame, &output->frame);
  output->request_state.notify = handle_output_request_state;
  wl_signal_add(&wlr_output->events.request_state, &output->request_state);
  output->destroy.notify = handle_output_destroy;
  wl_signal_add(&wlr_output->events.destroy, &output->destroy);
  wl_list_insert(&server->outputs, &output->link);

  choose_outputs(server);
}

static void pointer_focus(struct greeter_server* server, double lx, double ly) {
  double sx = lx;
  double sy = ly;
  struct wlr_scene_node* node = wlr_scene_node_at(&server->scene->tree.node, lx, ly, &sx, &sy);
  struct wlr_surface* surface = NULL;
  if (node != NULL && node->type == WLR_SCENE_NODE_BUFFER) {
    struct wlr_scene_buffer* buffer = wlr_scene_buffer_from_node(node);
    struct wlr_scene_surface* scene_surface = wlr_scene_surface_try_from_buffer(buffer);
    if (scene_surface != NULL) {
      surface = scene_surface->surface;
    }
  }
  struct wlr_surface* prev = server->seat->pointer_state.focused_surface;
  if (surface != NULL) {
    if (surface != prev) {
      wlr_seat_pointer_notify_enter(server->seat, surface, sx, sy);
    } else {
      wlr_seat_pointer_notify_motion(server->seat, 0, sx, sy);
    }
  } else if (prev != NULL) {
    wlr_seat_pointer_notify_clear_focus(server->seat);
  }
}

static void handle_cursor_motion(struct wl_listener* listener, void* data) {
  struct greeter_server* server = wl_container_of(listener, server, cursor_motion);
  struct wlr_pointer_motion_event* event = data;
  wlr_cursor_move(server->cursor, &event->pointer->base, event->delta_x, event->delta_y);
  pointer_focus(server, server->cursor->x, server->cursor->y);
}

static void handle_cursor_motion_absolute(struct wl_listener* listener, void* data) {
  struct greeter_server* server = wl_container_of(listener, server, cursor_motion_absolute);
  struct wlr_pointer_motion_absolute_event* event = data;
  wlr_cursor_warp_absolute(server->cursor, &event->pointer->base, event->x, event->y);
  pointer_focus(server, server->cursor->x, server->cursor->y);
}

static void handle_cursor_button(struct wl_listener* listener, void* data) {
  struct greeter_server* server = wl_container_of(listener, server, cursor_button);
  struct wlr_pointer_button_event* event = data;
  pointer_focus(server, server->cursor->x, server->cursor->y);
  wlr_seat_pointer_notify_button(server->seat, event->time_msec, event->button, event->state);
}

static void handle_cursor_axis(struct wl_listener* listener, void* data) {
  struct greeter_server* server = wl_container_of(listener, server, cursor_axis);
  struct wlr_pointer_axis_event* event = data;
  wlr_seat_pointer_notify_axis(
      server->seat, event->time_msec, event->orientation, event->delta, event->delta_discrete, event->source,
      event->relative_direction
  );
}

static void handle_cursor_frame(struct wl_listener* listener, void* data) {
  (void)data;
  struct greeter_server* server = wl_container_of(listener, server, cursor_frame);
  wlr_seat_pointer_notify_frame(server->seat);
}

static void handle_request_set_cursor(struct wl_listener* listener, void* data) {
  struct greeter_server* server = wl_container_of(listener, server, request_set_cursor);
  struct wlr_seat_pointer_request_set_cursor_event* event = data;
  struct wlr_seat_client* focused = server->seat->pointer_state.focused_client;
  if (focused == event->seat_client) {
    wlr_cursor_set_surface(server->cursor, event->surface, event->hotspot_x, event->hotspot_y);
  }
}

static void handle_keyboard_modifiers(struct wl_listener* listener, void* data) {
  (void)data;
  struct greeter_keyboard* keyboard = wl_container_of(listener, keyboard, modifiers);
  wlr_seat_keyboard_notify_modifiers(keyboard->server->seat, &keyboard->wlr_keyboard->modifiers);
}

static void handle_keyboard_key(struct wl_listener* listener, void* data) {
  struct greeter_keyboard* keyboard = wl_container_of(listener, keyboard, key);
  struct greeter_server* server = keyboard->server;
  struct wlr_keyboard_key_event* event = data;

  if (event->state == WL_KEYBOARD_KEY_STATE_PRESSED && server->session != NULL) {
    const xkb_keysym_t* syms;
    int nsyms = xkb_state_key_get_syms(keyboard->wlr_keyboard->xkb_state, event->keycode + 8, &syms);
    for (int i = 0; i < nsyms; i++) {
      xkb_keysym_t sym = syms[i];
      if (sym >= (xkb_keysym_t)XKB_KEY_XF86Switch_VT_1 && sym <= (xkb_keysym_t)XKB_KEY_XF86Switch_VT_12) {
        unsigned vt = (unsigned)(sym - (xkb_keysym_t)XKB_KEY_XF86Switch_VT_1 + 1);
        wlr_session_change_vt(server->session, vt);
        return;
      }
    }
  }

  wlr_seat_keyboard_notify_key(server->seat, event->time_msec, event->keycode, event->state);
}

static void handle_keyboard_destroy(struct wl_listener* listener, void* data) {
  (void)data;
  struct greeter_keyboard* keyboard = wl_container_of(listener, keyboard, destroy);
  wl_list_remove(&keyboard->modifiers.link);
  wl_list_remove(&keyboard->key.link);
  wl_list_remove(&keyboard->destroy.link);
  wl_list_remove(&keyboard->link);
  free(keyboard);
}

static void configure_libinput_touchpad(struct wlr_input_device* device) {
  if (!wlr_input_device_is_libinput(device)) {
    return;
  }

  struct libinput_device* libinput_dev = wlr_libinput_get_device_handle(device);
  if (libinput_dev == NULL) {
    return;
  }
  if (libinput_device_config_tap_get_finger_count(libinput_dev) == 0) {
    return;
  }

  if (libinput_device_config_tap_set_enabled(libinput_dev, LIBINPUT_CONFIG_TAP_ENABLED) != 0) {
    wlr_log(WLR_INFO, "touchpad: failed to enable tap-to-click for %s", device->name);
    return;
  }

  libinput_device_config_tap_set_button_map(libinput_dev, LIBINPUT_CONFIG_TAP_MAP_LRM);
  libinput_device_config_tap_set_drag_enabled(libinput_dev, LIBINPUT_CONFIG_DRAG_ENABLED);
  wlr_log(WLR_INFO, "touchpad: enabled tap-to-click for %s", device->name);
}

static void add_keyboard(struct greeter_server* server, struct wlr_input_device* device) {
  struct greeter_keyboard* keyboard = calloc(1, sizeof(*keyboard));
  if (keyboard == NULL) {
    return;
  }
  keyboard->server = server;
  keyboard->wlr_keyboard = wlr_keyboard_from_input_device(device);

  struct xkb_context* context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
  struct xkb_keymap* keymap = compose_keyboard_keymap(context, server);
  wlr_keyboard_set_keymap(keyboard->wlr_keyboard, keymap);
  xkb_keymap_unref(keymap);
  xkb_context_unref(context);

  // Enable Num Lock by default so numeric keypads work on the greeter.
  // Can be disabled via greeter.toml: [keyboard] numlock = false
  if (server->keyboard_numlock >= 0) {
    xkb_mod_index_t num_mod = xkb_keymap_mod_get_index(keyboard->wlr_keyboard->keymap, XKB_MOD_NAME_NUM);
    if (num_mod != XKB_MOD_INVALID) {
      xkb_mod_mask_t locked = keyboard->wlr_keyboard->modifiers.locked | ((xkb_mod_mask_t)1 << num_mod);
      wlr_keyboard_notify_modifiers(
          keyboard->wlr_keyboard, keyboard->wlr_keyboard->modifiers.depressed,
          keyboard->wlr_keyboard->modifiers.latched, locked, keyboard->wlr_keyboard->modifiers.group
      );
    }
  }
  wlr_keyboard_set_repeat_info(keyboard->wlr_keyboard, 25, 600);

  keyboard->modifiers.notify = handle_keyboard_modifiers;
  wl_signal_add(&keyboard->wlr_keyboard->events.modifiers, &keyboard->modifiers);
  keyboard->key.notify = handle_keyboard_key;
  wl_signal_add(&keyboard->wlr_keyboard->events.key, &keyboard->key);
  keyboard->destroy.notify = handle_keyboard_destroy;
  wl_signal_add(&device->events.destroy, &keyboard->destroy);
  wl_list_insert(&server->keyboards, &keyboard->link);

  wlr_seat_set_keyboard(server->seat, keyboard->wlr_keyboard);
}

static void handle_new_input(struct wl_listener* listener, void* data) {
  struct greeter_server* server = wl_container_of(listener, server, new_input);
  struct wlr_input_device* device = data;
  uint32_t caps = server->seat->capabilities;

  switch (device->type) {
  case WLR_INPUT_DEVICE_KEYBOARD:
    add_keyboard(server, device);
    caps |= WL_SEAT_CAPABILITY_KEYBOARD;
    break;
  case WLR_INPUT_DEVICE_POINTER:
    configure_libinput_touchpad(device);
    wlr_cursor_attach_input_device(server->cursor, device);
    caps |= WL_SEAT_CAPABILITY_POINTER;
    break;
  case WLR_INPUT_DEVICE_TOUCH:
  case WLR_INPUT_DEVICE_TABLET:
    wlr_cursor_attach_input_device(server->cursor, device);
    caps |= WL_SEAT_CAPABILITY_POINTER;
    break;
  default:
    break;
  }

  wlr_seat_set_capabilities(server->seat, caps);
}

static void handle_xdg_commit(struct wl_listener* listener, void* data) {
  (void)data;
  struct greeter_view* view = wl_container_of(listener, view, commit);
  struct wlr_xdg_surface* xdg = view->toplevel->base;
  if (xdg->initial_commit) {
    wlr_xdg_surface_schedule_configure(xdg);
    return;
  }
  schedule_output_frames(view->server);
}

static void handle_view_map(struct wl_listener* listener, void* data) {
  (void)data;
  struct greeter_view* view = wl_container_of(listener, view, map);
  view->mapped = true;
  wlr_log(WLR_INFO, "greeter surface mapped on %s", view->output != NULL ? view->output->wlr_output->name : "unknown");
  if (view->output != NULL) {
    notify_surface_scale(view->server, view->toplevel->base->surface, view->output);
  }
  configure_view(view);
}

static void handle_view_unmap(struct wl_listener* listener, void* data) {
  (void)data;
  struct greeter_view* view = wl_container_of(listener, view, unmap);
  view->mapped = false;
}

static void handle_view_destroy(struct wl_listener* listener, void* data) {
  (void)data;
  struct greeter_view* view = wl_container_of(listener, view, destroy);
  if (view->output != NULL) {
    view->output->view = NULL;
    view->output = NULL;
  }
  wl_list_remove(&view->map.link);
  wl_list_remove(&view->unmap.link);
  wl_list_remove(&view->destroy.link);
  wl_list_remove(&view->commit.link);
  free(view);
}

static void handle_new_toplevel(struct wl_listener* listener, void* data) {
  struct greeter_server* server = wl_container_of(listener, server, new_toplevel);
  struct wlr_xdg_toplevel* toplevel = data;

  struct greeter_output* output = output_for_next_view(server);
  if (output == NULL) {
    wlr_log(WLR_INFO, "closing unassigned toplevel (no free output)");
    wlr_xdg_toplevel_send_close(toplevel);
    return;
  }
  wlr_log(WLR_INFO, "assigning toplevel to output %s", output->wlr_output->name);

  struct greeter_view* view = calloc(1, sizeof(*view));
  if (view == NULL) {
    wlr_xdg_toplevel_send_close(toplevel);
    return;
  }
  view->server = server;
  view->output = output;
  output->view = view;
  view->toplevel = toplevel;
  view->scene_tree = wlr_scene_xdg_surface_create(&server->scene->tree, toplevel->base);
  view->scene_tree->node.data = view;

  view->map.notify = handle_view_map;
  wl_signal_add(&toplevel->base->surface->events.map, &view->map);
  view->unmap.notify = handle_view_unmap;
  wl_signal_add(&toplevel->base->surface->events.unmap, &view->unmap);
  view->commit.notify = handle_xdg_commit;
  wl_signal_add(&toplevel->base->surface->events.commit, &view->commit);
  view->destroy.notify = handle_view_destroy;
  wl_signal_add(&toplevel->events.destroy, &view->destroy);
}

static void handle_request_set_selection(struct wl_listener* listener, void* data) {
  struct greeter_server* server = wl_container_of(listener, server, request_set_selection);
  struct wlr_seat_request_set_selection_event* event = data;
  wlr_seat_set_selection(server->seat, event->source, event->serial);
}

static int handle_sigchld(int signal, void* data) {
  (void)signal;
  struct greeter_server* server = data;
  int status = 0;
  pid_t pid;
  while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
    if (pid == server->child_pid) {
      if (WIFEXITED(status)) {
        wlr_log(WLR_INFO, "greeter exited with status %d", WEXITSTATUS(status));
      } else if (WIFSIGNALED(status)) {
        wlr_log(WLR_INFO, "greeter exited on signal %d", WTERMSIG(status));
      } else {
        wlr_log(WLR_INFO, "greeter exited");
      }
      wl_display_terminate(server->display);
    }
  }
  return 0;
}

static char** child_argv(int argc, char** argv) {
  const char* greeter_bin = getenv("GREETER_BIN");
  if (greeter_bin == NULL || greeter_bin[0] == '\0') {
    greeter_bin = NOCTALIA_GREETER_INSTALLED_BINDIR "/noctalia-greeter";
  }

  if (argc > 1 && strcmp(argv[1], "--") == 0) {
    char** child = calloc((size_t)argc + 1, sizeof(char*));
    if (child == NULL) {
      return NULL;
    }
    child[0] = (char*)greeter_bin;
    for (int i = 2; i < argc; ++i) {
      child[i - 1] = argv[i];
    }
    child[argc - 1] = NULL;
    return child;
  }

  if (argc > 1) {
    return &argv[1];
  }

  char** child = calloc(2, sizeof(char*));
  if (child == NULL) {
    return NULL;
  }
  child[0] = (char*)greeter_bin;
  child[1] = NULL;
  return child;
}

static void try_launch_greeter(void* data) {
  struct greeter_server* server = data;

  if (server->child_launched) {
    return;
  }
  if (use_all_outputs(server)) {
    if (!all_outputs_active(server)) {
      if (!any_output_active(server)) {
        schedule_launch(server);
        return;
      }
    }
  } else if (!any_output_active(server)) {
    return;
  }

  warp_cursor_to_initial_position(server);

  char** argv = child_argv(server->child_argc, server->child_argv_ptr);
  bool free_argv = server->child_argc <= 1;
  if (argv == NULL || !start_child(server, argv, free_argv)) {
    wlr_log(WLR_ERROR, "failed to start greeter client");
    wl_display_terminate(server->display);
    return;
  }

  server->child_launched = true;
  const char* greeter_bin = getenv("GREETER_BIN");
  if (greeter_bin == NULL || greeter_bin[0] == '\0') {
    greeter_bin = NOCTALIA_GREETER_INSTALLED_BINDIR "/noctalia-greeter";
  }
  wlr_log(WLR_INFO, "started greeter: %s", greeter_bin);
}

static bool start_child(struct greeter_server* server, char** argv, bool free_argv) {
  server->child_pid = fork();
  if (server->child_pid < 0) {
    wlr_log(WLR_ERROR, "fork failed: %s", strerror(errno));
    if (free_argv) {
      free(argv);
    }
    return false;
  }
  if (server->child_pid == 0) {
    execvp(argv[0], argv);
    fprintf(stderr, "exec %s failed: %s\n", argv[0], strerror(errno));
    _exit(127);
  }
  if (free_argv) {
    free(argv);
  }
  return true;
}

static void remove_listener_if_set(struct wl_listener* listener) {
  if (listener->notify != NULL) {
    wl_list_remove(&listener->link);
    listener->notify = NULL;
  }
}

static void cleanup_server_listeners(struct greeter_server* server) {
  server->shutting_down = true;
  remove_listener_if_set(&server->new_output);
  remove_listener_if_set(&server->new_input);
  remove_listener_if_set(&server->new_toplevel);
  remove_listener_if_set(&server->cursor_motion);
  remove_listener_if_set(&server->cursor_motion_absolute);
  remove_listener_if_set(&server->cursor_button);
  remove_listener_if_set(&server->cursor_axis);
  remove_listener_if_set(&server->cursor_frame);
  remove_listener_if_set(&server->request_set_cursor);
  remove_listener_if_set(&server->request_set_selection);
}

int main(int argc, char** argv) {
  wlr_log_init(WLR_INFO, NULL);

  struct greeter_server server = {0};
  wl_list_init(&server.outputs);
  wl_list_init(&server.keyboards);
  read_greeter_config(&server);

  server.display = wl_display_create();
  if (server.display == NULL) {
    fprintf(stderr, "failed to create Wayland display\n");
    return 1;
  }

  server.backend = wlr_backend_autocreate(wl_display_get_event_loop(server.display), &server.session);
  if (server.backend == NULL) {
    fprintf(stderr, "failed to create wlroots backend\n");
    wl_display_destroy(server.display);
    return 1;
  }
  server.renderer = wlr_renderer_autocreate(server.backend);
  if (server.renderer == NULL || !wlr_renderer_init_wl_display(server.renderer, server.display)) {
    fprintf(stderr, "failed to create wlroots renderer\n");
    wl_display_destroy(server.display);
    return 1;
  }
  server.allocator = wlr_allocator_autocreate(server.backend, server.renderer);
  if (server.allocator == NULL) {
    fprintf(stderr, "failed to create wlroots allocator\n");
    wl_display_destroy(server.display);
    return 1;
  }

  wlr_compositor_create(server.display, 6, server.renderer);
  wlr_subcompositor_create(server.display);
  wlr_data_device_manager_create(server.display);
  wlr_viewporter_create(server.display);
  server.fractional_scale = wlr_fractional_scale_manager_v1_create(server.display, 1);
  server.output_layout = wlr_output_layout_create(server.display);
  server.scene = wlr_scene_create();
  server.scene_output_layout = wlr_scene_attach_output_layout(server.scene, server.output_layout);
  server.xdg_shell = wlr_xdg_shell_create(server.display, 6);
  server.seat = wlr_seat_create(server.display, "seat0");
  server.cursor = wlr_cursor_create();

  const char* cursor_theme = NULL;
  if (server.cursor_theme[0] != '\0') {
    cursor_theme = server.cursor_theme;
  } else {
    const char* env_theme = getenv("XCURSOR_THEME");
    if (env_theme != NULL && env_theme[0] != '\0') {
      cursor_theme = env_theme;
    }
  }

  uint32_t cursor_size = 24;
  if (server.cursor_size > 0) {
    cursor_size = (uint32_t)server.cursor_size;
  } else {
    const char* env_size = getenv("XCURSOR_SIZE");
    if (env_size != NULL && env_size[0] != '\0') {
      const long parsed = strtol(env_size, NULL, 10);
      if (parsed > 0 && parsed <= 1024) {
        cursor_size = (uint32_t)parsed;
      }
    }
  }

  if (server.cursor_path[0] != '\0') {
    setenv("XCURSOR_PATH", server.cursor_path, 1);
  }

  server.cursor_mgr = wlr_xcursor_manager_create(cursor_theme, cursor_size);
  wlr_cursor_attach_output_layout(server.cursor, server.output_layout);
  wlr_cursor_set_xcursor(server.cursor, server.cursor_mgr, "default");

  server.new_output.notify = handle_new_output;
  wl_signal_add(&server.backend->events.new_output, &server.new_output);
  server.new_input.notify = handle_new_input;
  wl_signal_add(&server.backend->events.new_input, &server.new_input);
  server.new_toplevel.notify = handle_new_toplevel;
  wl_signal_add(&server.xdg_shell->events.new_toplevel, &server.new_toplevel);

  server.cursor_motion.notify = handle_cursor_motion;
  wl_signal_add(&server.cursor->events.motion, &server.cursor_motion);
  server.cursor_motion_absolute.notify = handle_cursor_motion_absolute;
  wl_signal_add(&server.cursor->events.motion_absolute, &server.cursor_motion_absolute);
  server.cursor_button.notify = handle_cursor_button;
  wl_signal_add(&server.cursor->events.button, &server.cursor_button);
  server.cursor_axis.notify = handle_cursor_axis;
  wl_signal_add(&server.cursor->events.axis, &server.cursor_axis);
  server.cursor_frame.notify = handle_cursor_frame;
  wl_signal_add(&server.cursor->events.frame, &server.cursor_frame);
  server.request_set_cursor.notify = handle_request_set_cursor;
  wl_signal_add(&server.seat->events.request_set_cursor, &server.request_set_cursor);
  server.request_set_selection.notify = handle_request_set_selection;
  wl_signal_add(&server.seat->events.request_set_selection, &server.request_set_selection);

  const char* socket = wl_display_add_socket_auto(server.display);
  if (socket == NULL) {
    fprintf(stderr, "failed to create Wayland socket\n");
    cleanup_server_listeners(&server);
    wl_display_destroy(server.display);
    return 1;
  }
  setenv("WAYLAND_DISPLAY", socket, 1);
  unsetenv("DISPLAY");

  if (!wlr_backend_start(server.backend)) {
    fprintf(stderr, "failed to start wlroots backend\n");
    cleanup_server_listeners(&server);
    wl_display_destroy(server.display);
    return 1;
  }

  struct wl_event_loop* loop = wl_display_get_event_loop(server.display);
  server.sigchld_source = wl_event_loop_add_signal(loop, SIGCHLD, handle_sigchld, &server);
  server.child_argc = argc;
  server.child_argv_ptr = argv;
  schedule_launch(&server);

  wlr_log(WLR_INFO, "running on WAYLAND_DISPLAY=%s", socket);
  wl_display_run(server.display);

  if (server.child_pid > 0) {
    kill(server.child_pid, SIGTERM);
  }
  if (server.sigchld_source != NULL) {
    wl_event_source_remove(server.sigchld_source);
  }
  cleanup_server_listeners(&server);
  wl_display_destroy_clients(server.display);
  wl_display_destroy(server.display);
  return 0;
}
