# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Cyberalien
"""Deterministic animation, point, Gaussian and USD fixtures."""
import json,math,pathlib,struct,zipfile
ROOT=pathlib.Path(__file__).resolve().parents[1];OUT=ROOT/'tests/fixtures';SAMPLES=ROOT/'samples'
OUT.mkdir(exist_ok=True)
blob=bytearray();views=[];accessors=[]
def accessor(values,components,kind,component=5126,minimum=None,maximum=None):
    blob.extend(b'\0'*(-len(blob)%4));offset=len(blob);fmt={5126:'f',5123:'H',5125:'I'}[component]
    part=struct.pack('<'+str(len(values))+fmt,*values);blob.extend(part);view=len(views);views.append({'buffer':0,'byteOffset':offset,'byteLength':len(part)})
    value={'bufferView':view,'componentType':component,'count':len(values)//components,'type':kind}
    if minimum is not None:value['min']=minimum
    if maximum is not None:value['max']=maximum
    index=len(accessors);accessors.append(value);return index
p=accessor([0,0,0,1,0,0,0,2,0,1,2,0],3,'VEC3',minimum=[0,0,0],maximum=[1,2,0]);n=accessor([0,0,1]*4,3,'VEC3')
j=accessor([0,0,0,0]*2+[1,0,0,0]*2,4,'VEC4',5123);w=accessor([1,0,0,0]*4,4,'VEC4')
i=accessor([0,1,2,2,1,3],1,'SCALAR',5123)
identity=[1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1];inverse=identity.copy();inverse[13]=-1
ib=accessor(identity+inverse,16,'MAT4');t=accessor([0,1],1,'SCALAR',minimum=[0],maximum=[1])
q=accessor([0,0,0,1,0,0,math.sin(math.pi/4),math.cos(math.pi/4)],4,'VEC4')
translation=accessor([0,0,0,1,0,0],3,'VEC3');step=accessor([0,0,0,2,0,0],3,'VEC3')
doc={'asset':{'version':'2.0'},'scene':0,'scenes':[{'nodes':[0]}],
     'nodes':[{'name':'MeshRoot','mesh':0,'skin':0,'children':[1]},{'name':'Base','children':[2]},{'name':'Tip','translation':[0,1,0]}],
     'meshes':[{'primitives':[{'attributes':{'POSITION':p,'NORMAL':n,'JOINTS_0':j,'WEIGHTS_0':w},'indices':i,'material':0}]}],
     'materials':[{'doubleSided':True,'pbrMetallicRoughness':{'baseColorFactor':[.12,.7,.48,1],'metallicFactor':.3,'roughnessFactor':.25}}],
     'skins':[{'joints':[1,2],'inverseBindMatrices':ib,'skeleton':1}],
     'animations':[{'name':'Bend','samplers':[{'input':t,'output':q,'interpolation':'LINEAR'}],'channels':[{'sampler':0,'target':{'node':2,'path':'rotation'}}]},
                   {'name':'Move','samplers':[{'input':t,'output':translation,'interpolation':'LINEAR'}],'channels':[{'sampler':0,'target':{'node':0,'path':'translation'}}]},
                   {'name':'Step','samplers':[{'input':t,'output':step,'interpolation':'STEP'}],'channels':[{'sampler':0,'target':{'node':0,'path':'translation'}}]}],
     'buffers':[{'byteLength':len(blob)}],'bufferViews':views,'accessors':accessors}
encoded=json.dumps(doc,separators=(',',':')).encode();encoded+=b' '*(-len(encoded)%4);blob.extend(b'\0'*(-len(blob)%4))
glb=struct.pack('<III',0x46546c67,2,28+len(encoded)+len(blob))+struct.pack('<I4s',len(encoded),b'JSON')+encoded+struct.pack('<I4s',len(blob),b'BIN\0')+blob
(OUT/'skinned.glb').write_bytes(glb);(SAMPLES/'animated_flag.glb').write_bytes(glb)
rows=[]
for y in range(28):
    for x in range(36):
        a=x/35;b=y/27;rows.append((3*(a-.5),2*(b-.5),.25*math.sin(a*math.pi*4)*math.cos(b*math.pi*3),int(60+180*a),int(70+160*b),170))
header='ply\nformat {format} 1.0\nelement vertex '+str(len(rows))+'\nproperty float x\nproperty float y\nproperty float z\nproperty uchar red\nproperty uchar green\nproperty uchar blue\nend_header\n'
(OUT/'cloud_ascii.ply').write_text(header.format(format='ascii')+''.join(' '.join(map(str,r))+'\n' for r in rows))
for endian,name in (('<','little'),('>','big')):
    data=header.format(format='binary_'+name+'_endian').encode()+b''.join(struct.pack(endian+'3f3B',*r) for r in rows)
    (OUT/f'cloud_{name}.ply').write_bytes(data)
(SAMPLES/'colored_cloud.ply').write_bytes((OUT/'cloud_little.ply').read_bytes())
(OUT/'cloud.xyz').write_text(''.join(' '.join(map(str,r))+'\n' for r in rows))
(OUT/'cloud.pts').write_text(str(len(rows))+'\n'+''.join(' '.join(map(str,(*r[:3],.5,*r[3:])))+'\n' for r in rows))
(OUT/'bad_cloud.ply').write_bytes((OUT/'cloud_little.ply').read_bytes()[:-8])
(OUT/'float_color.ply').write_text('ply\nformat ascii 1.0\nelement vertex 1\nproperty float z\nproperty float blue\nproperty float x\nproperty float red\nproperty float y\nproperty float green\nproperty uchar alpha\nend_header\n+0 0.25 +1 0.5 +2 0.75 128\n')
splats=bytearray()
for x,y,z,r,g,b in rows:
    splats.extend(struct.pack('<6f8B',x,y,z,.045,.08,.025,r,g,b,230,255,128,128,128))
(OUT/'cloud.splat').write_bytes(splats);(SAMPLES/'gaussian_cloud.splat').write_bytes(splats)
near=struct.pack('<6f8B',0,0,.2,.3,.04,.04,255,0,0,180,255,128,128,128)
far=struct.pack('<6f8B',0,0,-.2,.3,.04,.04,0,0,255,180,255,128,128,128)
(OUT/'sorted_a.splat').write_bytes(near+far)
(OUT/'sorted_b.splat').write_bytes(far+near)
gauss_header='ply\nformat binary_little_endian 1.0\nelement vertex '+str(len(rows))+'\n'+''.join('property float '+p+'\n' for p in ('x','y','z','f_dc_0','f_dc_1','f_dc_2','opacity','scale_0','scale_1','scale_2','rot_0','rot_1','rot_2','rot_3'))+'end_header\n'
gauss=gauss_header.encode()+b''.join(struct.pack('<14f',x,y,z,*((c/255-.5)/.28209479177387814 for c in (r,g,b)),2.2,math.log(.045),math.log(.08),math.log(.025),1,0,0,0) for x,y,z,r,g,b in rows)
(OUT/'gaussian.ply').write_bytes(gauss)
usd='''#usda 1.0
(
    defaultPrim = "World"
    metersPerUnit = 1
    upAxis = "Y"
)
def Xform "World" {
    double3 xformOp:translate = (2, 0, 0)
    uniform token[] xformOpOrder = ["xformOp:translate"]
    def Mesh "Quad" {
        point3f[] points = [(-1,-1,0),(1,-1,0),(1,1,0),(-1,1,0)]
        int[] faceVertexCounts = [4]
        int[] faceVertexIndices = [0,1,2,3]
        uniform token subdivisionScheme = "none"
        color3f[] primvars:displayColor = [(0.1, 0.6, 0.8)]
    }
}
'''
(OUT/'quad.usda').write_text(usd);(OUT/'quad.usd').write_text(usd);(SAMPLES/'usd_scene.usda').write_text(usd)
(OUT/'unicode').mkdir(exist_ok=True)
(OUT/'unicode'/'épreuve.usda').write_text(usd)
with zipfile.ZipFile(OUT/'quad.usdz','w',compression=zipfile.ZIP_STORED) as archive:
    info=zipfile.ZipInfo('quad.usda');offset=30+len(info.filename);padding=(-offset)%64
    if padding<4:padding+=64
    info.extra=struct.pack('<HH',0x1986,padding-4)+b'\0'*(padding-4);archive.writestr(info,usd)
print('Animation, clouds, splats, USDA and aligned USDZ generated')
