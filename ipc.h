#ifndef IPC_H
#define IPC_H

#include <xcb/xcb.h>

// Command atoms
#define WM_COMMAND_KILL "_WM_COMMAND_KILL"
#define WM_COMMAND_MOVE "_WM_COMMAND_MOVE"
#define WM_COMMAND_RESIZE "_WM_COMMAND_RESIZE"
#define WM_COMMAND_FOCUS_NEXT "_WM_COMMAND_FOCUS_NEXT"
#define WM_COMMAND_FOCUS_PREV "_WM_COMMAND_FOCUS_PREV"

// Initialize window manager command atoms
xcb_atom_t
init_kill_command_atom(xcb_connection_t* conn);
xcb_atom_t
init_move_command_atom(xcb_connection_t* conn);
xcb_atom_t
init_resize_command_atom(xcb_connection_t* conn);
xcb_atom_t
init_focus_next_command_atom(xcb_connection_t* conn);
xcb_atom_t
init_focus_prev_command_atom(xcb_connection_t* conn);

#endif /* IPC_H */
