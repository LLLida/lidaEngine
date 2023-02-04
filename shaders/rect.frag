#version 450
#extension GL_GOOGLE_include_directive : enable

layout (location = 0) in vec2 uv;
layout (location = 0) out vec4 outColor;

layout (set = 0, binding = 0) uniform sampler2D in_attachment;

vec3 tone_map(vec3 hdr, float exposure) {
  const float gamma = 2.2;
  vec3 mapped = (1.0) - exp(-hdr * exposure);
  mapped = pow(mapped, vec3(1.0 / gamma));
  return mapped;
}

void main() {
  // vec3 hdr = texture(in_attachment, uv).xyz;
  // const float exposure = 0.05;
  // outColor = vec4(tone_map(hdr, exposure), 1.0);
  outColor = texture(in_attachment, uv);
}
