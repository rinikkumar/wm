#include <stdlib.h>
#include <string.h>
#include <xcb/xcb.h>

#include "ipc.h"
#include "utils.h"

static xcb_atom_t
init_atom(xcb_connection_t* conn, const char* name)
{
  xcb_intern_atom_cookie_t cookie =
    xcb_intern_atom(conn, 0, strlen(name), name);
  xcb_intern_atom_reply_t* reply = xcb_intern_atom_reply(conn, cookie, NULL);

  if (!reply)
    die("Failed to create atom");

  xcb_atom_t atom = reply->atom;
  free(reply);

  return atom;
}

xcb_atom_t
init_kill_command_atom(xcb_connection_t* conn)
{
  return init_atom(conn, WM_COMMAND_KILL);
}

xcb_atom_t
init_move_command_atom(xcb_connection_t* conn)
{
  return init_atom(conn, WM_COMMAND_MOVE);
}

xcb_atom_t
init_resize_command_atom(xcb_connection_t* conn)
{
  return init_atom(conn, WM_COMMAND_RESIZE);
}

xcb_atom_t
init_focus_next_command_atom(xcb_connection_t* conn)
{
  return init_atom(conn, WM_COMMAND_FOCUS_NEXT);
}

xcb_atom_t
init_focus_prev_command_atom(xcb_connection_t* conn)
{
  return init_atom(conn, WM_COMMAND_FOCUS_PREV);
}

xcb_atom_t
init_snap_left_command_atom(xcb_connection_t* conn)
{
  return init_atom(conn, WM_COMMAND_SNAP_LEFT);
}

xcb_atom_t
init_snap_right_command_atom(xcb_connection_t* conn)
{
  return init_atom(conn, WM_COMMAND_SNAP_RIGHT);
}

xcb_atom_t
init_maximize_command_atom(xcb_connection_t* conn)
{
  return init_atom(conn, WM_COMMAND_MAXIMIZE);
}

xcb_atom_t
init_fullscreen_command_atom(xcb_connection_t* conn)
{
  return init_atom(conn, WM_COMMAND_FULLSCREEN);
}