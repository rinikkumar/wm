#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <xcb/xcb.h>

#include "config.h"
#include "ipc.h"
#include "utils.h"

enum WindowState
{
  STATE_NORMAL,
  STATE_FULLSCREEN,
  STATE_SNAPPED_LEFT,
  STATE_SNAPPED_RIGHT,
  STATE_MAXIMIZED
};

struct Window
{
  xcb_window_t id;        // Original window
  xcb_window_t frame;     // Frame containing header + window
  xcb_window_t header;    // Header window
  int16_t x, y;           // Position
  uint16_t width, height; // Dimensions
  enum WindowState state; // Window state
  struct
  {
    int16_t x, y;
    uint16_t width, height;
  } saved;             // Saved position/dimensions
  struct Window* next; // Next window in list
};

struct Workspace
{
  struct Window* windows;
  int window_count;
  struct Window* focused;
};

struct
{
  struct Window* window; // Window being dragged (NULL if not dragging)
  int16_t orig_x;        // Original window X position
  int16_t orig_y;        // Original window Y position
  int16_t press_x;       // Mouse X position when button was pressed
  int16_t press_y;       // Mouse Y position when button was pressed
} drag_state = { 0 };

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
static xcb_atom_t quit_command_atom;
static struct Workspace workspaces[MAX_WORKSPACES] = { 0 };
static int current_workspace = 0;

static struct Window*
window_create(xcb_window_t id,
              xcb_window_t frame,
              xcb_window_t header,
              int16_t x,
              int16_t y,
              uint16_t width,
              uint16_t height)
{
  struct Workspace* ws = &workspaces[current_workspace];
  ws->windows =
    realloc(ws->windows, sizeof(struct Window) * (ws->window_count + 1));
  struct Window* win = &ws->windows[ws->window_count++];

  win->id = id;
  win->frame = frame;
  win->header = header;
  win->x = x;
  win->y = y;
  win->width = width;
  win->height = height;
  win->state = STATE_NORMAL;

  return win;
}

static struct Window*
window_find(xcb_window_t id)
{
  struct Workspace* ws = &workspaces[current_workspace];
  for (int i = 0; i < ws->window_count; i++) {
    if (ws->windows[i].id == id || ws->windows[i].frame == id ||
        ws->windows[i].header == id)
      return &ws->windows[i];
  }
  return NULL;
}

static void
window_delete(xcb_window_t id)
{
  struct Workspace* ws = &workspaces[current_workspace];
  for (int i = 0; i < ws->window_count; i++) {
    if (ws->windows[i].id == id) {
      if (&ws->windows[i] == ws->focused) {
        ws->focused = NULL;
      }
      memmove(&ws->windows[i],
              &ws->windows[i + 1],
              sizeof(struct Window) * (ws->window_count - i - 1));
      ws->window_count--;
      ws->windows =
        realloc(ws->windows, sizeof(struct Window) * ws->window_count);
      return;
    }
  }
}

static void
save_window_state(struct Window* win)
{
  if (win->state == STATE_NORMAL) {
    win->saved.x = win->x;
    win->saved.y = win->y;
    win->saved.width = win->width;
    win->saved.height = win->height;
  }
}

static void
restore_window_state(struct Window* win)
{
  win->state = STATE_NORMAL;
  win->x = win->saved.x;
  win->y = win->saved.y;
  win->width = win->saved.width;
  win->height = win->saved.height;
}

static void
focus_window(struct Window* win)
{
  struct Workspace* ws = &workspaces[current_workspace];
  if (win == ws->focused)
    return;

  // Update colors for all windows in current workspace
  for (int i = 0; i < ws->window_count; i++) {
    uint32_t header_color =
      (win == &ws->windows[i]) ? FOCUSED_HEADER_COLOR : UNFOCUSED_HEADER_COLOR;
    uint32_t border_color =
      (win == &ws->windows[i]) ? FOCUSED_BORDER_COLOR : UNFOCUSED_BORDER_COLOR;

    uint32_t values[] = { header_color };
    xcb_change_window_attributes(
      conn, ws->windows[i].header, XCB_CW_BACK_PIXEL, values);

    values[0] = border_color;
    xcb_change_window_attributes(
      conn, ws->windows[i].frame, XCB_CW_BORDER_PIXEL, values);

    xcb_clear_area(conn, 0, ws->windows[i].header, 0, 0, 0, 0);
  }

  // Raise focused window
  if (win) {
    uint32_t values[] = { XCB_STACK_MODE_ABOVE };
    xcb_configure_window(
      conn, win->frame, XCB_CONFIG_WINDOW_STACK_MODE, values);
  }

  ws->focused = win;
  xcb_flush(conn);
}

