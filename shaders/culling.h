#ifndef GLSL_CULLING_H
#define GLSL_CULLING_H

// stride: 48 bytes
struct Draw_Data {
  float half_size_x;
  float half_size_y;
  float half_size_z;
  uint first_vertex;
  uint first_instance;
  uint vertex_count0;
  uint vertex_count1;
  uint vertex_count2;
  uint vertex_count3;
  uint vertex_count4;
  uint vertex_count5;
  uint cull_mask;
};

// stride: 32 bytes
struct Transform {
  quat rotation;
  vec3 position;
  float scale;
};

// stride: 32 bytes
struct Draw_Command {
  uint index_count;
  uint instance_count;
  uint first_index;
  uint vertex_offset;
  uint first_instance;
};

// stride: 32 bytes
struct Vertex_Count {
  uint count0;
  uint count1;
  uint count2;
  uint count3;
  uint count4;
  uint debug_data1;
  uint debug_data2;
  float debug_data3;
};

// stride: 16 bytes
struct Draw_Count {
  uint count;
};

vec3 transform_point(in vec3 pos, in Transform transform, in vec3 box[3])
{
  vec3 basis[3];
  basis[0] = box[0] * pos.x * transform.scale;
  basis[1] = box[1] * pos.y * transform.scale;
  basis[2] = box[2] * pos.z * transform.scale;
  vec3 point;
  point.x = basis[0].x + basis[1].x + basis[2].x + transform.position.x;
  point.y = basis[0].y + basis[1].y + basis[2].y + transform.position.y;
  point.z = basis[0].z + basis[1].z + basis[2].z + transform.position.z;
  return point;
}

// Hoping glslang will optimize this
int occlussion_cull(in Draw_Data d,
                    in Transform transform,
                    in vec3 camera_position,
                    in mat4 projview_matrix,
                    in vec3 box[3],
                    in sampler2D depth_pyramid) {
  float radius = transform.scale * transform.scale *
    max(d.half_size_x*d.half_size_x + d.half_size_y*d.half_size_y,
        max(d.half_size_x*d.half_size_x + d.half_size_z*d.half_size_z,
            d.half_size_z*d.half_size_z + d.half_size_y*d.half_size_y));
  vec3 diff = transform.position - camera_position;
  float dist = dot(diff, diff);
  if (dist <= radius)
    return 0;
  // bounding rect
  vec2 aabb_min = { 1.0, 1.0 };
  vec2 aabb_max = { -1.0, -1.0 };
  float max_depth = 0.0;

  const vec3 muls[8] = {
    { -1.0f, -1.0f, -1.0f },
    { -1.0f, -1.0f,  1.0f },
    { -1.0f,  1.0f, -1.0f },
    { -1.0f,  1.0f,  1.0f },
    {  1.0f, -1.0f, -1.0f },
    {  1.0f, -1.0f,  1.0f },
    {  1.0f,  1.0f, -1.0f },
    {  1.0f,  1.0f,  1.0f },
  };

  for (int i = 0; i < 8; i++) {
    // project OBB point
    vec4 ndc = projview_matrix * vec4(transform_point(muls[i], transform, box), 1.0);
    ndc.xyz /= ndc.w;
    aabb_min.x = min(aabb_min.x, ndc.x);
    aabb_min.y = min(aabb_min.y, ndc.y);
    aabb_max.x = max(aabb_max.x, ndc.x);
    aabb_max.y = max(aabb_max.y, ndc.y);
    max_depth = max(max_depth, ndc.z);
  }
  // convert to UV space
  aabb_min = aabb_min * 0.5 + 0.5;
  aabb_max = aabb_max * 0.5 + 0.5;

  // compute depth level
  vec2 pyramid_size = textureSize(depth_pyramid, 0);
  float width = (aabb_max.x - aabb_min.x) * pyramid_size.x;
  float height = (aabb_max.y - aabb_min.y) * pyramid_size.y;
  // float level = floor(log2(max(width, height)));
  float level = ceil(log2(max(width, height)));

  // do culling
  float mip = level;

  // Texel footprint for the lower (finer-grained) level
  float level_lower = max(mip - 1, 0);
  vec2  scale       = vec2(exp2(-level_lower)) * pyramid_size;
  vec2  a           = floor(aabb_min*scale);
  vec2  b           = ceil(aabb_max*scale);
  vec2  dims        = b - a;

  // Use the lower level if we only touch <= 2 texels in both dimensions
  if (dims.x < 2 && dims.y < 2)
    mip = level_lower;
  float depth = min(min(textureLod(depth_pyramid, aabb_min, mip).r,
                        textureLod(depth_pyramid, vec2(aabb_min.x, aabb_max.y), mip).r),
                    min(textureLod(depth_pyramid, aabb_max, mip).r,
                        textureLod(depth_pyramid, vec2(aabb_max.x, aabb_min.y), mip).r));

  if (depth > max_depth)
    return 1;
  return 0;
}


