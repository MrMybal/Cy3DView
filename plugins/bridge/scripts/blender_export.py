# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Cyberalien
"""Read a .blend through Blender's own reader; never execute its embedded scripts."""
import json
import pathlib
import traceback
import sys
import bpy

sys.dont_write_bytecode=True
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from blender_gltf import normalize_glb

request=json.loads(pathlib.Path(REQUEST).read_text(encoding='utf-8'))
try:
    bpy.ops.wm.open_mainfile(filepath=request['source'],load_ui=False,use_scripts=False)
    dependencies={request['source']}
    for library in bpy.data.libraries:
        dependencies.add(bpy.path.abspath(library.filepath))
    for image in bpy.data.images:
        if image.filepath and not image.packed_file:
            dependencies.add(bpy.path.abspath(image.filepath,library=image.library))
    dependencies.add(str(pathlib.Path(__file__).with_name('blender_gltf.py')))
    fallbacks={}
    for material in bpy.data.materials:
        values={'baseColorFactor':list(material.diffuse_color), 'metallicFactor':material.metallic,
                'roughnessFactor':material.roughness}
        if material.node_tree:
            # Prefer the surface shader connected to the active material output.
            outputs=[node for node in material.node_tree.nodes if node.type=='OUTPUT_MATERIAL' and node.is_active_output]
            principled=None
            if outputs and outputs[0].inputs['Surface'].is_linked:
                shader=outputs[0].inputs['Surface'].links[0].from_node
                if shader.type=='BSDF_PRINCIPLED':principled=shader
            if principled:
                values.update(baseColorFactor=list(principled.inputs['Base Color'].default_value),
                              metallicFactor=principled.inputs['Metallic'].default_value,
                              roughnessFactor=principled.inputs['Roughness'].default_value)
                alpha=principled.inputs.get('Alpha')
                if alpha:values['baseColorFactor'][3]=alpha.default_value
        fallbacks[material.name]=values
    kwargs=dict(filepath=request['target'],export_format='GLB',check_existing=False,
                export_animations=True,export_skins=True,export_morph=False,
                export_apply=True,export_texcoords=True,export_normals=True,
                export_draco_mesh_compression_enable=False)
    # Blender's glTF exporter samples evaluated animation curves, including constraints.
    supported=bpy.ops.export_scene.gltf.get_rna_type().properties.keys()
    kwargs={key:value for key,value in kwargs.items() if key in supported}
    result=bpy.ops.export_scene.gltf(**kwargs)
    if 'FINISHED' not in result:raise RuntimeError('Echec de la conversion Blender')
    warnings=normalize_glb(request['target'],fallbacks)
    pathlib.Path(request['warnings']).write_text(json.dumps(warnings),encoding='utf-8')
    pathlib.Path(request['dependencies']).write_text(json.dumps(sorted(dependencies)),encoding='utf-8')
except Exception as error:
    pathlib.Path(request['error']).write_text(str(error),encoding='utf-8')
    traceback.print_exc()
    raise