static void
resize_window(struct Window* win,
              int16_t x,
              int16_t y,
              uint16_t width,
              uint16_t height,
              bool show_decorations)
{
  win->x = x;
  win->y = y;
  win->width = width;
  win->height = height;

  // Configure the frame window
  uint32_t frame_vals[] = {
    win->x, win->y, win->width, win->height, show_decorations ? BORDER_SIZE : 0
  };
  xcb_configure_window(conn,
                       win->frame,
                       XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y |
                         XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT |
                         XCB_CONFIG_WINDOW_BORDER_WIDTH,
                       frame_vals);

  // Configure the header window
  if (show_decorations) {
    xcb_map_window(conn, win->header);
    uint32_t header_vals[] = { 0, 0, win->width, HEADER_SIZE };
    xcb_configure_window(conn,
                         win->header,
                         XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y |
                           XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT,
                         header_vals);
  } else {
    xcb_unmap_window(conn, win->header);
  }

  // Configure the client window
  uint32_t client_vals[] = {
    0,
    show_decorations ? HEADER_SIZE : 0,
    win->width - (show_decorations ? 2 * BORDER_SIZE : 0),
    win->height - (show_decorations ? HEADER_SIZE + 2 * BORDER_SIZE : 0)
  };
  xcb_configure_window(conn,
                       win->id,
                       XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y |
                         XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT,
                       client_vals);

  xcb_flush(conn);
}

static void
switch_to_workspace(int workspace)
{
  if (workspace < 0 || workspace >= MAX_WORKSPACES ||
      workspace == current_workspace) {
    return;
  }

  // Hide all windows in current workspace
  for (int i = 0; i < workspaces[current_workspace].window_count; i++) {
    xcb_unmap_window(conn, workspaces[current_workspace].windows[i].frame);
  }

  current_workspace = workspace;

  // Show all windows in target workspace
  for (int i = 0; i < workspaces[current_workspace].window_count; i++) {
    xcb_map_window(conn, workspaces[current_workspace].windows[i].frame);
  }

  // Restore focused window
  if (workspaces[current_workspace].focused) {
    focus_window(workspaces[current_workspace].focused);
  }

  xcb_flush(conn);
}

static void
send_window_to_workspace(struct Window* win, int workspace)
{
  if (!win || workspace < 0 || workspace >= MAX_WORKSPACES ||
      workspace == current_workspace) {
    return;
  }

  // Add window to target workspace
  struct Workspace* target = &workspaces[workspace];
  target->windows = realloc(target->windows,
                            sizeof(struct Window) * (target->window_count + 1));
  target->windows[target->window_count++] = *win;

  // Hide window
  xcb_unmap_window(conn, win->frame);

  // Remove from current workspace
  window_delete(win->id);

  xcb_flush(conn);
}

static void
handle_kill_window()
{
  struct Window* focused_window = workspaces[current_workspace].focused;
  if (focused_window) {
    xcb_kill_client(conn, focused_window->id);
    xcb_flush(conn);
  }
}

static void
handle_move_window(xcb_client_message_event_t* ev)
{
  struct Window* focused_window = workspaces[current_workspace].focused;
  if (focused_window) {
    int16_t dx = ev->data.data32[0];
    int16_t dy = ev->data.data32[1];

    focused_window->x += dx;
    focused_window->y += dy;

    uint32_t values[2] = { focused_window->x, focused_window->y };
    xcb_configure_window(conn,
                         focused_window->frame,
                         XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y,
                         values);
    xcb_flush(conn);
  }
}

static void
handle_resize_window(xcb_client_message_event_t* ev)
{
  struct Window* focused_window = workspaces[current_workspace].focused;
  if (!focused_window)
    return;

  int16_t dx = ev->data.data32[0];
  int16_t dy = ev->data.data32[1];

  resize_window(focused_window,
                focused_window->x,
                focused_window->y,
                focused_window->width + dx,
                focused_window->height + dy,
                true);
}

static void
focus_window_relative(int direction)
{
  struct Workspace* ws = &workspaces[current_workspace];
  if (!ws->window_count)
    return;

  if (!ws->focused) {
    focus_window(&ws->windows[0]);
    return;
  }

  // Find current window index
  int current = 0;
  for (int i = 0; i < ws->window_count; i++) {
    if (&ws->windows[i] == ws->focused) {
      current = i;
      break;
    }
  }

  // Calculate new index with wrap-around
  int new_index = (current + direction + ws->window_count) % ws->window_count;
  focus_window(&ws->windows[new_index]);
}

