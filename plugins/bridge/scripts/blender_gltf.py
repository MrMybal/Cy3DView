# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Cyberalien
"""Repair Blender texture references that have no exported UV coordinates.

Only the generated GLB is changed. Valid texture references and the binary data
are preserved; missing UVs use the source material's constant PBR values.
"""
import json
import pathlib
import shutil
import struct


def normalize_document(document, fallbacks):
    uses = {}
    for mesh in document.get('meshes', []):
        for primitive in mesh.get('primitives', []):
            if 'material' in primitive:
                uses.setdefault(primitive['material'], []).append(primitive.get('attributes', {}))
    removed = remapped = 0
    for index, material in enumerate(document.get('materials', [])):
        fallback = fallbacks.get(material.get('name', ''), {})

        def visit(obj):
            nonlocal removed, remapped
            if not isinstance(obj, dict):
                return
            for key, value in list(obj.items()):
                if isinstance(value, dict) and key.endswith('Texture'):
                    transform = value.get('extensions', {}).get('KHR_texture_transform', {})
                    invalid = value.get('texCoord') == -1 or transform.get('texCoord') == -1
                    if invalid:
                        primitives = uses.get(index, [])
                        if primitives and all('TEXCOORD_0' in attrs for attrs in primitives):
                            if value.get('texCoord') == -1:
                                value['texCoord'] = 0
                            if transform.get('texCoord') == -1:
                                transform['texCoord'] = 0
                            remapped += 1
                        else:
                            del obj[key]
                            if key == 'baseColorTexture':
                                obj['baseColorFactor'] = fallback.get('baseColorFactor', [0.8, 0.8, 0.8, 1])
                            elif key == 'metallicRoughnessTexture':
                                obj['metallicFactor'] = fallback.get('metallicFactor', 0)
                                obj['roughnessFactor'] = fallback.get('roughnessFactor', 0.65)
                            elif key == 'emissiveTexture':
                                obj['emissiveFactor'] = fallback.get('emissiveFactor', [0, 0, 0])
                            removed += 1
                else:
                    visit(value)
        visit(material)
    warnings = []
    if removed:
        warnings.append('Certaines textures ne possedent pas de coordonnees UV exportables. '
                        'Les couleurs et facteurs des materiaux Blender sont utilises a la place.')
    if remapped:
        warnings.append('Des references de textures Blender invalides ont ete rattachees au jeu UV principal.')
    return removed + remapped, warnings


def normalize_glb(filename, fallbacks):
    filename = pathlib.Path(filename)
    with filename.open('rb') as source:
        header = source.read(20)
        if len(header) != 20:
            raise RuntimeError('GLB Blender tronque')
        magic, version, length, json_length, kind = struct.unpack('<4sIII4s', header)
        if magic != b'glTF' or version != 2 or kind != b'JSON' or json_length > 64 * 1024 * 1024:
            raise RuntimeError('GLB Blender invalide')
        if length != filename.stat().st_size or json_length > length - 20:
            raise RuntimeError('Taille GLB Blender invalide')
        document = json.loads(source.read(json_length))
        changed, warnings = normalize_document(document, fallbacks)
        if not changed:
            return warnings
        encoded = json.dumps(document, ensure_ascii=False, separators=(',', ':')).encode('utf-8')
        encoded += b' ' * (-len(encoded) % 4)
        temporary = filename.with_suffix('.normalized.glb')
        with temporary.open('wb') as target:
            target.write(struct.pack('<4sIII4s', b'glTF', 2, length - json_length + len(encoded), len(encoded), b'JSON'))
            target.write(encoded)
            shutil.copyfileobj(source, target, length=1024 * 1024)
    temporary.replace(filename)
    return warnings
