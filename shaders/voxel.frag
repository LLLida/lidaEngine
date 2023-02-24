#version 450
#extension GL_GOOGLE_include_directive : enable

layout (location = 0) in vec3 inPosition;
layout (location = 1) in vec3 inNormal;
layout (location = 2) in vec4 inColor;

layout (location = 0) out vec4 outColor;

#include "global.h"

layout (set = 1, binding = 0) uniform sampler2D shadow_map;
#include "shadow.h"

void main() {
  vec4 shadow_coord = shadow_bias_mat * g.light_space_matrix * vec4(inPosition, 1.0);
  // perspective division
  shadow_coord /= shadow_coord.w;

  float depth = texture(shadow_map, shadow_coord.xy).r;
  float shadow = 1.0;
  // we use inverted depth; i.e. objects with smaller depth are farther
  if (depth > shadow_coord.z) shadow = g.sun_ambient;

  // float shadow = texture(shadow_map, (shadow_coord/shadow_coord.w).xy).r;

  float diffuse = max(dot(inNormal, g.sun_dir), 0.0);

  vec3 light = (g.sun_ambient + diffuse) * inColor.xyz;
  light *= shadow;

  outColor = vec4(light, 1.0);
  // outColor = vec4(vec3(shadow), 1.0);
  // outColor = vec4(shadow_coord.xyz, 1.0);
}
