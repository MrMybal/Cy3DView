# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Cyberalien
"""Run with an installed Blender in background to generate independent adapter fixtures."""
import pathlib
import re
import bpy
ROOT=pathlib.Path(__file__).resolve().parents[1]
OUT=ROOT/'tests/fixtures'
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=str(OUT/'skinned.glb'))
bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'recent.blend'),check_existing=False,compress=False)
# Blender can serialize file-browser directories from its startup layout.
# These are UI metadata in this synthetic fixture, not model dependencies.
file=OUT/'recent.blend'
data=file.read_bytes()
if data[:7]!=b'BLENDER':raise RuntimeError('Expected uncompressed Blender fixture')
pattern=rb'[A-Za-z]:[\\/]+Users[\\/]+[^\x00\r\n]*\x00'
data=re.sub(pattern,lambda match:b'//'.ljust(len(match[0]),b'\0'),data)
file.write_bytes(data)
bpy.ops.wm.usd_export(filepath=str(OUT/'recent.usdc'),export_animation=False,export_materials=True)
print('Recent .blend and binary USDC generated with Blender',bpy.app.version_string)
