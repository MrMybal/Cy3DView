# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Cyberalien
"""Small reproducible compatibility patches for the pinned Assimp revision."""
import pathlib
import sys
ROOT=pathlib.Path(__file__).resolve().parents[1]
SOURCE=pathlib.Path(sys.argv[1]) if len(sys.argv)>1 else ROOT/'third_party/assimp'
p=SOURCE/'code/CMakeLists.txt'
text=p.read_text(); original=text
text=text.replace('set(Tinyusdz_SRC_ABSPATH "${Tinyusdz_REPO_ABSPATH}/tinyusdz_repo-src/src")',
                  'set(Tinyusdz_SRC_ABSPATH "${tinyusdz_repo_SOURCE_DIR}/src")')
if text!=original:p.write_text(text)
# Assimp 6.0.2 has interpolation flags, but the glTF reader does not populate them.
# Preserve STEP and warn explicitly in our adapter for CUBICSPLINE (tangents discarded upstream).
p=SOURCE/'code/AssetLib/glTF2/glTF2Importer.cpp'
text=p.read_text(); original=text
for name in ('Position','Rotation','Scaling'):
    sampler={'Position':'translation','Rotation':'rotation','Scaling':'scale'}[name]
    marker=f'anim->m{name}Keys[i].mTime = times[i] * kMillisecondsFromSeconds;'
    patch=marker+f'\n                anim->m{name}Keys[i].mInterpolation = samplers.{sampler}->interpolation == Interpolation_STEP ? aiAnimInterpolation_Step : (samplers.{sampler}->interpolation == Interpolation_CUBICSPLINE ? aiAnimInterpolation_Cubic_Spline : aiAnimInterpolation_Linear);'
    if patch not in text:
        if marker not in text:raise RuntimeError(f'Pinned Assimp interpolation marker changed: {name}')
        text=text.replace(marker,patch)
if text!=original:p.write_text(text)
print('Pinned Assimp compatibility patches applied')
