/**
 * @file glps_common.h
 * @brief Common definitions and structures for GLPS.
 */

#ifndef GLPS_COMMON_H
#define GLPS_COMMON_H

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "utils/logger/pico_logger.h"
#include <stdlib.h>

// Windows
#ifdef GLPS_USE_WIN32
#include <GL/gl.h>
#include <tchar.h>
#include <windows.h>
#endif

// Wayland
#ifdef GLPS_USE_WAYLAND
#include "xdg/wlr-data-control-unstable-v1.h"
#include "xdg/xdg-decorations.h"
#include "xdg/xdg-dialog.h"
#include "xdg/xdg-shell.h"
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <sys/mman.h>
#include <wayland-client-protocol.h>
#include <wayland-client.h>
#include <wayland-egl.h>
#include <xkbcommon/xkbcommon.h>
#endif

#define MAX_WINDOWS 100

/**
 * @struct glps_WindowProperties
 * @brief Properties for a GLPS window.
 */
typedef struct {
  char title[64]; /**< Title of the window. */
  int width;
  int height;
} glps_WindowProperties;

/**
 * @enum GLPS_SCROLL_AXES
 * @brief Scroll axis definitions.
 */
typedef enum {
  GLPS_SCROLL_H_AXIS, /**< Horizontal scroll axis. */
  GLPS_SCROLL_V_AXIS  /**< Vertical scroll axis. */
} GLPS_SCROLL_AXES;

/**
 * @enum GLPS_SCROLL_SOURCE
 * @brief Scroll source definitions.
 */
typedef enum {
  GLPS_SCROLL_SOURCE_FINGER,     /**< Scroll generated by a finger. */
  GLPS_SCROLL_SOURCE_WHEEL,      /**< Scroll generated by a wheel. */
  GLPS_SCROLL_SOURCE_CONTINUOUS, /**< Continuous scrolling source. */
  GLPS_SCROLL_SOURCE_WHEEL_TILT, /**< Tilted wheel scroll source. */
  GLPS_SCROLL_SOURCE_OTHER       /**< Other scroll source. */
} GLPS_SCROLL_SOURCE;

struct glps_Callback {
  void (*keyboard_enter_callback)(
      size_t window_id, void *data); /**< Callback for keyboard enter. */
  void (*keyboard_leave_callback)(
      size_t window_id, void *data); /**< Callback for keyboard leave. */
  void (*keyboard_callback)(size_t window_id, bool state, const char *value,
                            void *data); /**< Callback for keyboard input. */
  void (*mouse_enter_callback)(size_t window_id, double mouse_x, double mouse_y,
                               void *data); /**< Callback for mouse enter. */
  void (*mouse_leave_callback)(size_t window_id,
                               void *data); /**< Callback for mouse leave. */
  void (*mouse_move_callback)(size_t window_id, double mouse_x, double mouse_y,
                              void *data); /**< Callback for mouse move. */
  void (*mouse_click_callback)(size_t window_id, bool state,
                               void *data); /**< Callback for mouse click. */
  void (*mouse_scroll_callback)(size_t window_id, GLPS_SCROLL_AXES axe,
                                GLPS_SCROLL_SOURCE source, double value,
                                int discrete, bool is_stopped,
                                void *data); /**< Callback for mouse scroll. */
  void (*touch_callback)(size_t window_id, int id, double touch_x,
                         double touch_y, bool state, double major, double minor,
                         double orientation,
                         void *data); /**< Callback for touch events. */
  void (*drag_n_drop_callback)(
      size_t window_id, char *mime, char *buff,
      void *data); /**< Callback for drag & drop events. */
  void (*window_resize_callback)(
      size_t window_id, int width, int height,
      void *data); /**< Callback for resize events. */
  void (*window_close_callback)(
      size_t window_id, void *data); /**< Callback for window close event. */
  void (*window_frame_update_callback)(
      size_t window_id, void *data); /**< Callback for window update event. */

  void *mouse_enter_data;
  void *mouse_leave_data;
  void *mouse_move_data;
  void *mouse_click_data;
  void *mouse_scroll_data;
  void *keyboard_enter_data;
  void *keyboard_leave_data;
  void *keyboard_data;
  void *touch_data;
  void *drag_n_drop_data;
  void *window_resize_data;
  void *window_frame_update_data;
  void *window_close_data;
};

