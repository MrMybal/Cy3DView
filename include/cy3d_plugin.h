// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Cyberalien
#pragma once
#include <stddef.h>
#include <stdint.h>
#ifdef _WIN32
#define CY3D_EXPORT __declspec(dllexport)
#else
#define CY3D_EXPORT __attribute__((visibility("default")))
#endif
#ifdef __cplusplus
extern "C" {
#endif
#define CY3D_API_VERSION 3u
#define CY3D_PLUGIN_ENTRY "Cy3D_GetPlugin"
typedef struct Cy3DVertex {
    float position[3], normal[3], uv[2], uv1[2];
} Cy3DVertex;
enum { CY3D_TRIANGLES, CY3D_POINTS, CY3D_SPLATS };
typedef struct Cy3DSkinVertex {
    uint32_t joints[4];
    float weights[4];
} Cy3DSkinVertex;
typedef struct Cy3DBone {
    uint32_t node;
    float inverse_bind[16];
} Cy3DBone;
typedef struct Cy3DSplat {
    float scale[3], rotation[4];
} Cy3DSplat; /* quaternion XYZW */
typedef struct Cy3DMesh {
    const char* name;
    const Cy3DVertex* vertices;
    const uint32_t* indices;
    uint32_t vertex_count, index_count, material;
    float center[3]; /* local-space bounds center, for transparent instance sorting */
    uint32_t topology;
    const float* colors; /* optional linear RGBA per vertex */
    const Cy3DSkinVertex* skin;
    const Cy3DBone* bones;
    uint32_t bone_count;
    const Cy3DSplat* splats;
} Cy3DMesh;
typedef struct Cy3DInstance {
    uint32_t mesh;
    float transform[16];
    int32_t node; /* -1 static, otherwise scene node index */
} Cy3DInstance;
typedef struct Cy3DNode {
    int32_t parent;
    float transform[16], translation[3], rotation[4], scale[3];
    uint32_t bone;
} Cy3DNode;
typedef struct Cy3DKey {
    double time;
    float value[4];
    uint32_t step;
} Cy3DKey;
typedef struct Cy3DTrack {
    uint32_t node;
    const Cy3DKey *positions, *rotations, *scales;
    uint32_t position_count, rotation_count, scale_count;
} Cy3DTrack;
typedef struct Cy3DAnimation {
    const char* name;
    double duration;
    const Cy3DTrack* tracks;
    uint32_t track_count;
} Cy3DAnimation;
enum {
    CY3D_BASE_COLOR,
    CY3D_METALLIC,
    CY3D_ROUGHNESS,
    CY3D_NORMAL,
    CY3D_OCCLUSION,
    CY3D_EMISSIVE,
    CY3D_MAP_COUNT
};
enum { CY3D_OPAQUE, CY3D_MASK, CY3D_BLEND };
enum { CY3D_REPEAT, CY3D_CLAMP, CY3D_MIRROR };
typedef struct Cy3DMap {
    int32_t texture; /* -1 = none; image index otherwise */
    uint32_t uv_set, channel, wrap_u, wrap_v;
    float transform[9]; /* column-major UV matrix, applied before the V flip */
} Cy3DMap;
typedef struct Cy3DMaterial {
    const char* name;
    float color[4]; /* linear RGBA factors */
    float metallic, roughness, emissive[3], normal_scale, occlusion_strength, alpha_cutoff;
    uint32_t alpha_mode, double_sided, unlit;
    Cy3DMap maps[CY3D_MAP_COUNT];
} Cy3DMaterial;
/* Zero initialization alone would bind image 0 and produce a smooth metal.
   Always start from these defaults, then override the format's properties. */
static inline Cy3DMaterial Cy3D_DefaultMaterial(void) {
#ifdef __cplusplus
    Cy3DMaterial m = {};
#else
    Cy3DMaterial m = {0};
#endif
    uint32_t i;
    m.color[0] = m.color[1] = m.color[2] = 0.72f;
    m.color[3] = 1;
    m.roughness = 0.65f;
    m.normal_scale = m.occlusion_strength = 1;
    m.alpha_cutoff = 0.5f;
    m.double_sided = 1;
    for (i = 0; i < CY3D_MAP_COUNT; ++i) {
        m.maps[i].texture = -1;
        m.maps[i].transform[0] = m.maps[i].transform[4] = m.maps[i].transform[8] = 1;
    }
    return m;
}
typedef struct Cy3DTexture {
    const char* path; /* UTF-8 absolute external path, or NULL for embedded */
    const uint8_t* bytes;
    uint64_t byte_count;
    uint32_t width, height; /* both zero = encoded; otherwise RGBA8 */
} Cy3DTexture;
typedef struct Cy3DScene {
    uint32_t struct_size;
    const Cy3DMesh* meshes;
    const Cy3DInstance* instances;
    const Cy3DMaterial* materials;
    const Cy3DTexture* textures;
    uint32_t mesh_count, instance_count, material_count, texture_count;
    float bounds_min[3], bounds_max[3];
    uint64_t memory_bytes;
    uint32_t animation_count;
    void* owner;                     /* plugin-private; never dereference in the host */
    const char* const* dependencies; /* absolute UTF-8 files opened during import (MTL, BIN...) */
    uint32_t dependency_count;
    const Cy3DNode* nodes;
    uint32_t node_count;
    const Cy3DAnimation* animations;
    const char* const* warnings;
    uint32_t warning_count;
} Cy3DScene;
typedef struct Cy3DHost {
    uint32_t api_version, struct_size;
    void* context;
    int (*is_cancelled)(void* context);
    void (*progress)(void* context, float fraction);
    uint64_t max_output_bytes;
} Cy3DHost;
typedef struct Cy3DPlugin {
    uint32_t api_version, struct_size;
    const char* name;
    const char* version;
    const char* extensions; /* lower case, no dot, separated by semicolons */
    /* Each invocation is independent and may run concurrently. No exceptions cross this ABI.
       On success, output belongs to plugin until release(). All strings are UTF-8.
       Return 0 success, -1 error, -2 cancelled, -3 unsupported subset (host tries next plugin).
       error is always NUL terminated. */
    int (*load)(const char* path, const Cy3DHost* host, Cy3DScene** output, char* error, size_t error_size);
    void (*release)(Cy3DScene* scene);
} Cy3DPlugin;
typedef const Cy3DPlugin* (*Cy3DGetPluginFn)(void);
#ifdef __cplusplus
}
#endif
