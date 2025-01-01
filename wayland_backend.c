#define _POSIX_C_SOURCE 200112L
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include <wayland-client.h>
#include "xdg-shell.h"
#include "xdg-decorations.h"
#include "wayland_utils.h"
#include <wayland-egl.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include "glad.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

struct client_state
{
    struct wl_display *wl_display;
    struct wl_registry *wl_registry;
    struct wl_compositor *wl_compositor;
    struct xdg_wm_base *xdg_wm_base;
    struct wl_surface *wl_surface;
    struct xdg_surface *xdg_surface;
    struct xdg_toplevel *xdg_toplevel;
    struct zxdg_decoration_manager_v1 *decoration_manager;

    struct
    {
        EGLDisplay dpy;
        EGLContext ctx;
        EGLConfig conf;
        EGLSurface surface;
    } egl;

    GLuint vao, vbo;
    GLuint shader_program;
};

static struct client_state state = {0};

static void handle_global(void *data, struct wl_registry *registry, uint32_t id, const char *interface, uint32_t version)
{
    struct client_state *s = &state;
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
}

static void handle_global_remove(void *data, struct wl_registry *registry, uint32_t name) {}

static const struct wl_registry_listener registry_listener = {
    .global = handle_global,
    .global_remove = handle_global_remove,
};

static void wayland_create_xdg_surface(void)
{
    state.xdg_surface = xdg_wm_base_get_xdg_surface(state.xdg_wm_base, state.wl_surface);
    assert(state.xdg_surface);

    state.xdg_toplevel = xdg_surface_get_toplevel(state.xdg_surface);
    assert(state.xdg_toplevel);

    xdg_toplevel_set_title(state.xdg_toplevel, "Wayland Desktop OpenGL Example");

    if (state.decoration_manager)
    {

        struct zxdg_toplevel_decoration_v1 *decoration =
            zxdg_decoration_manager_v1_get_toplevel_decoration(state.decoration_manager, state.xdg_toplevel);
        zxdg_toplevel_decoration_v1_set_mode(decoration, ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
    }

    wl_surface_commit(state.wl_surface);
}

static void wayland_init_egl(void)
{
    static const EGLint context_attribs[] = {
        EGL_CONTEXT_MAJOR_VERSION, 4,
        EGL_CONTEXT_MINOR_VERSION, 5,
        EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR,
        EGL_NONE};

    EGLint config_attribs[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
        EGL_NONE};

    EGLint major, minor, n;

    state.egl.dpy = eglGetDisplay((EGLNativeDisplayType)state.wl_display);
    assert(state.egl.dpy);

    if (!eglInitialize(state.egl.dpy, &major, &minor))
    {
        fprintf(stderr, "Failed to initialize EGL\n");
        exit(EXIT_FAILURE);
    }

    printf("EGL initialized successfully (version %d.%d)\n", major, minor);

    if (!eglChooseConfig(state.egl.dpy, config_attribs, &state.egl.conf, 1, &n) || n != 1)
    {
        fprintf(stderr, "Failed to choose a valid EGL config\n");
        exit(EXIT_FAILURE);
    }

    if (!eglBindAPI(EGL_OPENGL_API))
    {
        fprintf(stderr, "Failed to bind OpenGL API\n");
        exit(EXIT_FAILURE);
    }

    state.egl.ctx = eglCreateContext(state.egl.dpy, state.egl.conf, EGL_NO_CONTEXT, context_attribs);
    if (state.egl.ctx == EGL_NO_CONTEXT)
    {
        fprintf(stderr, "Failed to create EGL context\n");
        exit(EXIT_FAILURE);
    }

    struct wl_egl_window *egl_window = wl_egl_window_create(state.wl_surface, 640, 480);
    assert(egl_window);

    state.egl.surface = eglCreateWindowSurface(state.egl.dpy, state.egl.conf, egl_window, NULL);
    if (state.egl.surface == EGL_NO_SURFACE)
    {
        fprintf(stderr, "Failed to create EGL surface\n");
        exit(EXIT_FAILURE);
    }

    if (!eglMakeCurrent(state.egl.dpy, state.egl.surface, state.egl.surface, state.egl.ctx))
    {
        fprintf(stderr, "Failed to make EGL context current\n");
        exit(EXIT_FAILURE);
    }

    if (!gladLoadGLLoader((GLADloadproc)eglGetProcAddress))
    {
        fprintf(stderr, "Failed to initialize GLAD\n");
        exit(EXIT_FAILURE);
    }

    printf("EGL context and surface created successfully\n");
}

static void wayland_cleanup_egl(void)
{
    eglDestroySurface(state.egl.dpy, state.egl.surface);
    eglDestroyContext(state.egl.dpy, state.egl.ctx);
    eglTerminate(state.egl.dpy);
}

static GLuint compile_shader(const char *src, GLenum type)
{
    GLuint shader = glCreateShader(type);
    set_shader_src_file(src, shader);
    glCompileShader(shader);

    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success)
    {
        char info_log[512];
        glGetShaderInfoLog(shader, sizeof(info_log), NULL, info_log);
        fprintf(stderr, "Shader compilation error: %s\n", info_log);
        exit(EXIT_FAILURE);
    }

    return shader;
}