#ifdef GLPS_USE_WAYLAND

/**
 * @enum pointer_event_mask
 * @brief Bitmask for pointer event types.
 */
enum pointer_event_mask {
  POINTER_EVENT_ENTER = 1 << 0,         /**< Pointer entered surface. */
  POINTER_EVENT_LEAVE = 1 << 1,         /**< Pointer left surface. */
  POINTER_EVENT_MOTION = 1 << 2,        /**< Pointer motion. */
  POINTER_EVENT_BUTTON = 1 << 3,        /**< Pointer button pressed/released. */
  POINTER_EVENT_AXIS = 1 << 4,          /**< Pointer axis event. */
  POINTER_EVENT_AXIS_SOURCE = 1 << 5,   /**< Pointer axis source event. */
  POINTER_EVENT_AXIS_STOP = 1 << 6,     /**< Pointer axis stop event. */
  POINTER_EVENT_AXIS_DISCRETE = 1 << 7, /**< Pointer axis discrete event. */
};

/**
 * @struct pointer_event
 * @brief Represents pointer event data.
 */
struct pointer_event {
  uint32_t event_mask;  /**< Pointer event mask. */
  wl_fixed_t surface_x; /**< X-coordinate of pointer. */
  wl_fixed_t surface_y; /**< Y-coordinate of pointer. */
  uint32_t button;      /**< Button identifier. */
  uint32_t state;       /**< State of the button (pressed/released). */
  uint32_t time;        /**< Timestamp of the event. */
  uint32_t serial;      /**< Serial number of the event. */
  struct {
    bool valid;         /**< Indicates if the axis data is valid. */
    wl_fixed_t value;   /**< Axis value. */
    int32_t discrete;   /**< Discrete axis value. */
  } axes[2];            /**< Data for horizontal and vertical axes. */
  uint32_t axis_source; /**< Source of the axis event. */

  size_t window_id;
};

/**
 * @enum touch_event_mask
 * @brief Bitmask for touch event types.
 */
enum touch_event_mask {
  TOUCH_EVENT_DOWN = 1 << 0,        /**< Touch down event. */
  TOUCH_EVENT_UP = 1 << 1,          /**< Touch up event. */
  TOUCH_EVENT_MOTION = 1 << 2,      /**< Touch motion event. */
  TOUCH_EVENT_CANCEL = 1 << 3,      /**< Touch cancel event. */
  TOUCH_EVENT_SHAPE = 1 << 4,       /**< Touch shape event. */
  TOUCH_EVENT_ORIENTATION = 1 << 5, /**< Touch orientation event. */
};

/**
 * @struct touch_point
 * @brief Represents a single touch point.
 */
struct touch_point {
  bool valid;             /**< Indicates if the touch point is valid. */
  int32_t id;             /**< Identifier of the touch point. */
  uint32_t event_mask;    /**< Event mask for the touch point. */
  wl_fixed_t surface_x;   /**< X-coordinate of the touch point. */
  wl_fixed_t surface_y;   /**< Y-coordinate of the touch point. */
  wl_fixed_t major;       /**< Major axis of the touch point. */
  wl_fixed_t minor;       /**< Minor axis of the touch point. */
  wl_fixed_t orientation; /**< Orientation of the touch point. */
};

/**
 * @struct touch_event
 * @brief Represents touch event data.
 */
struct touch_event {
  uint32_t event_mask;           /**< Event mask for the touch event. */
  uint32_t time;                 /**< Timestamp of the event. */
  uint32_t serial;               /**< Serial number of the event. */
  struct touch_point points[10]; /**< Array of touch points. */
  size_t window_id;
};

/**
 * @struct glps_EGLContext
 * @brief EGL context for rendering.
 */
typedef struct {
  EGLDisplay dpy; /**< EGL display. */
  EGLContext ctx; /**< EGL context. */
  EGLConfig conf; /**< EGL configuration. */
} glps_EGLContext;

/**
 * @struct glps_WaylandWindow
 * @brief Represents a Wayland window in GLPS.
 */
