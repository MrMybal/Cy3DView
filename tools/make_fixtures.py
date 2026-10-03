# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Cyberalien
"""Small deterministic assets testing import semantics rather than parser implementation."""
import base64
import json
import math
import pathlib
import struct
import zlib
ROOT=pathlib.Path(__file__).resolve().parents[1]
OUT=ROOT/'tests'/'fixtures'
OUT.mkdir(parents=True,exist_ok=True)
obj='''mtllib cube.mtl
o Cube
v -1 -1 -1
v 1 -1 -1
v 1 1 -1
v -1 1 -1
v -1 -1 1
v 1 -1 1
v 1 1 1
v -1 1 1
vt 0 0
vt 1 0
vt 1 1
vt 0 1
usemtl Surface
f 1/1 4/4 3/3 2/2
f 5/1 6/2 7/3 8/4
f 1/1 2/2 6/3 5/4
f 4/1 8/2 7/3 3/4
f 1/1 5/2 8/3 4/4
f 2/1 3/2 7/3 6/4
'''
(OUT/'cube.obj').write_text(obj)
(OUT/'cube.mtl').write_text('newmtl Surface\nKd 1 1 1\nmap_Kd checker.png\n')
def chunk(kind,data):return struct.pack('>I',len(data))+kind+data+struct.pack('>I',zlib.crc32(kind+data)&0xffffffff)
raw=b''.join(b'\0'+b''.join(bytes((38,190,165,255) if (x//8+y//8)%2 else (222,236,238,255)) for x in range(32)) for y in range(32))
png=b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>2I5B',32,32,8,6,0,0,0))+chunk(b'IDAT',zlib.compress(raw))+chunk(b'IEND',b'')
(OUT/'checker.png').write_bytes(png)
(OUT/'triangle.stl').write_text('solid triangle\nfacet normal 0 0 1\nouter loop\nvertex 0 0 0\nvertex 1 0 0\nvertex 0 1 0\nendloop\nendfacet\nendsolid triangle\n')
binary_stl=b'Cy3DView test'.ljust(80,b'\0')+struct.pack('<I',1)+struct.pack('<12fH',0,0,1,0,0,0,1,0,0,0,1,0,0)
(OUT/'binary.stl').write_bytes(binary_stl)
(OUT/'truncated.stl').write_bytes(binary_stl[:-4])
(OUT/'triangle.ply').write_text('ply\nformat ascii 1.0\nelement vertex 3\nproperty float x\nproperty float y\nproperty float z\nelement face 1\nproperty list uchar int vertex_indices\nend_header\n0 0 0\n1 0 0\n0 1 0\n3 0 1 2\n')
(OUT/'broken.obj').write_text('v bananas\nf 100000 200000 300000\n')
(OUT/'unicode').mkdir(exist_ok=True)
(OUT/'unicode'/'épreuve.obj').write_text(obj.replace('mtllib cube.mtl',''),encoding='utf-8')
geometry=struct.pack('<9f',0,0,0,1,0,0,0,1,0)
gltf={'asset':{'version':'2.0'},'scene':0,'scenes':[{'nodes':[0,1]}],'nodes':[{'mesh':0},{'mesh':0,'translation':[4,0,0]}],
    'meshes':[{'primitives':[{'attributes':{'POSITION':0}}]}],
    'buffers':[{'byteLength':len(geometry),'uri':'data:application/octet-stream;base64,'+base64.b64encode(geometry).decode()}],
    'bufferViews':[{'buffer':0,'byteOffset':0,'byteLength':len(geometry)}],
    'accessors':[{'bufferView':0,'componentType':5126,'count':3,'type':'VEC3','min':[0,0,0],'max':[1,1,0]}]}
(OUT/'transformed.gltf').write_text(json.dumps(gltf))
gltf['nodes']=[{'mesh':0}];gltf['scenes'][0]['nodes']=[0]
uv=struct.pack('<6f',0,0,1,0,0,1);binary=geometry+uv+png;binary+=b'\0'*(-len(binary)%4)
gltf['buffers']=[{'byteLength':len(binary)}]
gltf['bufferViews'] += [{'buffer':0,'byteOffset':len(geometry),'byteLength':len(uv)},{'buffer':0,'byteOffset':len(geometry)+len(uv),'byteLength':len(png)}]
gltf['accessors'].append({'bufferView':1,'componentType':5126,'count':3,'type':'VEC2'})
gltf['images']=[{'bufferView':2,'mimeType':'image/png'}];gltf['textures']=[{'source':0}]
gltf['materials']=[{'pbrMetallicRoughness':{'baseColorTexture':{'index':0}}}]
gltf['meshes'][0]['primitives'][0].update({'material':0,'attributes':{'POSITION':0,'TEXCOORD_0':1}})
j=json.dumps(gltf).encode();j+=b' '*(-len(j)%4)
glb=struct.pack('<III',0x46546c67,2,12+8+len(j)+8+len(binary))+struct.pack('<I4s',len(j),b'JSON')+j+struct.pack('<I4s',len(binary),b'BIN\0')+binary
(OUT/'embedded.glb').write_bytes(glb)
samples=ROOT/'samples';samples.mkdir(exist_ok=True)
# Sculptural knot, useful for inspecting normals, orbiting, and wireframe.
lines=['mtllib studio.mtl','o TorusKnot','usemtl Turquoise']
N,M=160,16
def curve(t):return ((2+0.55*math.cos(3*t))*math.cos(2*t),0.55*math.sin(3*t),(2+0.55*math.cos(3*t))*math.sin(2*t))
def sub(a,b):return tuple(x-y for x,y in zip(a,b))
def cross(a,b):return (a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0])
def norm(a):s=math.sqrt(sum(x*x for x in a));return tuple(x/s for x in a)
for i in range(N):
    t=2*math.pi*i/N;p=curve(t);tangent=norm(sub(curve(t+.001),curve(t-.001)));normal=norm(cross(tangent,(0,1,0)));binormal=cross(tangent,normal)
    for j in range(M):
        angle=2*math.pi*j/M;v=tuple(p[k]+.22*(normal[k]*math.cos(angle)+binormal[k]*math.sin(angle)) for k in range(3));lines.append('v '+' '.join(f'{x:.6f}' for x in v))
for i in range(N):
    for j in range(M):
        a=i*M+j+1;b=((i+1)%N)*M+j+1;c=((i+1)%N)*M+(j+1)%M+1;d=i*M+(j+1)%M+1;lines.append(f'f {a} {b} {c} {d}')
(samples/'studio.obj').write_text('\n'.join(lines)+'\n')
(samples/'studio.mtl').write_text('newmtl Turquoise\nKd 0.15 0.78 0.69\n')
print('Fixtures and sample generated')

# A glTF studio with explicit normals, UV0/UV1 and a range of metallic/roughness factors.
positions=[];normals=[];coords=[];coords1=[];indices=[]
W,H=48,24
for y in range(H+1):
    t=math.pi*y/H
    for x in range(W+1):
        p=2*math.pi*x/W
        n=(math.sin(t)*math.cos(p),math.cos(t),math.sin(t)*math.sin(p))
        positions.extend(n);normals.extend(n);coords.extend((x/W,y/H));coords1.extend((x/W*.5+.1,y/H*.5+.2))
for y in range(H):
    for x in range(W):
        a=y*(W+1)+x;b=a+1;c=a+W+1;d=c+1
        if y>0:indices.extend((a,b,c))
        if y<H-1:indices.extend((b,d,c))
parts=[struct.pack('<%df'%len(v),*v) for v in (positions,normals,coords,coords1)]
parts.append(struct.pack('<%dI'%len(indices),*indices))
blob=b''.join(parts);offset=0;views=[]
for part in parts:
    views.append({'buffer':0,'byteOffset':offset,'byteLength':len(part)});offset+=len(part)
accessors=[{'bufferView':i,'componentType':5126,'count':len(positions)//3,'type': 'VEC3' if i<2 else 'VEC2'} for i in range(4)]
accessors[0].update({'min':[-1,-1,-1],'max':[1,1,1]})
accessors.append({'bufferView':4,'componentType':5125,'count':len(indices),'type':'SCALAR'})
pbr={'asset':{'version':'2.0','generator':'Cy3DView procedural PBR studio'},'scene':0,
     'buffers':[{'byteLength':len(blob),'uri':'data:application/octet-stream;base64,'+base64.b64encode(blob).decode()}],
     'bufferViews':views,'accessors':accessors,'meshes':[],'materials':[],'nodes':[],'scenes':[{'nodes':[]}]}
attrs={'POSITION':0,'NORMAL':1,'TEXCOORD_0':2,'TEXCOORD_1':3}
for row,metal in enumerate((0,.5,1)):
    for col,rough in enumerate((.07,.2,.4,.6,.8,1)):
        i=len(pbr['materials'])
        pbr['materials'].append({'name':f'Metal {metal:.1f} / roughness {rough:.2f}',
           'pbrMetallicRoughness':{'baseColorFactor':[.85,.48,.12,1] if row==2 else [.08,.5,.42,1],
                                 'metallicFactor':metal,'roughnessFactor':rough}})
        pbr['meshes'].append({'primitives':[{'attributes':attrs,'indices':4,'material':i}]})
        pbr['nodes'].append({'mesh':i,'translation':[(col-2.5)*2.5,(1-row)*2.5,0]})
        pbr['scenes'][0]['nodes'].append(i)
(samples/'pbr_studio.gltf').write_text(json.dumps(pbr,separators=(',',':')))

def make_png(pixel):
    data=b''.join(b'\0'+b''.join(bytes(pixel(x,y)) for x in range(16)) for y in range(16))
    return b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>2I5B',16,16,8,6,0,0,0))+chunk(b'IDAT',zlib.compress(data))+chunk(b'IEND',b'')
import copy
mapped=copy.deepcopy(pbr);mapped['meshes']=mapped['meshes'][:1];mapped['nodes']=[{'mesh':0}];mapped['scenes']=[{'nodes':[0]}]
pixels=[lambda x,y:(180,120,70,255 if x<8 else 50),lambda x,y:(40,90,210,255),
        lambda x,y:(190 if x<8 else 66,128,235,255),lambda x,y:(120,20,10,255)]
mapped['images']=[{'uri':'data:image/png;base64,'+base64.b64encode(make_png(p)).decode()} for p in pixels]
mapped['textures']=[{'source':i,'sampler':0} for i in range(4)]
mapped['samplers']=[{'wrapS':33071,'wrapT':33648}]
mapped['extensionsUsed']=['KHR_texture_transform','KHR_materials_emissive_strength']
transform={'offset':[.15,.25],'scale':[.8,.6],'rotation':.3}
mapped['materials']=[{'name':'Mapped PBR test','doubleSided':True,'alphaMode':'MASK','alphaCutoff':.4,
    'pbrMetallicRoughness':{'baseColorFactor':[.9,.7,.5,1],'metallicFactor':.6,'roughnessFactor':.8,
        'baseColorTexture':{'index':0,'texCoord':1,'extensions':{'KHR_texture_transform':transform}},
        'metallicRoughnessTexture':{'index':1}},
    'normalTexture':{'index':2,'scale':.4},'occlusionTexture':{'index':1,'strength':.3,'texCoord':1},
    'emissiveFactor':[.1,.2,.3],'emissiveTexture':{'index':3},
    'extensions':{'KHR_materials_emissive_strength':{'emissiveStrength':2}}}]
(OUT/'pbr_mapped.gltf').write_text(json.dumps(mapped,separators=(',',':')))
print('PBR studio and texture fixture generated')
