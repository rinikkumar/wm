#include <stdlib.h>
#include <string.h>
#include <xcb/xcb.h>

#include "ipc.h"
#include "utils.h"

xcb_atom_t
init_wm_command_atom(xcb_connection_t* conn)
{
  xcb_intern_atom_cookie_t cookie =
    xcb_intern_atom(conn, 0, strlen(WM_COMMAND_ATOM), WM_COMMAND_ATOM);
  xcb_intern_atom_reply_t* reply = xcb_intern_atom_reply(conn, cookie, NULL);

  if (!reply)
    die("Failed to create command atom");

  xcb_atom_t atom = reply->atom;
  free(reply);

  return atom;
}