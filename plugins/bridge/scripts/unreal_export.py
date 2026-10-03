# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Cyberalien
"""Export a mesh from a temporary preview project, without saving any source asset."""
import json
import pathlib
import traceback
import unreal

request=json.loads(pathlib.Path(REQUEST).read_text(encoding='utf-8'))
try:
    registry=unreal.AssetRegistryHelpers.get_asset_registry()
    registry.scan_paths_synchronous([request['package'].rsplit('/',1)[0]],force_rescan=True)
    asset=unreal.load_asset(request['package'])
    if not isinstance(asset,(unreal.StaticMesh,unreal.SkeletalMesh)):
        raise RuntimeError('Cet .uasset ne contient pas un StaticMesh ou SkeletalMesh chargeable. Les packages cuits et classes de jeu demandent leur projet/version Unreal.')
    options=unreal.GLTFExportOptions()
    options.export_uniform_scale=.01
    options.export_vertex_colors=True
    options.export_vertex_skin_weights=True
    options.bake_material_inputs=unreal.GLTFMaterialBakeMode.DISABLED
    result=unreal.GLTFExporter.export_to_gltf(asset,request['target'],options,set())
    if result is None or not pathlib.Path(request['target']).is_file():raise RuntimeError('Echec de la conversion Unreal vers GLB')
    dependencies={request['source']}
    pending=[request['package']];seen=set()
    dependency_options=unreal.AssetRegistryDependencyOptions(include_soft_package_references=True,include_hard_package_references=True)
    while pending:
        package=pending.pop()
        if package in seen:continue
        seen.add(package)
        for dependency in registry.get_dependencies(package,dependency_options):
            name=str(dependency);pending.append(name)
            for prefix,directory in request['roots'].items():
                if name.startswith(prefix):
                    stem=pathlib.Path(directory)/name[len(prefix):]
                    for extension in ('.uasset','.uexp','.ubulk'):
                        file=stem.with_suffix(extension)
                        if file.is_file():dependencies.add(str(file))
    pathlib.Path(request['dependencies']).write_text(json.dumps(sorted(dependencies)),encoding='utf-8')
except Exception as error:
    pathlib.Path(request['error']).write_text(str(error),encoding='utf-8')
    traceback.print_exc()
    raise
