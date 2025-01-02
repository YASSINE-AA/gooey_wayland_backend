#version 330 core
layout(location = 0) in vec4 vertex;
out vec2 TexCoords;
uniform mat4 projection;
void main() {
 gl_Position = projection * vec4(vertex.xy, 0.0, 1.0);
 TexCoords = vec2(vertex.z, 1.0-vertex.w);
};