static void
handle_toggle_snap_left(void)
{
  struct Window* focused_window = workspaces[current_workspace].focused;
  if (!focused_window)
    return;

  if (focused_window->state != STATE_SNAPPED_LEFT) {
    save_window_state(focused_window);
    focused_window->state = STATE_SNAPPED_LEFT;
    resize_window(focused_window,
                  0,
                  0,
                  screen->width_in_pixels / 2,
                  screen->height_in_pixels,
                  true);
  } else {
    restore_window_state(focused_window);
    resize_window(focused_window,
                  focused_window->x,
                  focused_window->y,
                  focused_window->width,
                  focused_window->height,
                  true);
  }
}

static void
handle_toggle_snap_right(void)
{
  struct Window* focused_window = workspaces[current_workspace].focused;
  if (!focused_window)
    return;

  if (focused_window->state != STATE_SNAPPED_RIGHT) {
    save_window_state(focused_window);
    focused_window->state = STATE_SNAPPED_RIGHT;
    resize_window(focused_window,
                  screen->width_in_pixels / 2,
                  0,
                  screen->width_in_pixels / 2,
                  screen->height_in_pixels,
                  true);
  } else {
    restore_window_state(focused_window);
    resize_window(focused_window,
                  focused_window->x,
                  focused_window->y,
                  focused_window->width,
                  focused_window->height,
                  true);
  }
}

static void
handle_toggle_maximize(void)
{
  struct Window* focused_window = workspaces[current_workspace].focused;
  if (!focused_window)
    return;

  if (focused_window->state != STATE_MAXIMIZED) {
    save_window_state(focused_window);
    focused_window->state = STATE_MAXIMIZED;
    resize_window(focused_window,
                  0,
                  0,
                  screen->width_in_pixels,
                  screen->height_in_pixels,
                  true);
  } else {
    restore_window_state(focused_window);
    resize_window(focused_window,
                  focused_window->x,
                  focused_window->y,
                  focused_window->width,
                  focused_window->height,
                  true);
  }
}

static void
handle_toggle_fullscreen(void)
{
  struct Window* focused_window = workspaces[current_workspace].focused;
  if (!focused_window)
    return;

  if (focused_window->state != STATE_FULLSCREEN) {
    save_window_state(focused_window);
    focused_window->state = STATE_FULLSCREEN;
    resize_window(focused_window,
                  0,
                  0,
                  screen->width_in_pixels,
                  screen->height_in_pixels,
                  false);
  } else {
    restore_window_state(focused_window);
    resize_window(focused_window,
                  focused_window->x,
                  focused_window->y,
                  focused_window->width,
                  focused_window->height,
                  true);
  }
}

static void
handle_switch_workspace(xcb_client_message_event_t* ev)
{
  int workspace = ev->data.data32[0];
  switch_to_workspace(workspace);
}

static void
handle_send_to_workspace(xcb_client_message_event_t* ev)
{
  if (!workspaces[current_workspace].focused)
    return;

  int workspace = ev->data.data32[0];
  send_window_to_workspace(workspaces[current_workspace].focused, workspace);
}

static void
handle_quit(void)
{
  xcb_disconnect(conn);
  exit(0);
}

void
handle_map_request(xcb_map_request_event_t* ev)
{
  debug("Received map request for window: %d", ev->window);

  // Get window geometry
  xcb_generic_error_t* error;
  xcb_get_geometry_cookie_t cookie = xcb_get_geometry(conn, ev->window);
  xcb_get_geometry_reply_t* geom = xcb_get_geometry_reply(conn, cookie, &error);
  if (error) {
    debug("Failed to get window geometry for window: %d (error: %d)",
          ev->window,
          error->error_code);
    free(error);
    return;
  }

  // Create frame window
  xcb_window_t frame = xcb_generate_id(conn);
  uint32_t frame_vals[] = { UNFOCUSED_BORDER_COLOR,
                            XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY |
                              XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT };

  int16_t frame_x = geom->x;
  int16_t frame_y = (geom->y < HEADER_SIZE) ? 0 : geom->y - HEADER_SIZE;

  xcb_create_window(conn,
                    screen->root_depth,
                    frame,
                    screen->root,
                    frame_x,
                    frame_y,
                    geom->width,
                    geom->height + HEADER_SIZE,
                    BORDER_SIZE,
                    XCB_WINDOW_CLASS_INPUT_OUTPUT,
                    screen->root_visual,
                    XCB_CW_BORDER_PIXEL | XCB_CW_EVENT_MASK,
                    frame_vals);

  // Create header window
  xcb_window_t header = xcb_generate_id(conn);
  uint32_t header_vals[] = { UNFOCUSED_HEADER_COLOR,
                             XCB_EVENT_MASK_BUTTON_PRESS |
                               XCB_EVENT_MASK_BUTTON_RELEASE |
                               XCB_EVENT_MASK_BUTTON_1_MOTION };

  xcb_create_window(conn,
                    screen->root_depth,
                    header,
                    frame,
                    0,
                    0,
                    geom->width,
                    HEADER_SIZE,
                    0,
                    XCB_WINDOW_CLASS_INPUT_OUTPUT,
                    screen->root_visual,
                    XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK,
                    header_vals);

  struct Window* win = window_create(
    ev->window, frame, header, geom->x, geom->y, geom->width, geom->height);

  // Reparent client window
  xcb_reparent_window(conn, ev->window, frame, 0, HEADER_SIZE);

  // Focus frame window
  focus_window(win);

  xcb_map_window(conn, frame);
  xcb_map_window(conn, header);
  xcb_map_window(conn, ev->window);

  free(geom);
  xcb_flush(conn);
}

