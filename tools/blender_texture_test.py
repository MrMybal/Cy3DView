# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Cyberalien
"""Regression coverage for the Blender texCoord=-1 export failure."""
import copy
import importlib.util
import json
from pathlib import Path
import struct
import tempfile

root=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('blender_gltf',root/'plugins/bridge/scripts/blender_gltf.py')
module=importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
fallback={'Ivory':{'baseColorFactor':[.76,.735,.66,1],'metallicFactor':.12,'roughnessFactor':.28}}
source={'asset':{'version':'2.0'},'meshes':[{'primitives':[{'material':0,'attributes':{'POSITION':0}}]}],
        'materials':[{'name':'Ivory','normalTexture':{'index':0,'texCoord':-1},
                      'pbrMetallicRoughness':{'baseColorTexture':{'index':0,'texCoord':-1},
                                            'metallicRoughnessTexture':{'index':1,'texCoord':-1}}}]}
document=copy.deepcopy(source)
changed,warnings=module.normalize_document(document,fallback)
assert changed==3 and warnings
material=document['materials'][0]
assert 'normalTexture' not in material
assert material['pbrMetallicRoughness']==fallback['Ivory']
with_uv=copy.deepcopy(source)
with_uv['meshes'][0]['primitives'][0]['attributes']['TEXCOORD_0']=1
changed,warnings=module.normalize_document(with_uv,fallback)
assert changed==3 and warnings
assert with_uv['materials'][0]['normalTexture']['texCoord']==0
valid=copy.deepcopy(with_uv)
assert module.normalize_document(valid,fallback)==(0,[]) and valid==with_uv
transform=copy.deepcopy(with_uv)
normal=transform['materials'][0]['normalTexture']
normal['extensions']={'KHR_texture_transform':{'texCoord':-1,'offset':[.1,.2]}}
assert module.normalize_document(transform,fallback)[0]==1
assert normal['extensions']['KHR_texture_transform']=={'texCoord':0,'offset':[.1,.2]}
unrelated=copy.deepcopy(source)
unrelated['materials'][0]['normalTexture']['texCoord']=.5
module.normalize_document(unrelated,fallback)
assert unrelated['materials'][0]['normalTexture']['texCoord']==.5
encoded=json.dumps(source).encode();encoded+=b' '*(-len(encoded)%4)
binary=struct.pack('<I4s',16,b'BIN\0')+bytes(range(16))
original=struct.pack('<4sIII4s',b'glTF',2,20+len(encoded)+len(binary),len(encoded),b'JSON')+encoded+binary
with tempfile.TemporaryDirectory(prefix='Cy3DView-texture-test-') as directory:
    target=Path(directory)/'model.glb';target.write_bytes(original)
    assert module.normalize_glb(target,fallback)
    converted=target.read_bytes()
    magic,version,length,json_length,kind=struct.unpack('<4sIII4s',converted[:20])
    assert magic==b'glTF' and version==2 and kind==b'JSON' and length==len(converted) and json_length%4==0
    assert converted[20+json_length:]==binary
    assert json.loads(converted[20:20+json_length])==document
    assert module.normalize_glb(target,fallback)==[] and target.read_bytes()==converted
print('Missing UV textures, source PBR values, valid UVs and unchanged GLB binary verified')
