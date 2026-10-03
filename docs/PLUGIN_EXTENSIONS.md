# Export and tool plugins

Cy3DView 0.6 adds extension ABI **1**, defined in `sdk/cy3d_extension.h`
(or `include/cy3d_extension.h` in the source tree). Import ABI **3** remains
unchanged. A module can export either or both entry points.

The host reads small UTF-8 `.cy3dx` manifests at startup. Libraries are loaded
only when an export or tool is selected; exporters and xatlas add no work to
ordinary model opening. Native modules run in the application process and must
be trusted. The API does not sandbox them.

## Manifest

Put the manifest and library beside the other files in `plugins/`. The first
line names a sibling library, without directory components. Subsequent lines
declare actions; IDs use lowercase letters, digits, underscores or hyphens.

```text
my_extension.dll
export|myformat|foo|65|Example mesh format
tool|mytool|editor|Example editor|Éditeur exemple
```

Use the platform's actual module filename (`.dll`, `.so`, or `.dylib`). Each
action has five fields, separated by `|`:

- Export: `export|id|extension_without_dot|capability_flags_decimal|English_name`
  with an optional sixth field for its French name.
- Tool: `tool|id|category_reserved|English_name|French_name`.

Capabilities describe retained materials, PBR, animation, skin, colors, points,
UVs and nodes. They drive the export dialog's notices; declare them honestly.
The loaded descriptor must contain the selected action. File extension alone
does not prove that a format supports every variant or feature.

## C entry point and ownership

Export `Cy3D_GetExtension`, returning a static `Cy3DExtension`. Set
`api_version=CY3D_EXTENSION_VERSION` and `struct_size=sizeof(Cy3DExtension)`.
Return 0 for success, -1 for failure and -2 for cancellation. Terminate error
messages with NUL. Catch all exceptions inside the module.

Geometry, instances, materials, nodes and animation use the import scene layout.
Input is immutable and remains valid until the operation returns. Images are
decoded, embedded RGBA8; paths and dependency lists are removed from the view.
An unreadable image has zero dimensions and no data. Exporters should report a
missing required texture rather than silently losing it.

The host and plugin never exchange STL objects, allocation ownership or ImGui
objects. Return a complete independent scene from `run_tool`, with allocations
owned by the plugin until `release_scene`. `cy3d_scene_copy.hpp` is a plugin-local
copy helper. The module stays loaded while its tool state or result exists.
The host validates edited scenes before uploading them to the GPU.

`create_tool(id)` creates one settings object. `destroy_tool` frees it in the
same module. Exporters need no tool callbacks; tools need no export callbacks.

## Background work and UI

`export_scene` and `run_tool` execute on a worker. Poll `host->is_cancelled`,
report progress through `host->progress`, and respect `max_output_bytes`.
Callbacks are valid only until the operation returns. Never leave worker threads
using the host or input after return. Current output and scene limits are 2 GiB;
this is a budget check, not a process memory sandbox. Algorithms may require
additional temporary memory.

`draw_tool` runs on the main thread, using `Cy3DToolUI` callbacks for text,
integer/float sliders, checkboxes, choices and UV previews. `language` is 0 for
English and 1 for French. Localize labels and use stable IDs where appropriate.
Do not retain UI pointers, create graphics resources on a worker, or mutate the
input scene. While a worker uses settings, the host skips `draw_tool` entirely
and disables tool selection. Generic **Apply to copy** and **Undo** controls
are provided by the host.

This first UI API supports small editor panels. More advanced widgets would
require a later ABI revision; no third-party ImGui binary compatibility is assumed.

## Export destinations

The output path is absolute UTF-8 inside a fresh staging directory. Write the
requested model there, along with relative sidecars such as MTL, BIN or PNG.
Reference sidecars relative to the model, never via staging or source paths.
Create files only inside that directory. Unicode paths must work on all platforms.

The host collects generated files, rejects symbolic links and files beyond its
budget, protects the source model and known dependencies, and lists existing
destination files before replacing them. A confirmed replacement uses backups
and rollback if installation fails. Only generated output names are replaced;
unrelated files remain present. Interrupted staging output is discarded during
normal cleanup; an unclean process termination may leave its temporary folder.
Plugins must cooperate with these rules; native code is not a security boundary.

## Included examples

- `plugins/export/exporter.cpp`: seven actions (GLB, glTF, FBX binary/ASCII,
  OBJ, STL and PLY), with embedded or relative textures and Unicode file I/O.
- `plugins/xatlas/tool.cpp`: a real UV atlas editor with a settings panel,
  preview, cancellation and independent scene results.
- `src/editor_ui.cpp`: the host UI adapter and background operation lifecycle.
- `tools/extension_test.cpp`: export/reimport, PBR, skin, UV, point cloud,
  cancellation, ownership and file protection checks.

```cmake
add_library(my_extension MODULE extension.cpp)
target_include_directories(my_extension PRIVATE /path/to/Cy3DView/include)
set_target_properties(my_extension PROPERTIES PREFIX "")
```

Rebuild for each supported platform. Place the resulting module and its manifest
in `plugins/`, then restart Cy3DView to discover them.
