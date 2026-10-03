# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Cyberalien
"""Generate a reproducible binary STL grid, without downloading a model."""
import argparse
import pathlib
import struct
parser=argparse.ArgumentParser()
parser.add_argument('--triangles',type=int,default=200000)
parser.add_argument('--output',type=pathlib.Path,default=pathlib.Path('build/benchmark.stl'))
args=parser.parse_args()
args.output.parent.mkdir(parents=True,exist_ok=True)
with args.output.open('wb') as output:
    output.write(b'Cy3DView benchmark grid'.ljust(80,b'\0')+struct.pack('<I',args.triangles))
    for i in range(args.triangles):
        x,y=i%500,i//500
        output.write(struct.pack('<12fH',0,0,1,x,y,0,x+1,y,0,x,y+1,0,0))
print(f'{args.triangles:,} triangles / {args.output.stat().st_size:,} bytes -> {args.output}')
