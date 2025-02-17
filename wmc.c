#include "ipc.h"
#include "utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <xcb/xcb.h>

static xcb_connection_t* conn;
static xcb_screen_t* screen;

static xcb_atom_t wm_command_atom;

void
send_command(const char* cmd)
{
  // Create and send client message event
  xcb_client_message_event_t event = { .response_type =
                                         XCB_CLIENT_MESSAGE | 0x80,
                                       .format = 8,
                                       .window = screen->root,
                                       .type = wm_command_atom,
                                       .data = { .data8 = { 0 } } };

  strncpy((char*)event.data.data8, cmd, 20);

  xcb_void_cookie_t cookie2 =
    xcb_send_event_checked(conn, 0, screen->root, 0, (char*)&event);

  xcb_generic_error_t* error = xcb_request_check(conn, cookie2);
  if (error) {
    debug("Failed to send event: %d\n", error->error_code);
    free(error);
  }

  xcb_flush(conn);
}

void
setup(void)
{
  conn = xcb_connect(NULL, NULL);
  if (xcb_connection_has_error(conn))
    die("Failed to connect to X server");

  screen = xcb_setup_roots_iterator(xcb_get_setup(conn)).data;
  if (!screen)
    die("Failed to get screen");

  // Create command atom for IPC
  wm_command_atom = init_wm_command_atom(conn);

  xcb_flush(conn);
}

int
main(int argc, char* argv[])
{
  if (argc != 2) {
    return 1;
  }
  setup();
  send_command(argv[1]);
  xcb_disconnect(conn);
  return 0;
}
