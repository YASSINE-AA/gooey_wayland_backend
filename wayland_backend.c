/*
 Copyright (c) 2025 Yassine Ahmed Ali

 This program is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
#include <string.h>
#include <sys/mman.h>
#include <assert.h>
#include <time.h>
#include <unistd.h>
#include <wayland-client.h>

// XDG HEADERS
#include "xdg-shell.h"
#include "xdg-decorations.h"
#include "xdg-dialog.h"

#include "wayland_utils.h"
#include <wayland-egl.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include "glad.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

typedef struct
{
    GLuint textureID;
    int width;
    int height;
    int bearingX;
    int bearingY;
    int advance;
} Character;

enum pointer_event_mask
{
    POINTER_EVENT_ENTER = 1 << 0,
    POINTER_EVENT_LEAVE = 1 << 1,
    POINTER_EVENT_MOTION = 1 << 2,
    POINTER_EVENT_BUTTON = 1 << 3,
    POINTER_EVENT_AXIS = 1 << 4,
    POINTER_EVENT_AXIS_SOURCE = 1 << 5,
    POINTER_EVENT_AXIS_STOP = 1 << 6,
    POINTER_EVENT_AXIS_DISCRETE = 1 << 7,

};

struct pointer_event
{
    uint32_t event_mask;
    wl_fixed_t surface_x, surface_y;
    uint32_t button, state;
    uint32_t time;
    uint32_t serial;
    struct
    {
        bool valid;
        wl_fixed_t value;
        int32_t discrete;

    } axes[2];
    uint32_t axis_source;
};

struct GooeyBackendContext
{
    struct wl_display *wl_display;
    struct wl_registry *wl_registry;
    struct wl_compositor *wl_compositor;
    struct wl_seat *wl_seat;
    struct wl_pointer *wl_pointer;

    struct xdg_wm_base *xdg_wm_base;
    struct wl_surface **wl_surfaces;
    struct xdg_surface **xdg_surfaces;
    struct xdg_toplevel **xdg_toplevels;
    struct zxdg_decoration_manager_v1 *decoration_manager;

    GLuint *text_programs;
    GLuint shape_program;
    GLuint text_vbo;
    GLuint shape_vbo;
    // userPtr *user_ptrs;
    GLuint *text_vaos;
    GLuint *shape_vaos;
    mat4x4 projection;
    GLuint text_fragment_shader;
    GLuint text_vertex_shader;
    Character characters[128];
    char font_path[256];
    size_t window_count;
    bool inhibit_reset;
    unsigned int selected_color;

    struct
    {
        EGLDisplay dpy;
        EGLContext ctx;
        EGLConfig conf;
        EGLSurface *surfaces;
        struct wl_egl_window **windows;
    } egl;

    struct pointer_event pointer_event;
};

static struct GooeyBackendContext ctx = {0};

static void xdg_wm_base_ping(void *data, struct xdg_wm_base *xdg_wm_base,
                             uint32_t serial)
{
    xdg_wm_base_pong(xdg_wm_base, serial);
}

static const struct xdg_wm_base_listener xdg_wm_base_listener = {
    .ping = xdg_wm_base_ping,
};

void check_shader_compile(GLuint shader)
{
    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success)
    {
        char infoLog[512];
        glGetShaderInfoLog(shader, 512, NULL, infoLog);
        // LOG_ERROR("ERROR: Shader compilation failed\n%s\n", infoLog);
        exit(EXIT_FAILURE);
    }
}

void check_shader_link(GLuint program)
{
    GLint success;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success)
    {
        char infoLog[512];
        glGetProgramInfoLog(program, 512, NULL, infoLog);
        //  LOG_ERROR("ERROR: Program linking failed\n%s\n", infoLog);
        exit(EXIT_FAILURE);
    }
}

static void
wl_pointer_enter(void *data, struct wl_pointer *wl_pointer,
                 uint32_t serial, struct wl_surface *surface,
                 wl_fixed_t surface_x, wl_fixed_t surface_y)
{
    struct GooeyBackendContext *context = (struct GooeyBackendContext *)data;
    context->pointer_event.event_mask |= POINTER_EVENT_ENTER;
    context->pointer_event.serial = serial;
    context->pointer_event.surface_x = surface_x,
    context->pointer_event.surface_y = surface_y;
}

static void
wl_pointer_leave(void *data, struct wl_pointer *wl_pointer,
                 uint32_t serial, struct wl_surface *surface)
{
    struct GooeyBackendContext *context = (struct GooeyBackendContext *)data;
    context->pointer_event.serial = serial;
    context->pointer_event.event_mask |= POINTER_EVENT_LEAVE;
}

static void
wl_pointer_motion(void *data, struct wl_pointer *wl_pointer, uint32_t time,
                  wl_fixed_t surface_x, wl_fixed_t surface_y)
{
    struct GooeyBackendContext *context = (struct GooeyBackendContext *)data;
    context->pointer_event.event_mask |= POINTER_EVENT_MOTION;
    context->pointer_event.time = time;
    context->pointer_event.surface_x = surface_x,
    context->pointer_event.surface_y = surface_y;
}

static void
wl_pointer_button(void *data, struct wl_pointer *wl_pointer, uint32_t serial,
                  uint32_t time, uint32_t button, uint32_t state)
{
    struct GooeyBackendContext *context = (struct GooeyBackendContext *)data;
    context->pointer_event.event_mask |= POINTER_EVENT_BUTTON;
    context->pointer_event.time = time;
    context->pointer_event.serial = serial;
    context->pointer_event.button = button,
    context->pointer_event.state = state;
}

static void
wl_pointer_axis(void *data, struct wl_pointer *wl_pointer, uint32_t time,
                uint32_t axis, wl_fixed_t value)
{
    struct GooeyBackendContext *context = (struct GooeyBackendContext *)data;
    context->pointer_event.event_mask |= POINTER_EVENT_AXIS;
    context->pointer_event.time = time;
    context->pointer_event.axes[axis].valid = true;
    context->pointer_event.axes[axis].value = value;
}

static void
wl_pointer_axis_source(void *data, struct wl_pointer *wl_pointer,
                       uint32_t axis_source)
{
    struct GooeyBackendContext *context = (struct GooeyBackendContext *)data;
    context->pointer_event.event_mask |= POINTER_EVENT_AXIS_SOURCE;
    context->pointer_event.axis_source = axis_source;
}

static void
wl_pointer_axis_stop(void *data, struct wl_pointer *wl_pointer,
                     uint32_t time, uint32_t axis)
{
    struct GooeyBackendContext *context = (struct GooeyBackendContext *)data;
    context->pointer_event.time = time;
    context->pointer_event.event_mask |= POINTER_EVENT_AXIS_STOP;
    context->pointer_event.axes[axis].valid = true;
}

static void
wl_pointer_axis_discrete(void *data, struct wl_pointer *wl_pointer,
                         uint32_t axis, int32_t discrete)
{
    struct GooeyBackendContext *context = (struct GooeyBackendContext *)data;
    context->pointer_event.event_mask |= POINTER_EVENT_AXIS_DISCRETE;
    context->pointer_event.axes[axis].valid = true;
    context->pointer_event.axes[axis].discrete = discrete;
}

static void
wl_pointer_frame(void *data, struct wl_pointer *wl_pointer)
{
    struct GooeyBackendContext *context = (struct GooeyBackendContext *)data;
    struct pointer_event *event = &context->pointer_event;
    fprintf(stderr, "pointer frame @ %d: ", event->time);

    if (event->event_mask & POINTER_EVENT_ENTER)
    {
        fprintf(stderr, "entered %f, %f ",
                wl_fixed_to_double(event->surface_x),
                wl_fixed_to_double(event->surface_y));
    }

    if (event->event_mask & POINTER_EVENT_LEAVE)
    {
        fprintf(stderr, "leave");
    }

    if (event->event_mask & POINTER_EVENT_MOTION)
    {
        fprintf(stderr, "motion %f, %f ",
                wl_fixed_to_double(event->surface_x),
                wl_fixed_to_double(event->surface_y));
    }

    if (event->event_mask & POINTER_EVENT_BUTTON)
    {
        char *state = event->state == WL_POINTER_BUTTON_STATE_RELEASED ? "released" : "pressed";
        fprintf(stderr, "button %d %s ", event->button, state);
    }

    uint32_t axis_events = POINTER_EVENT_AXIS | POINTER_EVENT_AXIS_SOURCE | POINTER_EVENT_AXIS_STOP | POINTER_EVENT_AXIS_DISCRETE;
    char *axis_name[2] = {
        [WL_POINTER_AXIS_VERTICAL_SCROLL] = "vertical",
        [WL_POINTER_AXIS_HORIZONTAL_SCROLL] = "horizontal",
    };
    char *axis_source[4] = {
        [WL_POINTER_AXIS_SOURCE_WHEEL] = "wheel",
        [WL_POINTER_AXIS_SOURCE_FINGER] = "finger",
        [WL_POINTER_AXIS_SOURCE_CONTINUOUS] = "continuous",
        [WL_POINTER_AXIS_SOURCE_WHEEL_TILT] = "wheel tilt",
    };
    if (event->event_mask & axis_events)
    {
        for (size_t i = 0; i < 2; ++i)
        {
            if (!event->axes[i].valid)
            {
                continue;
            }
            fprintf(stderr, "%s axis ", axis_name[i]);
            if (event->event_mask & POINTER_EVENT_AXIS)
            {
                fprintf(stderr, "value %f ", wl_fixed_to_double(event->axes[i].value));
            }
            if (event->event_mask & POINTER_EVENT_AXIS_DISCRETE)
            {
                fprintf(stderr, "discrete %d ",
                        event->axes[i].discrete);
            }
            if (event->event_mask & POINTER_EVENT_AXIS_SOURCE)
            {
                fprintf(stderr, "via %s ",
                        axis_source[event->axis_source]);
            }
            if (event->event_mask & POINTER_EVENT_AXIS_STOP)
            {
                fprintf(stderr, "(stopped) ");
            }
        }
    }

    fprintf(stderr, "\n");
    memset(event, 0, sizeof(*event));
}

static const struct wl_pointer_listener wl_pointer_listener = {
    .enter = wl_pointer_enter,
    .leave = wl_pointer_leave,
    .motion = wl_pointer_motion,
    .button = wl_pointer_button,
    .axis = wl_pointer_axis,
    .frame = wl_pointer_frame,
    .axis_source = wl_pointer_axis_source,
    .axis_stop = wl_pointer_axis_stop,
    .axis_discrete = wl_pointer_axis_discrete,
};

static void
wl_seat_capabilities(void *data, struct wl_seat *wl_seat, uint32_t capabilities)
{
    struct GooeyBackendContext *context = (struct GooeyBackendContext *)data;

    bool have_pointer = capabilities & WL_SEAT_CAPABILITY_POINTER;

    if (have_pointer && context->wl_pointer == NULL)
    {
        context->wl_pointer = wl_seat_get_pointer(context->wl_seat);
        wl_pointer_add_listener(context->wl_pointer,
                                &wl_pointer_listener, &ctx);
    }
    else if (!have_pointer && context->wl_pointer != NULL)
    {
        wl_pointer_release(context->wl_pointer);
        context->wl_pointer = NULL;
    }
}

static void
wl_seat_name(void *data, struct wl_seat *wl_seat, const char *name)
{
    fprintf(stderr, "seat name: %s\n", name);
}

static const struct wl_seat_listener wl_seat_listener = {
    .capabilities = wl_seat_capabilities,
    .name = wl_seat_name,
};

static void handle_global(void *data, struct wl_registry *registry, uint32_t id, const char *interface, uint32_t version)
{
    struct GooeyBackendContext *s = &ctx;
    s = data;

    if (strcmp(interface, "wl_compositor") == 0)
    {
        s->wl_compositor = wl_registry_bind(registry, id, &wl_compositor_interface, 1);
    }
    else if (strcmp(interface, "xdg_wm_base") == 0)
    {
        s->xdg_wm_base = wl_registry_bind(registry, id, &xdg_wm_base_interface, 1);
    }
    else if (strcmp(interface, "zxdg_decoration_manager_v1") == 0)
    {

        s->decoration_manager = wl_registry_bind(registry, id,
                                                 &zxdg_decoration_manager_v1_interface,
                                                 version);
    }
    else if (strcmp(interface, wl_seat_interface.name) == 0)
    {
        s->wl_seat = wl_registry_bind(s->wl_registry, id, &wl_seat_interface, version);
        wl_seat_add_listener(s->wl_seat, &wl_seat_listener, s);
    }
}

static void handle_global_remove(void *data, struct wl_registry *registry, uint32_t name) {}

static const struct wl_registry_listener registry_listener = {
    .global = handle_global,
    .global_remove = handle_global_remove,
};

static void wayland_init_egl(void)
{

    EGLint config_attribs[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
        EGL_NONE};

    EGLint major, minor, n;

    ctx.egl.dpy = eglGetDisplay((EGLNativeDisplayType)ctx.wl_display);
    assert(ctx.egl.dpy);

    if (!eglInitialize(ctx.egl.dpy, &major, &minor))
    {
        fprintf(stderr, "Failed to initialize EGL\n");
        exit(EXIT_FAILURE);
    }

    printf("EGL initialized successfully (version %d.%d)\n", major, minor);

    if (!eglChooseConfig(ctx.egl.dpy, config_attribs, &ctx.egl.conf, 1, &n) || n != 1)
    {
        fprintf(stderr, "Failed to choose a valid EGL config\n");
        exit(EXIT_FAILURE);
    }

    if (!eglBindAPI(EGL_OPENGL_API))
    {
        fprintf(stderr, "Failed to bind OpenGL API\n");
        exit(EXIT_FAILURE);
    }
    EGLint error = eglGetError();
    if (error != EGL_SUCCESS)
    {
        fprintf(stderr, "EGL error: %x\n", error);
    }
}

static void wayland_cleanup_egl(void)
{
    for (size_t i = 0; i < ctx.window_count; ++i)
        eglDestroySurface(ctx.egl.dpy, ctx.egl.surfaces[i]);
    eglDestroyContext(ctx.egl.dpy, ctx.egl.ctx);
    eglTerminate(ctx.egl.dpy);
}

static void wayland_cleanup_gl(void)
{

    if (ctx.text_vaos)
    {
        free(ctx.text_vaos);
        ctx.text_vaos = NULL;
    }

    if (ctx.shape_vaos)
    {
        free(ctx.shape_vaos);
        ctx.shape_vaos = NULL;
    }

    if (ctx.text_programs)
    {
        free(ctx.text_programs);
        ctx.text_programs = NULL;
    }
}

void wayland_setup_shared()
{

    glGenBuffers(1, &ctx.text_vbo);
    ctx.text_vertex_shader = glCreateShader(GL_VERTEX_SHADER);
    set_shader_src_file("shaders/text/text_vertex.glsl", ctx.text_vertex_shader);

    glCompileShader(ctx.text_vertex_shader);
    check_shader_compile(ctx.text_vertex_shader);

    ctx.text_fragment_shader = glCreateShader(GL_FRAGMENT_SHADER);
    set_shader_src_file("shaders/text/text_fragment.glsl", ctx.text_fragment_shader);
    glCompileShader(ctx.text_fragment_shader);
    check_shader_compile(ctx.text_fragment_shader);

    glGenBuffers(1, &ctx.shape_vbo);

    GLuint shape_vertex_shader = glCreateShader(GL_VERTEX_SHADER);
    set_shader_src_file("shaders/shape/shape_vertex.glsl", shape_vertex_shader);
    glCompileShader(shape_vertex_shader);
    check_shader_compile(shape_vertex_shader);

    GLuint shape_fragment_shader = glCreateShader(GL_FRAGMENT_SHADER);
    set_shader_src_file("shaders/shape/shape_fragment.glsl", shape_fragment_shader);
    glCompileShader(shape_fragment_shader);
    check_shader_compile(shape_fragment_shader);

    ctx.shape_program = glCreateProgram();
    glAttachShader(ctx.shape_program, shape_vertex_shader);
    glAttachShader(ctx.shape_program, shape_fragment_shader);
    glLinkProgram(ctx.shape_program);
    check_shader_link(ctx.shape_program);

    glDeleteShader(shape_vertex_shader);
    glDeleteShader(shape_fragment_shader);
}

void wayland_setup_seperate_vao(int window_id)
{

    ctx.text_programs[window_id] = glCreateProgram();
    glAttachShader(ctx.text_programs[window_id], ctx.text_vertex_shader);
    glAttachShader(ctx.text_programs[window_id], ctx.text_fragment_shader);
    glLinkProgram(ctx.text_programs[window_id]);
    check_shader_link(ctx.text_programs[window_id]);

    GLuint text_vao;
    glGenVertexArrays(1, &text_vao);

    glBindVertexArray(text_vao);
    glBindBuffer(GL_ARRAY_BUFFER, ctx.text_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 6 * 4, NULL, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(float), 0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    ctx.text_vaos[window_id] = text_vao;
    glBindVertexArray(0);

    GLuint shape_vao;
    glGenVertexArrays(1, &shape_vao);
    glBindVertexArray(shape_vao);
    glBindBuffer(GL_ARRAY_BUFFER, ctx.shape_vbo);

    GLint position_attrib = glGetAttribLocation(ctx.shape_program, "pos");
    glEnableVertexAttribArray(position_attrib);
    glVertexAttribPointer(position_attrib, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void *)offsetof(Vertex, pos));

    GLint col_attrib = glGetAttribLocation(ctx.shape_program, "col");
    glEnableVertexAttribArray(col_attrib);
    glVertexAttribPointer(col_attrib, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void *)offsetof(Vertex, col));
    ctx.shape_vaos[window_id] = shape_vao;
}

static void wayland_init_gl(void)
{
    if (!gladLoadGLLoader((GLADloadproc)eglGetProcAddress))
    {
        fprintf(stderr, "Failed to initialize GLAD\n");
        exit(EXIT_FAILURE);
    }
}

static void wayland_make_ctx_current(int window_id)
{
    if (!eglMakeCurrent(ctx.egl.dpy, ctx.egl.surfaces[window_id], ctx.egl.surfaces[window_id], ctx.egl.ctx))
    {
        fprintf(stderr, "Failed to make EGL context current\n");
        exit(EXIT_FAILURE);
    }
}

static void draw_frame(int window_id)
{
    wayland_make_ctx_current(window_id);

    glClearColor(0.2f, 0.3f, 0.3f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glUseProgram(ctx.shape_program);
    glBindVertexArray(ctx.shape_vaos[window_id]);

    static bool initialized = false;
    if (!initialized)
    {
        Vertex vertices[] = {
            {{-0.5f, -0.5f}, {1.0f, 0.0f, 0.0f}},
            {{0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}},
            {{0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}},
            {{-0.5f, -0.5f}, {1.0f, 0.0f, 0.0f}},
            {{0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}},
            {{-0.5f, 0.5f}, {1.0f, 1.0f, 0.0f}}};
        glBindBuffer(GL_ARRAY_BUFFER, ctx.shape_vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
        initialized = true;
    }

    glDrawArrays(GL_TRIANGLES, 0, 6);

    glBindVertexArray(0);

    int width = 640, height = 480;
    wl_surface_damage(ctx.wl_surfaces[window_id], 0, 0, width, height);
    wl_surface_commit(ctx.wl_surfaces[window_id]);
    eglSwapBuffers(ctx.egl.dpy, ctx.egl.surfaces[window_id]);
}

static const struct wl_callback_listener frame_callback_listener;

struct frame_callback_args
{
    struct wl_surface *surface;
    size_t window_id;
};

static void frame_callback_done(void *data, struct wl_callback *callback, uint32_t time)
{
    struct frame_callback_args *args = (struct frame_callback_args *)data;
    struct wl_surface *surface = (struct wl_surface *)args->surface;
    draw_frame(args->window_id);

    if (callback)
    {
        wl_callback_destroy(callback);
    }

    if (surface)
    {
        struct wl_callback *new_callback = wl_surface_frame(surface);
        if (new_callback)
        {
            wl_callback_add_listener(new_callback, &frame_callback_listener, args);
        }
    }
}

static const struct wl_callback_listener frame_callback_listener = {
    .done = frame_callback_done};

void setup_frame_callback(void *data)
{
    if (!data)
        return;

    struct frame_callback_args *args = (struct frame_callback_args *)data;
    struct wl_surface *surface = (struct wl_surface *)args->surface;
    draw_frame(args->window_id);

    struct wl_callback *callback = wl_surface_frame(surface);
    if (callback)
    {
        wl_callback_add_listener(callback, &frame_callback_listener, args);
    }
}

static void wayland_setup(void)
{

    ctx.text_vaos = (GLuint *)malloc(sizeof(GLuint) * 100);
    ctx.shape_vaos = (GLuint *)malloc(sizeof(GLuint) * 100);
    // ctx.user_ptrs = (userPtr *)malloc(sizeof(userPtr) * 100);
    ctx.text_programs = (GLuint *)malloc(sizeof(GLuint) * 100);
    ctx.wl_surfaces = (struct wl_surface **)malloc(sizeof(struct wl_surface *) * 100);
    ctx.xdg_surfaces = (struct xdg_surface **)malloc(sizeof(struct xdg_surface *) * 100);
    ctx.xdg_toplevels = (struct xdg_toplevel **)malloc(sizeof(struct xdg_toplevel *) * 100);
    ctx.egl.surfaces = (EGLSurface *)malloc(sizeof(EGLSurface) * 100);
    ctx.egl.windows = (struct wl_egl_window **)malloc(sizeof(struct wl_egl_window *) * 100);
    ctx.window_count = 0;

    ctx.wl_display = wl_display_connect(NULL);
    assert(ctx.wl_display);

    ctx.wl_registry = wl_display_get_registry(ctx.wl_display);
    wl_registry_add_listener(ctx.wl_registry, &registry_listener, &ctx);
    wl_display_roundtrip(ctx.wl_display);

    if (ctx.xdg_wm_base)
    {
        xdg_wm_base_add_listener(ctx.xdg_wm_base, &xdg_wm_base_listener, NULL);
    }

    if (!ctx.decoration_manager)
    {
        fprintf(stderr, "xdg-decoration protocol not supported by compositor\n");
    }
    if (!ctx.wl_compositor || !ctx.xdg_wm_base)
    {
        fprintf(stderr, "Failed to retrieve Wayland compositor or xdg_wm_base\n");
        exit(EXIT_FAILURE);
    }
}

static void wayland_disconnect_display(void)
{
    wl_display_disconnect(ctx.wl_display);
}

static void xdg_surface_configure(void *data, struct xdg_surface *xdg_surface,
                                  uint32_t serial)
{
    xdg_surface_ack_configure(xdg_surface, serial);
}

static const struct xdg_surface_listener xdg_surface_listener = {
    .configure = xdg_surface_configure,
};

static void wayland_create_ctx(void)
{
    static const EGLint context_attribs[] = {
        EGL_CONTEXT_MAJOR_VERSION, 4,
        EGL_CONTEXT_MINOR_VERSION, 5,
        EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR,
        EGL_NONE};

    ctx.egl.ctx = eglCreateContext(ctx.egl.dpy, ctx.egl.conf, EGL_NO_CONTEXT, context_attribs);
    if (ctx.egl.ctx == EGL_NO_CONTEXT)
    {
        fprintf(stderr, "Failed to create EGL context\n");
        exit(EXIT_FAILURE);
    }
}
static void wayland_create_window(void)
{
    ctx.wl_surfaces[ctx.window_count] = wl_compositor_create_surface(ctx.wl_compositor);
    if (!ctx.wl_surfaces[ctx.window_count])
    {
        fprintf(stderr, "Failed to create wayland surface\n");
        exit(EXIT_FAILURE);
    }

    ctx.xdg_surfaces[ctx.window_count] = xdg_wm_base_get_xdg_surface(
        ctx.xdg_wm_base, ctx.wl_surfaces[ctx.window_count]);
    if (!ctx.xdg_surfaces[ctx.window_count])
    {
        fprintf(stderr, "Failed to create XDG surface\n");
        exit(EXIT_FAILURE);
    }

    if (xdg_surface_add_listener(ctx.xdg_surfaces[ctx.window_count],
                                 &xdg_surface_listener, NULL) == -1)
    {
        fprintf(stderr, "Failed to add XDG surface listener\n");
        exit(EXIT_FAILURE);
    }

    ctx.xdg_toplevels[ctx.window_count] = xdg_surface_get_toplevel(
        ctx.xdg_surfaces[ctx.window_count]);
    if (!ctx.xdg_toplevels[ctx.window_count])
    {
        fprintf(stderr, "Failed to create toplevel\n");
        exit(EXIT_FAILURE);
    }

    xdg_toplevel_set_title(ctx.xdg_toplevels[ctx.window_count],
                           "Wayland Desktop OpenGL Example");

    wl_surface_commit(ctx.wl_surfaces[ctx.window_count]);

    wl_display_roundtrip(ctx.wl_display);

    ctx.egl.windows[ctx.window_count] = wl_egl_window_create(
        ctx.wl_surfaces[ctx.window_count], 640, 480);
    if (!ctx.egl.windows[ctx.window_count])
    {
        fprintf(stderr, "Failed to create EGL window\n");
        exit(EXIT_FAILURE);
    }

    ctx.egl.surfaces[ctx.window_count] = eglCreateWindowSurface(
        ctx.egl.dpy, ctx.egl.conf, ctx.egl.windows[ctx.window_count], NULL);
    if (ctx.egl.surfaces[ctx.window_count] == EGL_NO_SURFACE)
    {
        fprintf(stderr, "Failed to create EGL surface\n");
        exit(EXIT_FAILURE);
    }

    if (ctx.window_count == 0)
    {
        wayland_create_ctx();
        wayland_make_ctx_current(0);
        wayland_init_gl();
        wayland_setup_shared();
    }

    wayland_setup_seperate_vao(ctx.window_count);
    ctx.window_count++;
}
int main(int argc, char *argv[])
{

    wayland_setup();
    wayland_init_egl();
    wayland_create_window();
    wayland_create_window();
    wayland_create_window();
    wayland_create_window();

    struct frame_callback_args frame_args = {0};
    for (size_t i = 0; i < ctx.window_count; ++i)
    {
        frame_args.surface = ctx.wl_surfaces[i];
        frame_args.window_id = i;
        setup_frame_callback(&frame_args);
    }

    while (true)
    {
        if (wl_display_dispatch(ctx.wl_display) == -1)
        {
            fprintf(stderr, "Error in Wayland event dispatch\n");
            break;
        }
    }

    wayland_cleanup_egl();
    wayland_cleanup_gl();
    wayland_disconnect_display();
    return 0;
}