// Hoping glslang will optimize this
int occlussion_cull_d(in Draw_Data d,
                      in Transform transform,
                      in vec3 camera_position,
                      in mat4 projview_matrix,
                      in vec3 box[3],
                      in sampler2D depth_pyramid,
                      out float mip,
                      out float max_depth) {
  float radius = transform.scale * transform.scale *
    max(d.half_size_x*d.half_size_x + d.half_size_y*d.half_size_y,
        max(d.half_size_x*d.half_size_x + d.half_size_z*d.half_size_z,
            d.half_size_z*d.half_size_z + d.half_size_y*d.half_size_y));
  vec3 diff = transform.position - camera_position;
  float dist = dot(diff, diff);
  if (dist <= radius)
    return 0;
  // bounding rect
  vec2 aabb_min = { 1.0, 1.0 };
  vec2 aabb_max = { -1.0, -1.0 };
  max_depth = 0.0;

  const vec3 muls[8] = {
    { -1.0f, -1.0f, -1.0f },
    { -1.0f, -1.0f, 1.0f },
    { -1.0f, 1.0f, -1.0f },
    { -1.0f, 1.0f, 1.0f },
    { 1.0f, -1.0f, -1.0f },
    { 1.0f, -1.0f, 1.0f },
    { 1.0f, 1.0f, -1.0f },
    { 1.0f, 1.0f, 1.0f },
  };

  for (int i = 0; i < 8; i++) {
    // project OBB point
    vec4 ndc = projview_matrix * vec4(transform_point(muls[i], transform, box), 1.0);
    ndc.xyz /= ndc.w;
    aabb_min.x = min(aabb_min.x, ndc.x);
    aabb_min.y = min(aabb_min.y, ndc.y);
    aabb_max.x = max(aabb_max.x, ndc.x);
    aabb_max.y = max(aabb_max.y, ndc.y);
    max_depth = max(max_depth, ndc.z);
  }
  // convert to UV space
  aabb_min = aabb_min * 0.5 + 0.5;
  aabb_max = aabb_max * 0.5 + 0.5;

  // compute depth level
  vec2 pyramid_size = textureSize(depth_pyramid, 0);
  float width = (aabb_max.x - aabb_min.x) * pyramid_size.x;
  float height = (aabb_max.y - aabb_min.y) * pyramid_size.y;
  // float level = floor(log2(max(width, height)));
  float level = ceil(log2(max(width, height)));

  // do culling
  mip = level;

  // Texel footprint for the lower (finer-grained) level
  float level_lower = max(mip - 1, 0);
  vec2  scale       = vec2(exp2(-level_lower)) * pyramid_size;
  vec2  a           = floor(aabb_min*scale);
  vec2  b           = ceil(aabb_max*scale);
  vec2  dims        = b - a;

  // Use the lower level if we only touch <= 2 texels in both dimensions
  if (dims.x < 2 && dims.y < 2)
    mip = level_lower;
  float depth = min(min(textureLod(depth_pyramid, aabb_min, mip).r,
                        textureLod(depth_pyramid, vec2(aabb_min.x, aabb_max.y), mip).r),
                    min(textureLod(depth_pyramid, aabb_max, mip).r,
                        textureLod(depth_pyramid, vec2(aabb_max.x, aabb_min.y), mip).r));

  if (depth > max_depth)
    return 1;
  max_depth = depth;            /* DEBUG */
  return 0;
}

#endif
