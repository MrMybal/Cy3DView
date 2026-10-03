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

# Export STEP channels using the interpolation flags preserved above.
p=SOURCE/'code/AssetLib/glTF2/glTF2Exporter.cpp'
text=p.read_text(); original=text
for kind in ('Position','Scaling','Rotation'):
    function={'Position':'Translation','Scaling':'Scale','Rotation':'Rotation'}[kind]
    start=text.index('inline void Extract'+function+'Sampler(')
    end=text.index('\n}',start)
    block=text[start:end]
    replacement=f'sampler.interpolation = std::all_of(nodeChannel->m{kind}Keys, nodeChannel->m{kind}Keys + nodeChannel->mNum{kind}Keys, [](const auto& key) {{ return key.mInterpolation == aiAnimInterpolation_Step; }}) ? Interpolation_STEP : Interpolation_LINEAR;'
    block=block.replace('sampler.interpolation = Interpolation_LINEAR;',replacement)
    text=text[:start]+block+text[end:]
if text!=original:p.write_text(text)

# Carry the ABI UV matrices as glTF KHR_texture_transform without baking shared UVs.
p=SOURCE/'code/AssetLib/glTF2/glTF2Exporter.cpp'
text=p.read_text(); original=text
marker='    GetMatTex(mat, texture, prop.texCoord, tt, slot);'
patch=marker+"""
    ai_real cyTransform[5]; unsigned cyCount = 5;
    if (mat.Get("$cy3d.uv.transform", tt, slot, cyTransform, &cyCount) == AI_SUCCESS && cyCount == 5) {
        prop.textureTransformSupported = true;
        prop.TextureTransformExt_t.offset[0] = cyTransform[0];
        prop.TextureTransformExt_t.offset[1] = cyTransform[1];
        prop.TextureTransformExt_t.rotation = cyTransform[2];
        prop.TextureTransformExt_t.scale[0] = cyTransform[3];
        prop.TextureTransformExt_t.scale[1] = cyTransform[4];
        mAsset->extensionsUsed.KHR_texture_transform = true;
    }"""
if patch not in text:
    if text.count(marker)!=3:raise RuntimeError('Pinned glTF texture exporter marker changed')
    text=text.replace(marker,patch)
if text!=original:p.write_text(text)
p=SOURCE/'code/AssetLib/glTF2/glTF2AssetWriter.inl'
text=p.read_text(); original=text
marker='            tex.AddMember("index", t.texture->index, al);'
patch=marker+"""
            if (t.textureTransformSupported) {
                Value offset(rapidjson::kArrayType), scale(rapidjson::kArrayType), transform(rapidjson::kObjectType), extensions(rapidjson::kObjectType);
                offset.PushBack(t.TextureTransformExt_t.offset[0], al).PushBack(t.TextureTransformExt_t.offset[1], al);
                scale.PushBack(t.TextureTransformExt_t.scale[0], al).PushBack(t.TextureTransformExt_t.scale[1], al);
                transform.AddMember("offset", offset, al);
                transform.AddMember("scale", scale, al);
                transform.AddMember("rotation", t.TextureTransformExt_t.rotation, al);
                extensions.AddMember("KHR_texture_transform", transform, al);
                tex.AddMember("extensions", extensions, al);
            }"""
if patch not in text:
    if marker not in text:raise RuntimeError('Pinned glTF texture writer marker changed')
    text=text.replace(marker,patch)
if text!=original:p.write_text(text)

# Assimp 6.0.2 reuses the UV lookup across channels, producing invalid UV1 indices.
p=SOURCE/'code/AssetLib/FBX/FBXExporter.cpp'
text=p.read_text(); original=text
marker='          std::map<aiVector3D, int32_t> index_by_uv;'
loop='          for (size_t uvi = 0; uvi < m->GetNumUVChannels(); uvi++) {'
patch=loop+'\n            std::map<aiVector3D, int32_t> index_by_uv;'
if patch not in text:
    if marker not in text or loop not in text:raise RuntimeError('Pinned FBX UV marker changed')
    text=text.replace('\n'+marker+'\n','\n').replace(loop,patch)
if text!=original:p.write_text(text)

p=SOURCE/'code/AssetLib/glTF2/glTF2AssetWriter.inl'
text=p.read_text(); original=text
marker='            if (this->mAsset.extensionsUsed.KHR_texture_basisu) {'
patch='            if (this->mAsset.extensionsUsed.KHR_texture_transform) {\n                exts.PushBack(StringRef("KHR_texture_transform"), mAl);\n            }\n\n'+marker
if patch not in text:
    if marker not in text:raise RuntimeError('Pinned glTF extension declaration marker changed')
    text=text.replace(marker,patch)
if text!=original:p.write_text(text)

# The minimum accessor accumulator must start at +infinity, not the smallest positive double.
p=SOURCE/'code/AssetLib/glTF2/glTF2Exporter.cpp'
text=p.read_text(); original=text
text=text.replace('acc->min.push_back(std::numeric_limits<double>::min());','acc->min.push_back(std::numeric_limits<double>::max());')
if text!=original:p.write_text(text)

# Only advertise the volume extension when its properties actually exist.
p=SOURCE/'code/AssetLib/glTF2/glTF2Exporter.cpp'
text=p.read_text(); original=text
text=text.replace('mat.Get(AI_MATKEY_VOLUME_THICKNESS_FACTOR, volume.thicknessFactor) != aiReturn_SUCCESS','mat.Get(AI_MATKEY_VOLUME_THICKNESS_FACTOR, volume.thicknessFactor) == aiReturn_SUCCESS')
text=text.replace('result = result || mat.Get(AI_MATKEY_VOLUME_ATTENUATION_DISTANCE, volume.attenuationDistance);','result = result || mat.Get(AI_MATKEY_VOLUME_ATTENUATION_DISTANCE, volume.attenuationDistance) == aiReturn_SUCCESS;')
text=text.replace('GetMatColor(mat, volume.attenuationColor, AI_MATKEY_VOLUME_ATTENUATION_COLOR) != aiReturn_SUCCESS','GetMatColor(mat, volume.attenuationColor, AI_MATKEY_VOLUME_ATTENUATION_COLOR) == aiReturn_SUCCESS')
if text!=original:p.write_text(text)
