#ifndef INCLUDED_SHADOW_MAP_H
#define INCLUDED_SHADOW_MAP_H

// return 1.0 if shadowCoord is in shadow with offset in shadow map=off
float shadowProj(vec4 shadowCoord, vec2 off, const float ambient)
{
  float shadow = 1.0;
  if ( shadowCoord.z > -1.0 && shadowCoord.z < 1.0 ) {
    float dist = texture( shadow_map, shadowCoord.xy + off ).r;
    if ( dist > shadowCoord.z ) {
      shadow = ambient;
    }
  }
  return shadow;
}

// return 1.0 if sc is in shadow
float shadowFilterPCF(vec4 sc, const float ambient) {
  ivec2 texDim = textureSize(shadow_map, 0);
  float scale = 1.0;
  float dx = scale * 1.0 / float(texDim.x);
  float dy = scale * 1.0 / float(texDim.y);
  float res = 0.0;
  const int range = 1;
  for (int x = -range; x<= range; x++)
    for (int y = -range; y<=range; y++) {
      res += shadowProj(sc, vec2(dx*x, dy*y), ambient);
    }
  return res/(2*range+1)/(2*range+1);
}

const mat4 shadow_bias_mat = mat4(0.5, 0.0, 0.0, 0.0,
                                  0.0, 0.5, 0.0, 0.0,
                                  0.0, 0.0, 1.0, 0.0,
                                  0.5, 0.5, 0.0, 1.0);

#endif // #ifndef INCLUDED_SHADOW_MAP_H