static void
handle_configure_request(xcb_configure_request_event_t* ev)
{
  debug("Handling configure request for window: %d", ev->window);

  uint32_t values[7];
  uint32_t value_mask = 0;

  if (ev->value_mask & XCB_CONFIG_WINDOW_X) {
    values[value_mask++] = ev->x;
  }
  if (ev->value_mask & XCB_CONFIG_WINDOW_Y) {
    values[value_mask++] = ev->y;
  }
  if (ev->value_mask & XCB_CONFIG_WINDOW_WIDTH) {
    values[value_mask++] = ev->width;
  }
  if (ev->value_mask & XCB_CONFIG_WINDOW_HEIGHT) {
    values[value_mask++] = ev->height;
  }
  if (ev->value_mask & XCB_CONFIG_WINDOW_BORDER_WIDTH) {
    values[value_mask++] = ev->border_width;
  }
  if (ev->value_mask & XCB_CONFIG_WINDOW_SIBLING) {
    values[value_mask++] = ev->sibling;
  }
  if (ev->value_mask & XCB_CONFIG_WINDOW_STACK_MODE) {
    values[value_mask++] = ev->stack_mode;
  }

  xcb_configure_window(conn, ev->window, ev->value_mask, values);
  xcb_flush(conn);
}

static void
handle_create_notify(xcb_create_notify_event_t* ev)
{
  debug("Window %d created at (%d, %d) with dimensions %dx%d",
        ev->window,
        ev->x,
        ev->y,
        ev->width,
        ev->height);
}

static void
handle_destroy_notify(xcb_destroy_notify_event_t* ev)
{
  debug("Window %d destroyed", ev->window);

  struct Window* win = window_find(ev->window);
  if (win) {
    // Clean up frame and header
    xcb_destroy_window(conn, win->frame);
    xcb_destroy_window(conn, win->header);

    window_delete(win->id);

    xcb_flush(conn);
  }
}

static void
handle_button_press(xcb_button_press_event_t* ev)
{
  struct Window* win = window_find(ev->event);
  if (!win && ev->child != XCB_NONE) {
    win = window_find(ev->child);
  }

  if (!win) {
    debug(
      "No window found for event window %d or child %d", ev->event, ev->child);
    xcb_allow_events(conn, XCB_ALLOW_REPLAY_POINTER, ev->time);
    xcb_flush(conn);
    return;
  }

  // Focus clicked window
  focus_window(win);

  // If header is clicked with button 1, start drag
  if (ev->event == win->header && ev->detail == XCB_BUTTON_INDEX_1) {
    drag_state.window = win;
    drag_state.orig_x = win->x;
    drag_state.orig_y = win->y;
    drag_state.press_x = ev->root_x;
    drag_state.press_y = ev->root_y;
  }

  xcb_allow_events(conn, XCB_ALLOW_REPLAY_POINTER, ev->time);
  xcb_flush(conn);
}

static void
handle_button_release(xcb_button_release_event_t* ev)
{
  (void)ev;

  if (!drag_state.window)
    return;

  drag_state.window = NULL;
}

static void
handle_motion_notify(xcb_motion_notify_event_t* ev)
{
  if (!drag_state.window)
    return;

  // Calculate the change in position
  int16_t delta_x = ev->root_x - drag_state.press_x;
  int16_t delta_y = ev->root_y - drag_state.press_y;

  // Update position of the frame window
  drag_state.window->x = drag_state.orig_x + delta_x;
  drag_state.window->y = drag_state.orig_y + delta_y;
  uint32_t values[2] = { drag_state.window->x, drag_state.window->y };

  xcb_configure_window(conn,
                       drag_state.window->frame,
                       XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y,
                       values);
  xcb_flush(conn);
}

