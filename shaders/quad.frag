#version 450
// a very complicated shader

layout (location = 0) in vec4 color;

layout (location = 0) out vec4 out_color;

void main() {
  out_color = color;
}
