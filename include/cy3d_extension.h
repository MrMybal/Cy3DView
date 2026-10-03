// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Cyberalien
#pragma once
#include "cy3d_plugin.h"
#ifdef __cplusplus
extern "C" {
#endif
#define CY3D_EXTENSION_VERSION 1u
#define CY3D_EXTENSION_ENTRY "Cy3D_GetExtension"
enum {
    CY3D_EXPORT_MATERIALS = 1u,
    CY3D_EXPORT_PBR = 2u,
    CY3D_EXPORT_ANIMATION = 4u,
    CY3D_EXPORT_SKIN = 8u,
    CY3D_EXPORT_COLORS = 16u,
    CY3D_EXPORT_POINTS = 32u,
    CY3D_EXPORT_UV = 64u,
    CY3D_EXPORT_NODES = 128u
};
typedef struct Cy3DExportFormat {
    const char *id, *name, *extension;
    uint32_t capabilities;
} Cy3DExportFormat;
typedef struct Cy3DTool {
    const char *id, *name, *name_fr;
} Cy3DTool;
/* UI callbacks execute on the UI thread only. Never retain context or scene pointers.
   The host disables input while a worker uses tool state. No ImGui/STL objects cross DLLs. */
typedef struct Cy3DToolUI {
    uint32_t struct_size, language; /* 0 English, 1 French */
    void* context;
    void (*text)(void*, const char*);
    void (*slider_int)(void*, const char*, int*, int, int);
    void (*slider_float)(void*, const char*, float*, float, float);
    void (*checkbox)(void*, const char*, int*);
    void (*combo)(void*, const char*, int*, const char* const*, uint32_t);
    void (*uv_preview)(void*, const Cy3DScene*, uint32_t);
} Cy3DToolUI;
typedef struct Cy3DExtension {
    uint32_t api_version, struct_size;
    const char *name, *version;
    const Cy3DExportFormat* formats;
    uint32_t format_count;
    /* Write only in the supplied empty staging directory. Output path is absolute UTF-8.
       All sidecars must use relative references within this directory. Never overwrite source data. */
    int (*export_scene)(const char* format, const Cy3DScene*, const char* path, const Cy3DHost*, char* error,
                        size_t error_size);
    const Cy3DTool* tools;
    uint32_t tool_count;
    void* (*create_tool)(const char* id);
    void (*destroy_tool)(void* state);
    void (*draw_tool)(const char* id, void* state, const Cy3DToolUI*, const Cy3DScene*);
    /* Input is immutable. Return an independent result, valid until release_scene. NULL means no edit.
       Use host's cancellation/progress/budget callbacks. No exceptions may cross this ABI. */
    int (*run_tool)(const char* id, const void* state, const Cy3DScene*, const Cy3DHost*, Cy3DScene** output,
                    char* error, size_t error_size);
    void (*release_scene)(Cy3DScene*);
} Cy3DExtension;
typedef const Cy3DExtension* (*Cy3DGetExtensionFn)(void);
#ifdef __cplusplus
}
#endif
