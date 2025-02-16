#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <xcb/xcb.h>

#include "config.h"

struct Window
{
  xcb_window_t id;     // Original window
  xcb_window_t frame;  // Frame containing header + window
  xcb_window_t header; // Header window
  int16_t x, y;        // Position
  uint16_t width;      // Width
  uint16_t height;     // Height
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
static struct Window* windows = NULL;
static int window_count = 0;

static int debug_enabled = 1;

void
debug(const char* fmt, ...)
{
  if (!debug_enabled)
    return;

  time_t now = time(NULL);
  char timestamp[64];
  strftime(timestamp, sizeof(timestamp), "%H:%M:%S", localtime(&now));

  va_list ap;
  va_start(ap, fmt);
  fprintf(stderr, "[%s] ", timestamp);
  vfprintf(stderr, fmt, ap);
  fprintf(stderr, "\n");
  va_end(ap);
}

void
die(const char* msg)
{
  fprintf(stderr, "Error: %s\n", msg);
  exit(1);
}

struct Window*
window_create(xcb_window_t id,
              xcb_window_t frame,
              xcb_window_t header,
              int16_t x,
              int16_t y,
              uint16_t width,
              uint16_t height)
{
  windows = realloc(windows, sizeof(struct Window) * (window_count + 1));
  struct Window* win = &windows[window_count++];

  win->id = id;
  win->frame = frame;
  win->header = header;
  win->x = x;
  win->y = y;
  win->width = width;
  win->height = height;

  return win;
}

struct Window*
window_find(xcb_window_t id)
{
  for (int i = 0; i < window_count; i++) {
    if (windows[i].id == id || windows[i].frame == id ||
        windows[i].header == id)
      return &windows[i];
  }
  return NULL;
}

void
window_delete(xcb_window_t id)
{
  for (int i = 0; i < window_count; i++) {
    if (windows[i].id == id) {
      memmove(&windows[i],
              &windows[i + 1],
              sizeof(struct Window) * (window_count - i - 1));
      window_count--;
      windows = realloc(windows, sizeof(struct Window) * window_count);
      return;
    }
  }
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
  uint32_t frame_vals[] = { BORDER_COLOR,
                            XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY |
                              XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT };

  int16_t frame_x = geom->x;
  int16_t frame_y = (geom->y < HEADER_HEIGHT) ? 0 : geom->y - HEADER_HEIGHT;

  xcb_create_window(conn,
                    screen->root_depth,
                    frame,
                    screen->root,
                    frame_x,
                    frame_y,
                    geom->width,
                    geom->height + HEADER_HEIGHT,
                    1,
                    XCB_WINDOW_CLASS_INPUT_OUTPUT,
                    screen->root_visual,
                    XCB_CW_BORDER_PIXEL | XCB_CW_EVENT_MASK,
                    frame_vals);

  // Create header window
  xcb_window_t header = xcb_generate_id(conn);
  uint32_t header_vals[] = { HEADER_COLOR,
                             XCB_EVENT_MASK_BUTTON_PRESS |
                               XCB_EVENT_MASK_BUTTON_RELEASE |
                               XCB_EVENT_MASK_BUTTON_MOTION };

  xcb_create_window(conn,
                    screen->root_depth,
                    header,
                    frame, // parent is frame
                    0,     // x position relative to frame
                    0,     // y position relative to frame
                    geom->width,
                    HEADER_HEIGHT,
                    0,
                    XCB_WINDOW_CLASS_INPUT_OUTPUT,
                    screen->root_visual,
                    XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK,
                    header_vals);

  window_create(
    ev->window, frame, header, geom->x, geom->y, geom->width, geom->height);

  // Reparent client window into frame
  xcb_reparent_window(conn,
                      ev->window,
                      frame,
                      0,              // x position relative to frame
                      HEADER_HEIGHT); // y position relative to frame

  xcb_map_window(conn, frame);
  xcb_map_window(conn, header);
  xcb_map_window(conn, ev->window);

  free(geom);
  xcb_flush(conn);
}

void
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

void
handle_create_notify(xcb_create_notify_event_t* ev)
{
  debug("Window %d created at (%d, %d) with dimensions %dx%d",
        ev->window,
        ev->x,
        ev->y,
        ev->width,
        ev->height);
}

void
handle_destroy_notify(xcb_destroy_notify_event_t* ev)
{
  debug("Window %d destroyed", ev->window);
}

void
handle_button_press(xcb_button_press_event_t* ev)
{
  struct Window* win = window_find(ev->event);
  if (!win || ev->event != win->header)
    return;

  drag_state.window = win;
  drag_state.orig_x = win->x;
  drag_state.orig_y = win->y;
  drag_state.press_x = ev->root_x;
  drag_state.press_y = ev->root_y;
}

void
handle_button_release(xcb_button_release_event_t* ev)
{
  (void)ev;

  if (!drag_state.window)
    return;

  drag_state.window = NULL;
}

void
handle_motion_notify(xcb_motion_notify_event_t* ev)
{
  if (!drag_state.window)
    return;

  // Calculate the change in position
  int16_t delta_x = ev->root_x - drag_state.press_x;
  int16_t delta_y = ev->root_y - drag_state.press_y;

  // Update position of the frame window
  uint32_t values[2] = { drag_state.orig_x + delta_x,
                         drag_state.orig_y + delta_y };

  xcb_configure_window(conn,
                       drag_state.window->frame,
                       XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y,
                       values);
  xcb_flush(conn);
}

void
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
      default:
        debug("Unhandled event: %d", ev->response_type & ~0x80);
        break;
    }
    free(ev);
  }
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

  uint32_t values[] = {
    XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT | XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY |
    XCB_EVENT_MASK_ENTER_WINDOW | XCB_EVENT_MASK_LEAVE_WINDOW |
    XCB_EVENT_MASK_STRUCTURE_NOTIFY | XCB_EVENT_MASK_PROPERTY_CHANGE |
    XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE |
    XCB_EVENT_MASK_KEY_PRESS | XCB_EVENT_MASK_KEY_RELEASE
  };

  xcb_change_window_attributes(conn, screen->root, XCB_CW_EVENT_MASK, values);
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