static void wayland_init_gl(void)
{
    GLuint vertex_shader = compile_shader("vertex_shader.glsl", GL_VERTEX_SHADER);
    GLuint fragment_shader = compile_shader("frag_shader.glsl", GL_FRAGMENT_SHADER);

    state.shader_program = glCreateProgram();
    glAttachShader(state.shader_program, vertex_shader);
    glAttachShader(state.shader_program, fragment_shader);
    glLinkProgram(state.shader_program);

    GLint success;
    glGetProgramiv(state.shader_program, GL_LINK_STATUS, &success);
    if (!success)
    {
        char info_log[512];
        glGetProgramInfoLog(state.shader_program, sizeof(info_log), NULL, info_log);
        fprintf(stderr, "Program linking error: %s\n", info_log);
        exit(EXIT_FAILURE);
    }

    glDeleteShader(vertex_shader);
    glDeleteShader(fragment_shader);

    float vertices[] = {
        0.0f, 0.5f, 1.0f, 0.0f, 0.0f,
        -0.5f, -0.5f, 0.0f, 1.0f, 0.0f,
        0.5f, -0.5f, 0.0f, 0.0f, 1.0f};

    glGenVertexArrays(1, &state.vao);
    glGenBuffers(1, &state.vbo);

    glBindVertexArray(state.vao);

    glBindBuffer(GL_ARRAY_BUFFER, state.vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void *)0);
    glEnableVertexAttribArray(0);

    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void *)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
}

static void draw_frame(void)
{
    glClearColor(0.2f, 0.3f, 0.3f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(state.shader_program);
    glBindVertexArray(state.vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
}

static void wayland_setup(void)
{

    state.wl_display = wl_display_connect(NULL);
    assert(state.wl_display);

    state.wl_registry = wl_display_get_registry(state.wl_display);
    wl_registry_add_listener(state.wl_registry, &registry_listener, &state);
    wl_display_roundtrip(state.wl_display);

    if (!state.decoration_manager)
    {
        fprintf(stderr, "xdg-decoration protocol not supported by compositor\n");
    }
    if (!state.wl_compositor || !state.xdg_wm_base)
    {
        fprintf(stderr, "Failed to retrieve Wayland compositor or xdg_wm_base\n");
        exit(EXIT_FAILURE);
    }

    state.wl_surface = wl_compositor_create_surface(state.wl_compositor);
    assert(state.wl_surface);

    wayland_create_xdg_surface();
}

static void wayland_disconnect_display()
{
    wl_display_disconnect(state.wl_display);
}

int main(int argc, char *argv[])
{

    wayland_setup();
    wayland_init_egl();
    wayland_init_gl();

    while (wl_display_dispatch(state.wl_display) != -1)
    {
        draw_frame();
        eglSwapBuffers(state.egl.dpy, state.egl.surface);
    }

    wayland_cleanup_egl();

    return 0;
}
