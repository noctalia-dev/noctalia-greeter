// Exercise the production entry point and private handlers without adding a
// testing protocol or bypass to the installed compositor. Link wrappers observe
// its server through the registered backend listener and add fixture-only I/O.
#define main greeter_compositor_main
#include "compositor/noctalia_compositor.c"
#undef main

#include <fcntl.h>
#include <sys/socket.h>
#include <wlr/backend/headless.h>
#include <wlr/backend/multi.h>

static struct greeter_server* test_server;
static struct wlr_backend* headless;
static int control[2];
static bool failed;
static bool completed;
static struct wlr_surface* saved_focus;

static bool check(bool condition, const char* message) {
  if (!condition) {
    fprintf(stderr, "compositor lifecycle: %s\n", message);
    failed = true;
  }
  return condition;
}

static void find_headless(struct wlr_backend* backend, void* data) {
  (void)data;
  if (wlr_backend_is_headless(backend)) {
    headless = backend;
  }
}

bool __real_wlr_backend_start(struct wlr_backend* backend);
bool __wrap_wlr_backend_start(struct wlr_backend* backend) {
  struct wl_listener* listener;
  wl_list_for_each(listener, &backend->events.new_output.listener_list, link) {
    if (listener->notify == handle_new_output) {
      test_server = wl_container_of(listener, test_server, new_output);
      break;
    }
  }
  wlr_multi_for_each_backend(backend, find_headless, NULL);
  return __real_wlr_backend_start(backend);
}

static bool check_views(int count) {
  int outputs = 0;
  int mapped = 0;
  struct greeter_output* output;
  wl_list_for_each(output, &test_server->outputs, link) {
    outputs++;
    if (output->view != NULL) {
      pid_t pid = 0;
      wl_client_get_credentials(output->view->toplevel->base->client->client, &pid, NULL, NULL);
      if (!check(pid == test_server->child_pid, "foreign process owns an output")) {
        return false;
      }
      mapped += output->view->mapped;
    }
  }
  return check(outputs == count && mapped == count, "wrong output/mapped-view count");
}

static bool check_focus(void) {
  struct wlr_surface* focus = test_server->seat->keyboard_state.focused_surface;
  pid_t pid = 0;
  if (!check(focus != NULL, "remaining greeter has no keyboard focus")) {
    return false;
  }
  wl_client_get_credentials(wl_resource_get_client(focus->resource), &pid, NULL, NULL);
  return check(pid == test_server->child_pid, "keyboard focus belongs to foreign process");
}

static int command_ready(int fd, uint32_t mask, void* data) {
  (void)mask;
  (void)data;
  char command = 0;
  bool ok = read(fd, &command, 1) == 1;
  if (ok) {
    switch (command) {
    case 'E': {
      struct greeter_output* output;
      wl_list_for_each(output, &test_server->outputs, link) {
        ok = check(output->view == NULL, "foreign toplevel consumed an empty output") && ok;
      }
      break;
    }
    case 'M':
    case 'V':
      ok = check_views(2) && check_focus();
      break;
    case 'B':
      saved_focus = test_server->seat->keyboard_state.focused_surface;
      ok = check_focus();
      break;
    case 'F':
      ok = check_views(2)
          && check_focus()
          && check(test_server->seat->keyboard_state.focused_surface == saved_focus, "foreign client stole focus");
      break;
    case 'D': {
      struct greeter_output* output = output_by_identifier(test_server, "HEADLESS-1");
      ok = check(output != NULL && output->view != NULL && output->view->mapped, "missing live removal target");
      if (ok) {
        struct greeter_view* view = output->view;
        wlr_output_destroy(output->wlr_output);
        ok = check(view->output == NULL, "destroyed output left a dangling view pointer");
      }
      break;
    }
    case 'S':
      ok = check_views(1) && check_focus();
      break;
    case 'R':
      ok = check(wlr_headless_add_output(headless, 1280, 720) != NULL, "replacement output creation failed");
      break;
    case 'X':
      completed = true;
      break;
    default:
      ok = check(false, "unknown fixture command");
    }
  }
  const char reply = ok ? command : '!';
  if (write(fd, &reply, 1) != 1 || !ok) {
    failed = true;
    wl_display_terminate(test_server->display);
  }
  return 0;
}

static int deadline_expired(void* data) {
  (void)data;
  check(false, "fixture timed out");
  wl_display_terminate(test_server->display);
  return 0;
}

void __real_wl_display_run(struct wl_display* display);
void __wrap_wl_display_run(struct wl_display* display) {
  if (!check(test_server != NULL && headless != NULL, "production headless server not observed")) {
    return;
  }
  // A real seat keyboard lets the fixture assert compositor-side focus, even
  // though the headless backend supplies no physical input devices.
  struct wlr_keyboard keyboard;
  static const struct wlr_keyboard_impl impl = {.name = "lifecycle-test"};
  wlr_keyboard_init(&keyboard, &impl, "lifecycle-test");
  handle_new_input(&test_server->new_input, &keyboard.base);
  struct wl_event_loop* loop = wl_display_get_event_loop(display);
  struct wl_event_source* commands = wl_event_loop_add_fd(loop, control[0], WL_EVENT_READABLE, command_ready, NULL);
  struct wl_event_source* deadline = wl_event_loop_add_timer(loop, deadline_expired, NULL);
  if (check(commands != NULL && deadline != NULL, "fixture event source creation failed")) {
    wl_event_source_timer_update(deadline, 20000);
    __real_wl_display_run(display);
  }
  if (commands != NULL) {
    wl_event_source_remove(commands);
  }
  if (deadline != NULL) {
    wl_event_source_remove(deadline);
  }
  wlr_keyboard_finish(&keyboard);
}

int main(int argc, char** argv) {
  if (socketpair(AF_UNIX, SOCK_SEQPACKET, 0, control) != 0 || fcntl(control[0], F_SETFD, FD_CLOEXEC) < 0) {
    perror("control socketpair");
    return 1;
  }
  char descriptor[32];
  snprintf(descriptor, sizeof(descriptor), "%d", control[1]);
  if (setenv("NOCTALIA_TEST_CONTROL_FD", descriptor, 1) != 0) {
    return 1;
  }
  const int result = greeter_compositor_main(argc, argv);
  close(control[0]);
  close(control[1]);
  if (result != 0 || failed || !completed) {
    return 1;
  }
  puts("compositor lifecycle: output destruction, reassignment and client admission passed");
  return 0;
}
