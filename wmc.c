#include "ipc.h"
#include "utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <xcb/xcb.h>

static xcb_connection_t* conn;
static xcb_screen_t* screen;
static xcb_atom_t kill_command_atom;
static xcb_atom_t move_command_atom;
static xcb_atom_t resize_command_atom;
static xcb_atom_t focus_next_command_atom;
static xcb_atom_t focus_prev_command_atom;
static xcb_atom_t snap_left_command_atom;
static xcb_atom_t snap_right_command_atom;
static xcb_atom_t maximize_command_atom;
static xcb_atom_t fullscreen_command_atom;
static xcb_atom_t switch_workspace_command_atom;
static xcb_atom_t send_to_workspace_command_atom;

struct Command
{
  const char* name;
  xcb_atom_t* atom;
  int arg_count;
};

static const struct Command commands[] = {
  { "kill-window", &kill_command_atom, 0 },
  { "move-window", &move_command_atom, 2 },
  { "resize-window", &resize_command_atom, 2 },
  { "focus-next", &focus_next_command_atom, 0 },
  { "focus-prev", &focus_prev_command_atom, 0 },
  { "toggle-snap-left", &snap_left_command_atom, 0 },
  { "toggle-snap-right", &snap_right_command_atom, 0 },
  { "toggle-maximize", &maximize_command_atom, 0 },
  { "toggle-fullscreen", &fullscreen_command_atom, 0 },
  { "switch-to-workspace", &switch_workspace_command_atom, 1 },
  { "send-to-workspace", &send_to_workspace_command_atom, 1 },
};

static void
send_client_message(xcb_connection_t* conn,
                    xcb_window_t root,
                    xcb_client_message_event_t* event)
{
  xcb_void_cookie_t cookie = xcb_send_event_checked(
    conn,
    0,
    root,
    XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT | XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY,
    (char*)event);

  xcb_generic_error_t* error = xcb_request_check(conn, cookie);
  if (error) {
    debug("Failed to send event: %d", error->error_code);
    free(error);
  }

  xcb_flush(conn);
}

static int
parse_int(const char* str)
{
  char* endptr;
  long val = strtol(str, &endptr, 10);
  if (*endptr != '\0') {
    die("Expected integer argument");
  }
  return (int)val;
}

static void
send_command(int argc, char* argv[])
{
  for (size_t i = 0; i < sizeof(commands) / sizeof(commands[0]); i++) {
    if (strcmp(argv[1], commands[i].name) == 0) {
      if (argc != commands[i].arg_count + 2) {
        die("Expected %d arguments", commands[i].arg_count);
      }

      xcb_client_message_event_t event = {
        .response_type = XCB_CLIENT_MESSAGE | 0x80,
        .format = 32,
        .window = screen->root,
        .type = *commands[i].atom,
      };

      for (int j = 0; j < commands[i].arg_count; j++) {
        event.data.data32[j] = parse_int(argv[j + 2]);
      }

      send_client_message(conn, screen->root, &event);
      return;
    }
  }
  debug("Unknown command: %s\n", argv[1]);
  exit(1);
}

static void
setup(void)
{
  conn = xcb_connect(NULL, NULL);
  if (xcb_connection_has_error(conn))
    die("Failed to connect to X server");

  screen = xcb_setup_roots_iterator(xcb_get_setup(conn)).data;
  if (!screen)
    die("Failed to get screen");

  kill_command_atom = init_kill_command_atom(conn);
  move_command_atom = init_move_command_atom(conn);
  resize_command_atom = init_resize_command_atom(conn);
  focus_next_command_atom = init_focus_next_command_atom(conn);
  focus_prev_command_atom = init_focus_prev_command_atom(conn);
  snap_left_command_atom = init_snap_left_command_atom(conn);
  snap_right_command_atom = init_snap_right_command_atom(conn);
  maximize_command_atom = init_maximize_command_atom(conn);
  fullscreen_command_atom = init_fullscreen_command_atom(conn);
  switch_workspace_command_atom = init_switch_workspace_command_atom(conn);
  send_to_workspace_command_atom = init_send_to_workspace_command_atom(conn);

  xcb_flush(conn);
}

int
main(int argc, char* argv[])
{
  if (argc < 2) {
    return 1;
  }
  setup();
  send_command(argc, argv);
  xcb_disconnect(conn);
  return 0;
}
