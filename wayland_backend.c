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

#define _POSIX_C_SOURCE 200112L
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

struct GooeyBackendContext
{
    struct wl_display *wl_display;
    struct wl_registry *wl_registry;
    struct wl_compositor *wl_compositor;
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
};

static struct GooeyBackendContext ctx = {0};

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
    else if (strcmp(interface, "xdg_wm_dialog_v1") == 0)
    {
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

static void draw_frame(int window_id)
{
    glClearColor(0.2f, 0.3f, 0.3f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glUseProgram(ctx.shape_program);

    glBindVertexArray(ctx.shape_vaos[window_id]);

    Vertex vertices[] = {
        {{-0.5f, -0.5f}, {1.0f, 0.0f, 0.0f}},
        {{0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}},
        {{0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}},
        {{-0.5f, -0.5f}, {1.0f, 0.0f, 0.0f}},
        {{0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}},
        {{-0.5f, 0.5f}, {1.0f, 1.0f, 0.0f}}};

    glBindBuffer(GL_ARRAY_BUFFER, ctx.shape_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_DYNAMIC_DRAW);

    glDrawArrays(GL_TRIANGLES, 0, 6);

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
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

static void wayland_make_ctx_current(int window_id)
{
    if (!eglMakeCurrent(ctx.egl.dpy, ctx.egl.surfaces[window_id], ctx.egl.surfaces[window_id], ctx.egl.ctx))
    {
        fprintf(stderr, "Failed to make EGL context current\n");
        exit(EXIT_FAILURE);
    }
}

static void wayland_create_window(void)
{

    ctx.wl_surfaces[ctx.window_count] = wl_compositor_create_surface(ctx.wl_compositor);
    assert(ctx.wl_surfaces[ctx.window_count]);

    ctx.xdg_surfaces[ctx.window_count] = xdg_wm_base_get_xdg_surface(ctx.xdg_wm_base, ctx.wl_surfaces[ctx.window_count]);
    assert(ctx.xdg_surfaces[ctx.window_count]);

    ctx.xdg_toplevels[ctx.window_count] = xdg_surface_get_toplevel(ctx.xdg_surfaces[ctx.window_count]);
    assert(ctx.xdg_toplevels[ctx.window_count]);

    xdg_toplevel_set_title(ctx.xdg_toplevels[ctx.window_count], "Wayland Desktop OpenGL Example");

    /*
        if (ctx.decoration_manager)
        {

            struct zxdg_toplevel_decoration_v1 *decoration =
                zxdg_decoration_manager_v1_get_toplevel_decoration(ctx.decoration_manager, ctx.xdg_toplevel);
            zxdg_toplevel_decoration_v1_set_mode(decoration, ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
        }

    */
    ctx.egl.windows[ctx.window_count] = wl_egl_window_create(ctx.wl_surfaces[ctx.window_count], 640, 480);
    assert(ctx.egl.windows[ctx.window_count]);

    ctx.egl.surfaces[ctx.window_count] = eglCreateWindowSurface(ctx.egl.dpy, ctx.egl.conf, ctx.egl.windows[ctx.window_count], NULL);
    if (ctx.egl.surfaces[ctx.window_count] == EGL_NO_SURFACE)
    {
        fprintf(stderr, "Failed to create EGL surface\n");
        exit(EXIT_FAILURE);
    }

    if (ctx.window_count == 0)
    {
        // Create one context for all windows.
        // First window created is the parent.
        // Other windows created later are all children of the parent window.
        wayland_create_ctx();
        wayland_make_ctx_current(0);
        wayland_init_gl();
        wayland_setup_shared();
    }

    wl_surface_commit(ctx.wl_surfaces[ctx.window_count]);

    wayland_setup_seperate_vao(ctx.window_count);

    ctx.window_count++;
}

static void wayland_render()
{
    while (true)
    {
        if (wl_display_dispatch_pending(ctx.wl_display) == -1)
        {
            fprintf(stderr, "Wayland dispatch error\n");
            break;
        }

        for (size_t window_id = 0; window_id < ctx.window_count; ++window_id)
        {
            eglMakeCurrent(ctx.egl.dpy, ctx.egl.surfaces[window_id], ctx.egl.surfaces[window_id], ctx.egl.ctx);

            draw_frame(window_id);

            eglSwapBuffers(ctx.egl.dpy, ctx.egl.surfaces[window_id]);
        }
    }
}

int main(int argc, char *argv[])
{

    wayland_setup();
    wayland_init_egl();
    wayland_create_window();
    wayland_create_window();

    wayland_create_window();

    wayland_create_window();
    wayland_create_window();

    wayland_render();
    wayland_cleanup_egl();
    wayland_cleanup_gl();
    wayland_disconnect_display();
    return 0;
}
