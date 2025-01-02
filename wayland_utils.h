#include "glad.h"
#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include "linmath.h"
#include <freetype2/freetype/freetype.h>
typedef struct Vertex {
    vec2 pos;
    vec3 col;
} Vertex;
void set_shader_src_file(const char *file_path, GLuint shader);

