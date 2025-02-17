#ifndef IPC_H
#define IPC_H

#include <xcb/xcb.h>

// Window manager command atom name
#define WM_COMMAND_ATOM "_WM_COMMAND"

// Initialize window manager command atom
xcb_atom_t
init_wm_command_atom(xcb_connection_t* conn);

#endif /* IPC_H */