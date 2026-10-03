// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Cyberalien
#include "cy3d_extension.h"
#include <assimp/Exporter.hpp>
#include <assimp/scene.h>
#include <assimp/material.h>
#include <assimp/GltfMaterial.h>
#include <assimp/IOSystem.hpp>
#include <assimp/IOStream.hpp>
#include <assimp/ProgressHandler.hpp>
#include <assimp/postprocess.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>
#include <filesystem>
#include <fstream>
#include <memory>
#include <vector>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <array>
#include <cstdio>
#include <bit>
#include <stdexcept>
namespace {
namespace fs = std::filesystem;
fs::path path(const char* s) {
    return fs::u8path(s);
}
std::string utf8(const fs::path& p) {
    auto s = p.generic_u8string();
    return {reinterpret_cast<const char*>(s.data()), s.size()};
}
void check(const Cy3DHost* h) {
    if (h->is_cancelled && h->is_cancelled(h->context))
        throw std::runtime_error("Operation cancelled.");
}
void progress(const Cy3DHost* h, float f) {
    check(h);
    if (h->progress)
        h->progress(h->context, f);
}
aiMatrix4x4 matrix(const float* p) {
    return {p[0], p[4], p[8],  p[12], p[1], p[5], p[9],  p[13],
            p[2], p[6], p[10], p[14], p[3], p[7], p[11], p[15]};
}
float srgb(float f) {
    return f <= .0031308f ? f * 12.92f : 1.055f * std::pow(std::max(0.f, f), 1.f / 2.4f) - .055f;
}
struct Stream : Assimp::IOStream {
    mutable std::fstream file;
    const Cy3DHost* host;
    uint64_t* written;
    bool write;
    Stream(const fs::path& p, bool w, const Cy3DHost* h, uint64_t* bytes)
        : host(h), written(bytes), write(w) {
        file.open(p, std::ios::binary | (w ? std::ios::out | std::ios::trunc : std::ios::in));
    }
    size_t Read(void* p, size_t size, size_t count) override {
        if (!size)
            return 0;
        file.read(static_cast<char*>(p), static_cast<std::streamsize>(size * count));
        return static_cast<size_t>(file.gcount()) / size;
    }
    size_t Write(const void* p, size_t size, size_t count) override {
        if ((host->is_cancelled && host->is_cancelled(host->context)) || !size || count > SIZE_MAX / size ||
            *written + size * count > host->max_output_bytes)
            return 0;
        file.write(static_cast<const char*>(p), static_cast<std::streamsize>(size * count));
        *written += size * count;
        return file ? count : 0;
    }
    aiReturn Seek(size_t offset, aiOrigin origin) override {
        auto dir = origin == aiOrigin_SET   ? std::ios::beg
                   : origin == aiOrigin_CUR ? std::ios::cur
                                            : std::ios::end;
        auto n = static_cast<std::streamoff>(offset);
        if (origin == aiOrigin_END)
            n = -n;
        if (write)
            file.seekp(n, dir);
        else
            file.seekg(n, dir);
        return file ? aiReturn_SUCCESS : aiReturn_FAILURE;
    }
    size_t Tell() const override { return static_cast<size_t>(write ? file.tellp() : file.tellg()); }
    size_t FileSize() const override {
        auto old = file.tellg();
        file.seekg(0, std::ios::end);
        auto size = file.tellg();
        file.seekg(old);
        return static_cast<size_t>(size);
    }
    void Flush() override { file.flush(); }
};
struct IO : Assimp::IOSystem {
    fs::path root;
    const Cy3DHost* host;
    uint64_t written = 0;
    IO(fs::path p, const Cy3DHost* h) : root(fs::weakly_canonical(p)), host(h) {}
    fs::path resolve(const char* s) const {
        auto p = path(s);
        if (!p.is_absolute())
            p = root / p;
        p = fs::weakly_canonical(p);
        auto r = p.lexically_relative(root);
        if (r.empty() || r.is_absolute() || *r.begin() == "..")
            throw std::runtime_error("Exporter tried to access outside its staging folder.");
        return p;
    }
    bool Exists(const char* p) const override {
        try {
            return fs::exists(resolve(p));
        } catch (...) {
            return false;
        }
    }
    char getOsSeparator() const override { return '/'; }
    Assimp::IOStream* Open(const char* p, const char* mode) override {
        try {
            auto target = resolve(p);
            bool w = mode[0] == 'w';
            if (w)
                fs::create_directories(target.parent_path());
            auto s = std::make_unique<Stream>(target, w, host, &written);
            return s->file ? s.release() : nullptr;
        } catch (...) {
            return nullptr;
        }
    }
    void Close(Assimp::IOStream* p) override { delete p; }
};
struct Progress : Assimp::ProgressHandler {
    const Cy3DHost* host;
    explicit Progress(const Cy3DHost* h) : host(h) {}
    bool Update(float f) override {
        if (host->progress)
            host->progress(host->context, .65f + .3f * std::clamp(f, 0.f, 1.f));
        return !host->is_cancelled || !host->is_cancelled(host->context);
    }
};
constexpr uint32_t full = CY3D_EXPORT_MATERIALS | CY3D_EXPORT_PBR | CY3D_EXPORT_ANIMATION | CY3D_EXPORT_SKIN |
                          CY3D_EXPORT_COLORS | CY3D_EXPORT_UV | CY3D_EXPORT_NODES;
const Cy3DExportFormat formats[]{
    {"glb2", "glTF 2 binary (GLB)", "glb", full},
    {"gltf2", "glTF 2 (glTF + BIN)", "gltf", full},
    {"fbx", "FBX binary", "fbx", full & ~CY3D_EXPORT_PBR},
    {"fbxa", "FBX ASCII", "fbx", full & ~CY3D_EXPORT_PBR},
    {"obj", "Wavefront OBJ + MTL", "obj", CY3D_EXPORT_MATERIALS | CY3D_EXPORT_UV},
    {"stlb", "STL binary", "stl", 0},
    {"plyb", "PLY binary", "ply", CY3D_EXPORT_COLORS | CY3D_EXPORT_UV | CY3D_EXPORT_POINTS}};
struct Builder {
    const Cy3DScene& in;
    const Cy3DHost* host;
    fs::path output;
    bool gltf, embed, materials;
    aiScene scene;
    std::vector<aiTexture*> images;
    uint64_t memory = 0;
    std::vector<std::array<std::array<float, 9>, 2>> transforms;
    Builder(const Cy3DScene& s, const Cy3DHost* h, fs::path p, const std::string& id)
        : in(s), host(h), output(std::move(p)), gltf(id == "glb2" || id == "gltf2"),
          embed(id == "glb2" || id == "fbx" || id == "fbxa"), materials(id != "stlb" && id != "plyb") {}
    ~Builder() {
        for (auto* image : images)
            delete image;
    }
    void budget(uint64_t bytes) {
        memory += bytes;
        if (memory > host->max_output_bytes)
            throw std::runtime_error("Export working data exceeds 2 GiB.");
        check(host);
    }
    std::string image(const uint8_t* rgba, uint32_t w, uint32_t h) {
        if (!rgba || !w || !h || w > 16384 || h > 16384)
            throw std::runtime_error("An export texture is missing or invalid.");
        std::vector<uint8_t> png;
        int ok = stbi_write_png_to_func(
            [](void* c, void* p, int n) {
                auto& bytes = *static_cast<std::vector<uint8_t>*>(c);
                auto* b = static_cast<uint8_t*>(p);
                bytes.insert(bytes.end(), b, b + n);
            },
            &png, static_cast<int>(w), static_cast<int>(h), 4, rgba, static_cast<int>(w * 4));
        if (!ok)
            throw std::runtime_error("PNG encoding failed.");
        budget(png.size());
        std::string filename =
            utf8(output.stem()) + "_textures/texture_" + std::to_string(images.size()) + ".png";
        auto texture = std::make_unique<aiTexture>();
        texture->mWidth = static_cast<unsigned>(png.size());
        texture->mHeight = 0;
        std::strcpy(texture->achFormatHint, "png");
        texture->mFilename.Set(filename);
        texture->pcData = new aiTexel[(png.size() + sizeof(aiTexel) - 1) / sizeof(aiTexel)];
        std::memcpy(texture->pcData, png.data(), png.size());
        auto index = images.size();
        images.push_back(texture.release());
        if (embed)
            return "*" + std::to_string(index);
        auto destination = output.parent_path() / path(filename.c_str());
        fs::create_directories(destination.parent_path());
        std::ofstream file(destination, std::ios::binary);
        file.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
        if (!file)
            throw std::runtime_error("Cannot write texture file.");
        return filename;
    }
    std::string image(int index) {
        const auto& t = in.textures[index];
        return image(t.bytes, t.width, t.height);
    }
    void map(aiMaterial& material, aiTextureType type, const Cy3DMap& map, const std::string& ref) {
        aiString name(ref);
        material.AddProperty(&name, AI_MATKEY_TEXTURE(type, 0));
        if (gltf) {
            const auto* t = map.transform;
            float sx = std::hypot(t[0], t[1]), rotation = std::atan2(-t[1], t[0]);
            float sy = sx > 1e-8f ? (t[0] * t[4] - t[1] * t[3]) / sx : std::hypot(t[3], t[4]);
            if (sx < 1e-8f && std::abs(sy) > 1e-8f)
                rotation = std::atan2(t[3], t[4]);
            if (std::abs(t[3] - std::sin(rotation) * sy) > 1e-4f ||
                std::abs(t[4] - std::cos(rotation) * sy) > 1e-4f ||
                std::abs(t[2]) + std::abs(t[5]) + std::abs(t[8] - 1) > 1e-4f)
                throw std::runtime_error("glTF cannot represent a sheared or projective UV transform.");
            float values[]{t[6] + t[3], 1 - t[7] - t[4], rotation, sx, sy};
            material.AddProperty(values, 5, "$cy3d.uv.transform", type, 0);
        }
        int uv = static_cast<int>(map.uv_set);
        material.AddProperty(&uv, 1, AI_MATKEY_UVWSRC(type, 0));
        auto mode = [](uint32_t n) {
            return n == CY3D_CLAMP    ? aiTextureMapMode_Clamp
                   : n == CY3D_MIRROR ? aiTextureMapMode_Mirror
                                      : aiTextureMapMode_Wrap;
        };
        int u = mode(map.wrap_u), v = mode(map.wrap_v);
        material.AddProperty(&u, 1, AI_MATKEY_MAPPINGMODE_U(type, 0));
        material.AddProperty(&v, 1, AI_MATKEY_MAPPINGMODE_V(type, 0));
    }
    std::string scalarImage(const Cy3DMap& m) {
        const auto& t = in.textures[m.texture];
        budget(uint64_t(t.width) * t.height * 4);
        std::vector<uint8_t> pixels(size_t(t.width) * t.height * 4);
        if (!t.bytes || t.byte_count != pixels.size())
            throw std::runtime_error("A required texture is missing.");
        for (size_t i = 0; i < pixels.size(); i += 4) {
            pixels[i] = pixels[i + 1] = pixels[i + 2] = t.bytes[i + m.channel];
            pixels[i + 3] = 255;
        }
        return image(pixels.data(), t.width, t.height);
    }
    std::string orm(const Cy3DMaterial& m, Cy3DMap& settings) {
        const auto& metal = m.maps[CY3D_METALLIC];
        const auto& rough = m.maps[CY3D_ROUGHNESS];
        settings = metal.texture >= 0 ? metal : rough;
        if (metal.texture >= 0 && rough.texture >= 0 &&
            (metal.uv_set != rough.uv_set || metal.wrap_u != rough.wrap_u || metal.wrap_v != rough.wrap_v ||
             !std::equal(metal.transform, metal.transform + 9, rough.transform)))
            throw std::runtime_error("Metallic and roughness maps need matching UV coordinates and sampling "
                                     "settings for glTF export.");
        uint32_t w = 1, h = 1;
        for (const auto* v : {&metal, &rough})
            if (v->texture >= 0) {
                const auto& t = in.textures[v->texture];
                w = std::max(w, t.width);
                h = std::max(h, t.height);
            }
        budget(uint64_t(w) * h * 4);
        std::vector<uint8_t> pixels(size_t(w) * h * 4, 255);
        for (uint32_t y = 0; y < h; ++y) {
            check(host);
            for (uint32_t x = 0; x < w; ++x) {
                auto sample = [&](const Cy3DMap& map) -> uint8_t {
                    if (map.texture < 0)
                        return 255;
                    const auto& t = in.textures[map.texture];
                    if (!t.bytes || !t.width || !t.height)
                        throw std::runtime_error("A required PBR texture is missing.");
                    auto px = std::min(t.width - 1, static_cast<uint32_t>((uint64_t(x) * t.width) / w));
                    auto py = std::min(t.height - 1, static_cast<uint32_t>((uint64_t(y) * t.height) / h));
                    return t.bytes[(size_t(py) * t.width + px) * 4 + map.channel];
                };
                auto index = (size_t(y) * w + x) * 4;
                pixels[index + 1] = sample(rough);
                pixels[index + 2] = sample(metal);
            }
        }
        return image(pixels.data(), w, h);
    }
    void buildMaterials() {
        scene.mNumMaterials = in.material_count;
        scene.mMaterials = new aiMaterial* [in.material_count] {};
        transforms.resize(in.material_count);
        for (uint32_t i = 0; i < in.material_count; ++i) {
            const auto& m = in.materials[i];
            auto* a = scene.mMaterials[i] = new aiMaterial;
            aiString name("Material_" + std::to_string(i));
            a->AddProperty(&name, AI_MATKEY_NAME);
            aiColor4D color(m.color[0], m.color[1], m.color[2], m.color[3]);
            aiColor3D emission(m.emissive[0], m.emissive[1], m.emissive[2]);
            if (!gltf) {
                color.r = srgb(color.r);
                color.g = srgb(color.g);
                color.b = srgb(color.b);
                emission.r = srgb(emission.r);
                emission.g = srgb(emission.g);
                emission.b = srgb(emission.b);
            }
            if (gltf) {
                float strength = std::max({1.f, emission.r, emission.g, emission.b});
                if (strength > 1) {
                    emission.r /= strength;
                    emission.g /= strength;
                    emission.b /= strength;
                    a->AddProperty(&strength, 1, AI_MATKEY_EMISSIVE_INTENSITY);
                }
            }
            a->AddProperty(&color, 1, AI_MATKEY_COLOR_DIFFUSE);
            a->AddProperty(&color, 1, AI_MATKEY_BASE_COLOR);
            a->AddProperty(&emission, 1, AI_MATKEY_COLOR_EMISSIVE);
            a->AddProperty(&m.color[3], 1, AI_MATKEY_OPACITY);
            a->AddProperty(&m.metallic, 1, AI_MATKEY_METALLIC_FACTOR);
            a->AddProperty(&m.roughness, 1, AI_MATKEY_ROUGHNESS_FACTOR);
            int two = static_cast<int>(m.double_sided);
            a->AddProperty(&two, 1, AI_MATKEY_TWOSIDED);
            int shading = m.unlit ? aiShadingMode_Unlit : gltf ? aiShadingMode_PBR_BRDF : aiShadingMode_Phong;
            a->AddProperty(&shading, 1, AI_MATKEY_SHADING_MODEL);
            float shine = std::max(0.f, 2.f / (m.roughness * m.roughness + .0001f) - 2.f);
            a->AddProperty(&shine, 1, AI_MATKEY_SHININESS);
            aiString alpha(m.alpha_mode == CY3D_BLEND  ? "BLEND"
                           : m.alpha_mode == CY3D_MASK ? "MASK"
                                                       : "OPAQUE");
            a->AddProperty(&alpha, AI_MATKEY_GLTF_ALPHAMODE);
            a->AddProperty(&m.alpha_cutoff, 1, AI_MATKEY_GLTF_ALPHACUTOFF);
            for (auto& t : transforms[i])
                t = {1, 0, 0, 0, 1, 0, 0, 0, 1};
            bool used[2]{};
            if (!materials)
                continue;
            for (unsigned slot = 0; slot < CY3D_MAP_COUNT; ++slot) {
                const auto& v = m.maps[slot];
                if (v.texture < 0)
                    continue;
                if (!gltf && (slot == CY3D_METALLIC || slot == CY3D_ROUGHNESS || slot == CY3D_OCCLUSION))
                    continue;
                if (!gltf && used[v.uv_set] &&
                    !std::equal(v.transform, v.transform + 9, transforms[i][v.uv_set].begin()))
                    throw std::runtime_error(
                        "Maps sharing a UV channel need the same UV transform for this exporter.");
                if (!gltf)
                    std::copy_n(v.transform, 9, transforms[i][v.uv_set].begin());
                used[v.uv_set] = true;
                if (slot == CY3D_METALLIC || slot == CY3D_ROUGHNESS)
                    continue;
                aiTextureType type = slot == CY3D_BASE_COLOR
                                         ? (gltf ? aiTextureType_BASE_COLOR : aiTextureType_DIFFUSE)
                                     : slot == CY3D_NORMAL    ? aiTextureType_NORMALS
                                     : slot == CY3D_OCCLUSION ? aiTextureType_LIGHTMAP
                                                              : aiTextureType_EMISSIVE;
                map(*a, type, v, slot == CY3D_OCCLUSION ? scalarImage(v) : image(v.texture));
                if (slot == CY3D_NORMAL) {
                    a->AddProperty(&m.normal_scale, 1, AI_MATKEY_GLTF_TEXTURE_SCALE(type, 0));
                    a->AddProperty(&m.normal_scale, 1, "$tex.file.scale", type, 0);
                }
                if (slot == CY3D_OCCLUSION) {
                    a->AddProperty(&m.occlusion_strength, 1, AI_MATKEY_GLTF_TEXTURE_STRENGTH(type, 0));
                    a->AddProperty(&m.occlusion_strength, 1, "$tex.file.strength", type, 0);
                }
            }
            if (gltf && (m.maps[CY3D_METALLIC].texture >= 0 || m.maps[CY3D_ROUGHNESS].texture >= 0)) {
                Cy3DMap settings;
                auto packed = orm(m, settings);
                map(*a, aiTextureType_DIFFUSE_ROUGHNESS, settings, packed);
            }
        }
        scene.mNumTextures = embed ? static_cast<unsigned>(images.size()) : 0;
        if (embed) {
            scene.mTextures = new aiTexture* [images.size()] {};
            std::copy(images.begin(), images.end(), scene.mTextures);
            images.clear();
        }
    }
    void buildMeshes() {
        scene.mNumMeshes = in.mesh_count;
        scene.mMeshes = new aiMesh* [in.mesh_count] {};
        for (uint32_t i = 0; i < in.mesh_count; ++i) {
            const auto& m = in.meshes[i];
            if (m.topology == CY3D_SPLATS)
                throw std::runtime_error(
                    "Gaussian splats require a dedicated exporter; mesh conversion is unavailable.");
            auto* a = scene.mMeshes[i] = new aiMesh;
            a->mName.Set("Mesh_" + std::to_string(i));
            a->mMaterialIndex = m.material;
            a->mNumVertices = m.vertex_count;
            budget(uint64_t(m.vertex_count) * 72 + uint64_t(m.index_count) * 16);
            a->mVertices = new aiVector3D[m.vertex_count];
            a->mNormals = new aiVector3D[m.vertex_count];
            for (int u = 0; u < 2; ++u) {
                a->mTextureCoords[u] = new aiVector3D[m.vertex_count];
                a->mNumUVComponents[u] = 2;
            }
            if (m.colors)
                a->mColors[0] = new aiColor4D[m.vertex_count];
            for (uint32_t j = 0; j < m.vertex_count; ++j) {
                if ((j & 65535) == 0)
                    check(host);
                const auto& v = m.vertices[j];
                a->mVertices[j] = {v.position[0], v.position[1], v.position[2]};
                a->mNormals[j] = {v.normal[0], v.normal[1], v.normal[2]};
                for (int u = 0; u < 2; ++u) {
                    const auto* uv = u ? v.uv1 : v.uv;
                    const auto& t = transforms[m.material][u];
                    a->mTextureCoords[u][j] = {t[0] * uv[0] + t[3] * uv[1] + t[6],
                                               t[1] * uv[0] + t[4] * uv[1] + t[7], 0};
                }
                if (m.colors)
                    a->mColors[0][j] = {m.colors[j * 4], m.colors[j * 4 + 1], m.colors[j * 4 + 2],
                                        m.colors[j * 4 + 3]};
            }
            unsigned n = m.topology == CY3D_TRIANGLES ? 3 : 1;
            a->mPrimitiveTypes = n == 3 ? aiPrimitiveType_TRIANGLE : aiPrimitiveType_POINT;
            a->mNumFaces = m.index_count / n;
            a->mFaces = new aiFace[a->mNumFaces];
            for (uint32_t j = 0; j < a->mNumFaces; ++j) {
                auto& f = a->mFaces[j];
                f.mNumIndices = n;
                f.mIndices = new unsigned[n];
                std::copy_n(m.indices + j * n, n, f.mIndices);
            }
            if (m.skin) {
                a->mNumBones = m.bone_count;
                a->mBones = new aiBone* [m.bone_count] {};
                std::vector<std::vector<aiVertexWeight>> weights(m.bone_count);
                for (uint32_t j = 0; j < m.vertex_count; ++j)
                    for (int k = 0; k < 4; ++k)
                        if (m.skin[j].weights[k] > 0)
                            weights[m.skin[j].joints[k]].emplace_back(j, m.skin[j].weights[k]);
                for (uint32_t b = 0; b < m.bone_count; ++b) {
                    auto* bone = a->mBones[b] = new aiBone;
                    bone->mName.Set("Node_" + std::to_string(m.bones[b].node));
                    bone->mOffsetMatrix = matrix(m.bones[b].inverse_bind);
                    bone->mNumWeights = static_cast<unsigned>(weights[b].size());
                    bone->mWeights = new aiVertexWeight[weights[b].size()];
                    std::copy(weights[b].begin(), weights[b].end(), bone->mWeights);
                }
            }
            progress(host, .2f + .35f * static_cast<float>(i + 1) / in.mesh_count);
        }
    }
    void buildNodes() {
        scene.mRootNode = new aiNode("Cy3DView");
        std::vector<aiNode*> nodes(in.node_count);
        std::vector<std::vector<aiNode*>> children(in.node_count + 1);
        for (uint32_t i = 0; i < in.node_count; ++i) {
            auto* n = nodes[i] = new aiNode("Node_" + std::to_string(i));
            n->mTransformation = matrix(in.nodes[i].transform);
            auto p = in.nodes[i].parent;
            n->mParent = p < 0 ? scene.mRootNode : nodes[p];
            children[p < 0 ? in.node_count : static_cast<uint32_t>(p)].push_back(n);
        }
        for (uint32_t i = 0; i < in.instance_count; ++i) {
            const auto& v = in.instances[i];
            // Separate mesh objects from skeleton roots, including for Blender FBX import.
            auto* n = new aiNode("Instance_" + std::to_string(i));
            auto parent = v.node >= 0 ? static_cast<uint32_t>(v.node) : in.node_count;
            if (v.node < 0)
                n->mTransformation = matrix(v.transform);
            n->mParent = parent == in.node_count ? scene.mRootNode : nodes[parent];
            n->mNumMeshes = 1;
            n->mMeshes = new unsigned[1]{v.mesh};
            children[parent].push_back(n);
        }
        for (uint32_t i = 0; i <= in.node_count; ++i) {
            auto* n = i == in.node_count ? scene.mRootNode : nodes[i];
            n->mNumChildren = static_cast<unsigned>(children[i].size());
            n->mChildren = new aiNode*[children[i].size()];
            std::copy(children[i].begin(), children[i].end(), n->mChildren);
        }
        scene.mNumAnimations = in.animation_count;
        scene.mAnimations = new aiAnimation* [in.animation_count] {};
        for (uint32_t i = 0; i < in.animation_count; ++i) {
            const auto& clip = in.animations[i];
            auto* a = scene.mAnimations[i] = new aiAnimation;
            a->mName.Set(clip.name ? clip.name : "Animation");
            a->mDuration = clip.duration;
            a->mTicksPerSecond = 1;
            a->mNumChannels = clip.track_count;
            a->mChannels = new aiNodeAnim* [clip.track_count] {};
            for (uint32_t j = 0; j < clip.track_count; ++j) {
                const auto& t = clip.tracks[j];
                if (gltf) {
                    auto uniform = [](const Cy3DKey* keys, uint32_t count) {
                        for (uint32_t k = 1; k < count; ++k)
                            if (keys[k].step != keys[0].step)
                                throw std::runtime_error("A channel mixing STEP and linear keys cannot be "
                                                         "represented by this glTF exporter.");
                    };
                    uniform(t.positions, t.position_count);
                    uniform(t.rotations, t.rotation_count);
                    uniform(t.scales, t.scale_count);
                }
                const auto& n = in.nodes[t.node];
                auto* track = a->mChannels[j] = new aiNodeAnim;
                track->mNodeName.Set("Node_" + std::to_string(t.node));
                auto vectors = [](const Cy3DKey* keys, uint32_t count, const float* fallback,
                                  aiVectorKey*& out, unsigned& size) {
                    size = std::max(1u, count);
                    out = new aiVectorKey[size];
                    for (uint32_t k = 0; k < size; ++k) {
                        const auto* v = count ? keys[k].value : fallback;
                        out[k].mTime = count ? keys[k].time : 0;
                        out[k].mValue = {v[0], v[1], v[2]};
                        out[k].mInterpolation =
                            count && keys[k].step ? aiAnimInterpolation_Step : aiAnimInterpolation_Linear;
                    }
                };
                vectors(t.positions, t.position_count, n.translation, track->mPositionKeys,
                        track->mNumPositionKeys);
                vectors(t.scales, t.scale_count, n.scale, track->mScalingKeys, track->mNumScalingKeys);
                track->mNumRotationKeys = std::max(1u, t.rotation_count);
                track->mRotationKeys = new aiQuatKey[track->mNumRotationKeys];
                for (uint32_t k = 0; k < track->mNumRotationKeys; ++k) {
                    const auto* v = t.rotation_count ? t.rotations[k].value : n.rotation;
                    auto& key = track->mRotationKeys[k];
                    key.mTime = t.rotation_count ? t.rotations[k].time : 0;
                    key.mValue = {v[3], v[0], v[1], v[2]};
                    key.mInterpolation = t.rotation_count && t.rotations[k].step ? aiAnimInterpolation_Step
                                                                                 : aiAnimInterpolation_Linear;
                }
            }
        }
    }
};
// A small deterministic PLY writer avoids upstream multi-UV header inconsistencies.
void writePly(const Cy3DScene& input, const char* destination, const Cy3DHost* host) {
    uint64_t vertices = 0, faces = 0;
    for (uint32_t i = 0; i < input.instance_count; ++i) {
        const auto& m = input.meshes[input.instances[i].mesh];
        if (m.topology == CY3D_SPLATS)
            throw std::runtime_error("Gaussian splats require a dedicated exporter.");
        vertices += m.topology == CY3D_POINTS ? m.index_count : m.vertex_count;
        if (m.topology == CY3D_TRIANGLES)
            faces += m.index_count / 3;
    }
    if (vertices > UINT32_MAX || vertices * 36 + faces * 13 > host->max_output_bytes)
        throw std::runtime_error("PLY output exceeds its budget.");
    std::ofstream out(path(destination), std::ios::binary);
    out << "ply\nformat binary_little_endian 1.0\ncomment Created by Cy3DView\nelement vertex " << vertices
        << "\nproperty float x\nproperty float y\nproperty float z\nproperty float nx\nproperty float "
           "ny\nproperty float nz\nproperty float s\nproperty float t\nproperty uchar red\nproperty uchar "
           "green\nproperty uchar blue\nproperty uchar alpha\nelement face "
        << faces << "\nproperty list uchar uint vertex_indices\nend_header\n";
    auto u32 = [&](uint32_t n) {
        unsigned char b[]{static_cast<unsigned char>(n), static_cast<unsigned char>(n >> 8),
                          static_cast<unsigned char>(n >> 16), static_cast<unsigned char>(n >> 24)};
        out.write(reinterpret_cast<const char*>(b), 4);
    };
    auto f32 = [&](float n) { u32(std::bit_cast<uint32_t>(n)); };
    for (uint32_t i = 0; i < input.instance_count; ++i) {
        check(host);
        const auto& instance = input.instances[i];
        const auto& m = input.meshes[instance.mesh];
        auto transform = matrix(instance.transform);
        aiMatrix3x3 normal(transform);
        normal.Inverse().Transpose();
        uint32_t count = m.topology == CY3D_POINTS ? m.index_count : m.vertex_count;
        for (uint32_t j = 0; j < count; ++j) {
            if ((j & 65535) == 0)
                check(host);
            auto v = m.topology == CY3D_POINTS ? m.indices[j] : j;
            const auto& a = m.vertices[v];
            auto position = transform * aiVector3D(a.position[0], a.position[1], a.position[2]);
            auto n = normal * aiVector3D(a.normal[0], a.normal[1], a.normal[2]);
            if (n.SquareLength() > 1e-12f)
                n.Normalize();
            else
                n = {0, 1, 0};
            f32(position.x);
            f32(position.y);
            f32(position.z);
            f32(n.x);
            f32(n.y);
            f32(n.z);
            f32(a.uv[0]);
            f32(a.uv[1]);
            unsigned char color[4]{255, 255, 255, 255};
            if (m.colors)
                for (int c = 0; c < 4; ++c)
                    color[c] = static_cast<unsigned char>(std::lround(
                        std::clamp(c == 3 ? m.colors[size_t(v) * 4 + c] : srgb(m.colors[size_t(v) * 4 + c]),
                                   0.f, 1.f) *
                        255));
            out.write(reinterpret_cast<const char*>(color), 4);
        }
        progress(host, .05f + .7f * static_cast<float>(i + 1) / input.instance_count);
    }
    uint32_t offset = 0;
    for (uint32_t i = 0; i < input.instance_count; ++i) {
        const auto& m = input.meshes[input.instances[i].mesh];
        if (m.topology == CY3D_TRIANGLES)
            for (uint32_t j = 0; j < m.index_count; j += 3) {
                if ((j & 65535) == 0)
                    check(host);
                out.put(3);
                u32(offset + m.indices[j]);
                u32(offset + m.indices[j + 1]);
                u32(offset + m.indices[j + 2]);
            }
        offset += m.topology == CY3D_POINTS ? m.index_count : m.vertex_count;
    }
    out.flush();
    if (!out)
        throw std::runtime_error("Cannot write PLY output.");
    progress(host, 1);
}
int exportScene(const char* id, const Cy3DScene* input, const char* destination, const Cy3DHost* host,
                char* error, size_t size) {
    try {
        if (!input || !host || host->struct_size < sizeof(Cy3DHost) || host->api_version != CY3D_API_VERSION)
            throw std::runtime_error("Invalid export arguments.");
        const Cy3DExportFormat* format = nullptr;
        for (const auto& f : formats)
            if (id && std::strcmp(id, f.id) == 0)
                format = &f;
        if (!format)
            throw std::runtime_error("Unknown export format.");
        for (uint32_t i = 0; i < input->mesh_count; ++i)
            if (input->meshes[i].topology == CY3D_POINTS && !(format->capabilities & CY3D_EXPORT_POINTS))
                throw std::runtime_error("Use PLY to export point clouds.");
        if (std::strcmp(id, "plyb") == 0) {
            writePly(*input, destination, host);
            return 0;
        }
        progress(host, .01f);
        Builder builder(*input, host, path(destination), id);
        builder.buildMaterials();
        builder.buildMeshes();
        builder.buildNodes();
        progress(host, .65f);
        Assimp::Exporter exporter;
        exporter.SetIOHandler(new IO(builder.output.parent_path(), host));
        exporter.SetProgressHandler(new Progress(host));
        auto code = exporter.Export(&builder.scene, id, destination);
        check(host);
        if (code != AI_SUCCESS)
            throw std::runtime_error(exporter.GetErrorString());
        progress(host, 1);
        return 0;
    } catch (const std::exception& e) {
        if (size)
            std::snprintf(error, size, "%s", e.what());
        return host && host->is_cancelled && host->is_cancelled(host->context) ? -2 : -1;
    } catch (...) {
        if (size)
            std::snprintf(error, size, "Unexpected exporter failure.");
        return -1;
    }
}
const Cy3DExtension api{CY3D_EXTENSION_VERSION,
                        sizeof(Cy3DExtension),
                        "Assimp exporters",
                        "1.0",
                        formats,
                        static_cast<uint32_t>(std::size(formats)),
                        exportScene,
                        nullptr,
                        0,
                        nullptr,
                        nullptr,
                        nullptr,
                        nullptr,
                        nullptr};
} // namespace
extern "C" CY3D_EXPORT const Cy3DExtension* Cy3D_GetExtension() {
    return &api;
}