void
handle_client_message(xcb_client_message_event_t* ev)
{
  if (ev->type == quit_command_atom) {
    handle_quit();
  } else if (ev->type == kill_command_atom) {
    handle_kill_window();
  } else if (ev->type == move_command_atom) {
    handle_move_window(ev);
  } else if (ev->type == resize_command_atom) {
    handle_resize_window(ev);
  } else if (ev->type == focus_next_command_atom) {
    focus_window_relative(1);
  } else if (ev->type == focus_prev_command_atom) {
    focus_window_relative(-1);
  } else if (ev->type == maximize_command_atom) {
    handle_toggle_maximize();
  } else if (ev->type == fullscreen_command_atom) {
    handle_toggle_fullscreen();
  } else if (ev->type == snap_left_command_atom) {
    handle_toggle_snap_left();
  } else if (ev->type == snap_right_command_atom) {
    handle_toggle_snap_right();
  } else if (ev->type == switch_workspace_command_atom) {
    handle_switch_workspace(ev);
  } else if (ev->type == send_to_workspace_command_atom) {
    handle_send_to_workspace(ev);
  } else {
    debug("Unhandled client message type: %d", ev->type);
  }
}

static void
run(void)
{
  xcb_generic_event_t* ev;

  while ((ev = xcb_wait_for_event(conn))) {
    switch (ev->response_type & ~0x80) {
      case XCB_MAP_REQUEST:
        handle_map_request((xcb_map_request_event_t*)ev);
        break;
      case XCB_CONFIGURE_REQUEST:
        handle_configure_request((xcb_configure_request_event_t*)ev);
        break;
      case XCB_CREATE_NOTIFY:
        handle_create_notify((xcb_create_notify_event_t*)ev);
        break;
      case XCB_DESTROY_NOTIFY:
        handle_destroy_notify((xcb_destroy_notify_event_t*)ev);
        break;
      case XCB_BUTTON_PRESS:
        handle_button_press((xcb_button_press_event_t*)ev);
        break;
      case XCB_BUTTON_RELEASE:
        handle_button_release((xcb_button_release_event_t*)ev);
        break;
      case XCB_MOTION_NOTIFY:
        handle_motion_notify((xcb_motion_notify_event_t*)ev);
        break;
      case XCB_ENTER_NOTIFY: // Ignore enter events
      case XCB_LEAVE_NOTIFY: // Ignore leave events
        break;
      case XCB_CLIENT_MESSAGE:
        handle_client_message((xcb_client_message_event_t*)ev);
        break;
      default:
        debug("Unhandled event: %d", ev->response_type & ~0x80);
        break;
    }
    free(ev);
  }
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

  uint32_t values[] = { XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT |
                        XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY |
                        XCB_EVENT_MASK_BUTTON_PRESS |
                        XCB_EVENT_MASK_BUTTON_RELEASE };

  xcb_change_window_attributes(conn, screen->root, XCB_CW_EVENT_MASK, values);

  // Grab all button presses on root window
  xcb_grab_button(conn,
                  0,
                  screen->root,
                  XCB_EVENT_MASK_BUTTON_PRESS,
                  XCB_GRAB_MODE_SYNC,
                  XCB_GRAB_MODE_ASYNC,
                  XCB_NONE,
                  XCB_NONE,
                  XCB_BUTTON_INDEX_ANY,
                  XCB_MOD_MASK_ANY);

  kill_command_atom = init_kill_command_atom(conn);
  move_command_atom = init_move_command_atom(conn);
  resize_command_atom = init_resize_command_atom(conn);
  focus_next_command_atom = init_focus_next_command_atom(conn);
  focus_prev_command_atom = init_focus_prev_command_atom(conn);
  maximize_command_atom = init_maximize_command_atom(conn);
  snap_left_command_atom = init_snap_left_command_atom(conn);
  snap_right_command_atom = init_snap_right_command_atom(conn);
  maximize_command_atom = init_maximize_command_atom(conn);
  fullscreen_command_atom = init_fullscreen_command_atom(conn);
  switch_workspace_command_atom = init_switch_workspace_command_atom(conn);
  send_to_workspace_command_atom = init_send_to_workspace_command_atom(conn);
  quit_command_atom = init_quit_command_atom(conn);

  xcb_flush(conn);
}

int
main(void)
{
  setup();
  run();
  xcb_disconnect(conn);
  return 0;
}