typedef struct {
  struct xdg_surface *xdg_surface;   /**< XDG surface. */
  struct xdg_toplevel *xdg_toplevel; /**< XDG toplevel. */
  struct wl_surface *wl_surface;     /**< Wayland surface. */

  EGLSurface egl_surface;           /**< EGL surface. */
  struct wl_egl_window *egl_window; /**< Wayland EGL window. */
  glps_WindowProperties properties; /**< Window properties. */
  struct zxdg_toplevel_decoration_v1 *zxdg_toplevel_decoration;
  struct wl_callback *frame_callback;
  // FPS COUNTER
  struct timespec fps_start_time;
  bool fps_is_init;
  void *frame_args;
  uint32_t serial;
} glps_WaylandWindow;

/**
 * @struct glps_WaylandContext
 * @brief Represents the Wayland context for GLPS.
 */
typedef struct {
  struct wl_display *wl_display;       /**< Wayland display. */
  struct wl_registry *wl_registry;     /**< Wayland registry. */
  struct wl_compositor *wl_compositor; /**< Wayland compositor. */
  struct wl_seat *wl_seat;             /**< Wayland seat. */
  struct xdg_wm_base *xdg_wm_base;     /**< XDG WM base. */
  struct zxdg_decoration_manager_v1
      *decoration_manager;                         /**< Decoration manager. */
  struct wl_data_device_manager *data_dvc_manager; /**< Data control Manager. */
  struct wl_data_device *data_dvc; /**< Data device to interact with Clipboard
                                      and Drag&Drop operations. */
  struct wl_data_source *data_src; /**< Clipboard data source.*/
  struct wl_pointer *wl_pointer;   /**< Wayland pointer. */
  struct wl_keyboard *wl_keyboard; /**< Wayland keyboard. */
  struct xkb_state *xkb_state;     /**< Keyboard state. */
  struct xkb_context *xkb_context; /**< Keyboard context. */
  struct xkb_keymap *xkb_keymap;   /**< Keyboard keymap. */
  struct wl_touch *wl_touch;       /**< Wayland touch interface. */
  struct wl_data_offer *current_drag_offer;
  uint32_t current_serial;
  uint32_t keyboard_serial;
  size_t keyboard_window_id;
  size_t mouse_window_id;
  size_t touch_window_id;
  size_t current_drag_n_drop_window;
} glps_WaylandContext;

#endif

#ifdef GLPS_USE_WIN32

typedef struct {
  HWND hwnd;
  HDC hdc;
  glps_WindowProperties properties;
  LARGE_INTEGER fps_start_time;
  LARGE_INTEGER fps_freq;
  bool fps_is_init;
} glps_Win32Window;

typedef struct {
  WNDCLASSEX wc;
  HGLRC hglrc;

} glps_Win32Context;

#endif

struct clipboard_data {
  char mime_type[64];
  char buff[1024];
};

struct glps_debug {
  bool enable_fps_counter;
};
/**
 * @struct glps_WindowManager
 * @brief Represents the manager for GLPS windows.
 */
typedef struct {

#ifdef GLPS_USE_WAYLAND
  glps_WaylandContext *wayland_ctx;   /**< Wayland context. */
  glps_WaylandWindow **windows;       /**< Array of Wayland window pointers. */
  glps_EGLContext *egl_ctx;           /**< EGL context. */
  struct touch_event touch_event;     /**< Current touch event data. */
  struct pointer_event pointer_event; /**< Current pointer event data. */
  struct clipboard_data clipboard;    /**< Current clipboard data. */

#endif

#ifdef GLPS_USE_WIN32
  glps_Win32Context *win32_ctx; /**< Win32 Context */
  glps_Win32Window **windows;   /**< Array of Win32 window pointers. */
  WNDCLASSEX wc;
#endif

  char font_path[256];         /**< Path to the font file. */
  size_t window_count;         /**< Number of managed windows. */
  bool inhibit_reset;          /**< Indicates if reset should be inhibited. */
  unsigned int selected_color; /**< Selected color value. */
  struct glps_debug debug_utilities;
  struct glps_Callback callbacks;

} glps_WindowManager;

/**
 * @struct frame_callback_args
 * @brief Arguments for frame callbacks.
 */
typedef struct {
  glps_WindowManager *wm; /**< Window Manager. */
  size_t window_id;       /**< ID of the window. */
} frame_callback_args;

#endif // GLPS_COMMON_H
