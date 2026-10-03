// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Cyberalien
#include "cy3d_plugin.h"
#include <assimp/Importer.hpp>
#include <assimp/ProgressHandler.hpp>
#include <assimp/IOSystem.hpp>
#include <assimp/IOStream.hpp>
#include <assimp/postprocess.h>
#include <assimp/config.h>
#include <assimp/scene.h>
#include <assimp/material.h>
#include <assimp/GltfMaterial.h>
#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <unordered_map>
#include <vector>
namespace {
namespace fs = std::filesystem;
std::string utf8(const fs::path& p) {
    auto s = p.u8string();
    return {reinterpret_cast<const char*>(s.data()), s.size()};
}
fs::path native(const char* p) {
    std::string s(p);
    std::replace(s.begin(), s.end(), '\\', '/');
    return fs::path(std::u8string_view(reinterpret_cast<const char8_t*>(s.data()), s.size()));
}
class Stream final : public Assimp::IOStream {
    mutable std::ifstream file_;
    size_t size_;

  public:
    explicit Stream(const fs::path& file) : file_(file, std::ios::binary | std::ios::ate), size_(0) {
        if (file_) {
            size_ = static_cast<size_t>(file_.tellg());
            file_.seekg(0);
        }
    }
    bool valid() const { return file_.is_open(); }
    size_t Read(void* buffer, size_t size, size_t count) override {
        if (!size || count > std::numeric_limits<size_t>::max() / size)
            return 0;
        file_.read(static_cast<char*>(buffer), static_cast<std::streamsize>(size * count));
        return static_cast<size_t>(file_.gcount()) / size;
    }
    size_t Write(const void*, size_t, size_t) override { return 0; }
    aiReturn Seek(size_t offset, aiOrigin origin) override {
        file_.clear();
        auto position = static_cast<std::streamoff>(offset);
        if (origin == aiOrigin_END)
            position = -position;
        file_.seekg(position, origin == aiOrigin_SET   ? std::ios::beg
                              : origin == aiOrigin_CUR ? std::ios::cur
                                                       : std::ios::end);
        return file_ ? aiReturn_SUCCESS : aiReturn_FAILURE;
    }
    size_t Tell() const override {
        auto position = file_.tellg();
        return position < 0 ? size_ : static_cast<size_t>(position);
    }
    size_t FileSize() const override { return size_; }
    void Flush() override {}
};
class FileSystem final : public Assimp::IOSystem {
    std::vector<std::string>& files_;

  public:
    explicit FileSystem(std::vector<std::string>& files) : files_(files) {}
    bool Exists(const char* file) const override {
        std::error_code ec;
        return fs::is_regular_file(native(file), ec);
    }
    char getOsSeparator() const override { return '/'; }
    Assimp::IOStream* Open(const char* file, const char* mode) override {
        if (mode[0] != 'r')
            return nullptr;
        auto result = std::make_unique<Stream>(native(file));
        if (!result->valid())
            return nullptr;
        files_.push_back(utf8(fs::absolute(native(file))));
        return result.release();
    }
    void Close(Assimp::IOStream* file) override { delete file; }
};
bool cancelled(const Cy3DHost* host) {
    return host->is_cancelled && host->is_cancelled(host->context);
}
class Progress final : public Assimp::ProgressHandler {
    const Cy3DHost* host_;

  public:
    explicit Progress(const Cy3DHost* host) : host_(host) {}
    bool Update(float fraction = -1.f) override {
        if (host_->progress && fraction >= 0)
            host_->progress(host_->context, fraction * .65f);
        return !cancelled(host_);
    }
};
struct Storage {
    Cy3DScene scene{};
    std::deque<std::string> strings;
    std::vector<std::vector<Cy3DVertex>> vertices;
    std::vector<std::vector<uint32_t>> indices;
    std::vector<std::vector<uint8_t>> texture_bytes;
    std::vector<std::vector<float>> colors;
    std::vector<std::vector<Cy3DSkinVertex>> skin;
    std::vector<std::vector<Cy3DBone>> bones;
    std::vector<Cy3DNode> nodes;
    std::deque<std::vector<Cy3DKey>> keys;
    std::deque<std::vector<Cy3DTrack>> tracks;
    std::vector<Cy3DAnimation> animations;
    std::vector<const char*> warnings;
    std::vector<Cy3DMesh> meshes;
    std::vector<Cy3DInstance> instances;
    std::vector<Cy3DMaterial> materials;
    std::vector<Cy3DTexture> textures;
    std::vector<const char*> dependencies;
    const char* text(std::string s) {
        strings.push_back(std::move(s));
        return strings.back().c_str();
    }
};
void checkBudget(Storage& storage, const Cy3DHost* host, uint64_t bytes) {
    if (cancelled(host))
        throw std::runtime_error("Chargement annule");
    if (bytes > host->max_output_bytes || storage.scene.memory_bytes > host->max_output_bytes - bytes)
        throw std::runtime_error("Modele trop volumineux");
    storage.scene.memory_bytes += bytes;
}
void copyMatrix(float* out, const aiMatrix4x4& m) {
    const float rows[16] = {m.a1, m.a2, m.a3, m.a4, m.b1, m.b2, m.b3, m.b4,
                            m.c1, m.c2, m.c3, m.c4, m.d1, m.d2, m.d3, m.d4};
    for (int row = 0; row < 4; ++row)
        for (int column = 0; column < 4; ++column)
            out[column * 4 + row] = rows[row * 4 + column];
}
int load(const char* filename, const Cy3DHost* host, Cy3DScene** output, char* error, size_t error_size) {
    if (output)
        *output = nullptr;
    try {
        if (!filename || !host || host->api_version != CY3D_API_VERSION ||
            host->struct_size < sizeof(Cy3DHost) || !output)
            throw std::runtime_error("ABI hote invalide");
        if (cancelled(host))
            return -2;
        std::vector<std::string> opened;
        Assimp::Importer importer;
        importer.SetIOHandler(new FileSystem(opened));
        importer.SetProgressHandler(new Progress(host));
        importer.SetPropertyInteger(AI_CONFIG_PP_SBP_REMOVE, aiPrimitiveType_POINT | aiPrimitiveType_LINE);
        const aiScene* source = importer.ReadFile(
            filename, static_cast<unsigned>(aiProcess_Triangulate | aiProcess_JoinIdenticalVertices |
                                            aiProcess_GenSmoothNormals | aiProcess_SortByPType |
                                            aiProcess_GenBoundingBoxes | aiProcess_PopulateArmatureData));
        if (!source || !source->mRootNode || !source->mNumMeshes)
            throw std::runtime_error(importer.GetErrorString()[0] ? importer.GetErrorString()
                                                                  : "Aucun maillage triangulaire");
        auto storage = std::make_unique<Storage>();
        auto& s = *storage;
        s.scene.struct_size = sizeof(Cy3DScene);
        s.vertices.resize(source->mNumMeshes);
        s.indices.resize(source->mNumMeshes);
        s.meshes.resize(source->mNumMeshes);
        s.colors.resize(source->mNumMeshes);
        s.skin.resize(source->mNumMeshes);
        s.bones.resize(source->mNumMeshes);
        bool animated = source->mNumAnimations != 0;
        for (unsigned i = 0; i < source->mNumMeshes; ++i)
            animated = animated || source->mMeshes[i]->HasBones();
        std::unordered_map<const aiNode*, uint32_t> nodeIndex;
        std::unordered_map<std::string, uint32_t> namedNodes;
        if (animated) {
            std::vector<std::pair<const aiNode*, int32_t>> stack{{source->mRootNode, -1}};
            while (!stack.empty()) {
                auto [node, parent] = stack.back();
                stack.pop_back();
                uint32_t index = static_cast<uint32_t>(s.nodes.size());
                nodeIndex[node] = index;
                namedNodes.emplace(node->mName.C_Str(), index);
                Cy3DNode out{};
                out.parent = parent;
                copyMatrix(out.transform, node->mTransformation);
                aiVector3D scale, position;
                aiQuaternion rotation;
                node->mTransformation.Decompose(scale, rotation, position);
                std::copy_n(&position.x, 3, out.translation);
                std::copy_n(&scale.x, 3, out.scale);
                out.rotation[0] = rotation.x;
                out.rotation[1] = rotation.y;
                out.rotation[2] = rotation.z;
                out.rotation[3] = rotation.w;
                checkBudget(s, host, sizeof(Cy3DNode));
                s.nodes.push_back(out);
                for (unsigned j = node->mNumChildren; j > 0; --j)
                    stack.emplace_back(node->mChildren[j - 1], static_cast<int32_t>(index));
            }
        }
        for (unsigned i = 0; i < source->mNumMeshes; ++i) {
            auto& m = *source->mMeshes[i];
            checkBudget(s, host,
                        static_cast<uint64_t>(m.mNumVertices) * sizeof(Cy3DVertex) +
                            static_cast<uint64_t>(m.mNumFaces) * 3 * sizeof(uint32_t));
            auto& vertices = s.vertices[i];
            auto& indices = s.indices[i];
            vertices.resize(m.mNumVertices);
            indices.reserve(static_cast<size_t>(m.mNumFaces) * 3);
            for (unsigned j = 0; j < m.mNumVertices; ++j) {
                if ((j & 65535) == 0 && cancelled(host))
                    return -2;
                auto p = m.mVertices[j], n = m.HasNormals() ? m.mNormals[j] : aiVector3D(0, 1, 0),
                     uv = m.HasTextureCoords(0) ? m.mTextureCoords[0][j] : aiVector3D();
                auto uv1 = m.HasTextureCoords(1) ? m.mTextureCoords[1][j] : uv;
                vertices[j] = {{p.x, p.y, p.z}, {n.x, n.y, n.z}, {uv.x, uv.y}, {uv1.x, uv1.y}};
            }
            for (unsigned j = 0; j < m.mNumFaces; ++j)
                if (m.mFaces[j].mNumIndices == 3)
                    indices.insert(indices.end(), m.mFaces[j].mIndices, m.mFaces[j].mIndices + 3);
            if (indices.empty())
                throw std::runtime_error("Maillage sans triangles");
            s.meshes[i] = {s.text(m.mName.C_Str()),
                           vertices.data(),
                           indices.data(),
                           m.mNumVertices,
                           static_cast<uint32_t>(indices.size()),
                           m.mMaterialIndex,
                           {}};
            auto center = (m.mAABB.mMin + m.mAABB.mMax) * .5f;
            std::copy_n(&center.x, 3, s.meshes[i].center);
            if (m.HasVertexColors(0)) {
                auto& colors = s.colors[i];
                checkBudget(s, host, static_cast<uint64_t>(m.mNumVertices) * 16);
                colors.resize(static_cast<size_t>(m.mNumVertices) * 4);
                for (unsigned j = 0; j < m.mNumVertices; ++j)
                    std::copy_n(&m.mColors[0][j].r, 4, colors.data() + j * 4);
                s.meshes[i].colors = colors.data();
            }
            if (m.HasBones()) {
                auto& skin = s.skin[i];
                auto& bones = s.bones[i];
                checkBudget(s, host,
                            static_cast<uint64_t>(m.mNumVertices) * sizeof(Cy3DSkinVertex) +
                                static_cast<uint64_t>(m.mNumBones) * sizeof(Cy3DBone));
                skin.resize(m.mNumVertices);
                bones.resize(m.mNumBones);
                bool reduced = false;
                for (unsigned b = 0; b < m.mNumBones; ++b) {
                    const auto& bone = *m.mBones[b];
                    auto node = bone.mNode ? nodeIndex.at(bone.mNode) : namedNodes.at(bone.mName.C_Str());
                    bones[b].node = node;
                    copyMatrix(bones[b].inverse_bind, bone.mOffsetMatrix);
                    s.nodes[node].bone = 1;
                    for (unsigned k = 0; k < bone.mNumWeights; ++k) {
                        auto weight = bone.mWeights[k];
                        if (weight.mVertexId >= skin.size())
                            throw std::runtime_error("Sommet d'os invalide");
                        auto& v = skin[weight.mVertexId];
                        int slot = 0;
                        for (int j = 1; j < 4; ++j)
                            if (v.weights[j] < v.weights[slot])
                                slot = j;
                        if (v.weights[slot] > 0)
                            reduced = true;
                        if (weight.mWeight > v.weights[slot]) {
                            v.joints[slot] = b;
                            v.weights[slot] = weight.mWeight;
                        }
                    }
                }
                for (auto& v : skin) {
                    float sum = 0;
                    for (float w : v.weights)
                        sum += w;
                    if (sum > 0)
                        for (float& w : v.weights)
                            w /= sum;
                }
                if (reduced)
                    s.warnings.push_back(s.text(
                        "Squelette : les quatre influences les plus fortes sont conservees par sommet."));
                s.meshes[i].skin = skin.data();
                s.meshes[i].bones = bones.data();
                s.meshes[i].bone_count = m.mNumBones;
            }
            if (m.mNumAnimMeshes)
                s.warnings.push_back(
                    s.text("Morph targets detectes : leur deformation n'est pas encore animee."));
            if (host->progress)
                host->progress(host->context, .65f + .2f * static_cast<float>(i + 1) / source->mNumMeshes);
        }
        std::unordered_map<std::string, int32_t> texture_map;
        fs::path directory = native(filename).parent_path();
        auto extension = native(filename).extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        bool gltf = extension == ".gltf" || extension == ".glb" || extension == ".vrm";
        if (extension == ".usd" || extension == ".usda" || extension == ".usdc" || extension == ".usdz")
            s.warnings.push_back(s.text("USD : lecteur experimental ; les compositions complexes et certains "
                                        "materiaux ou animations peuvent etre incomplets."));
        s.materials.resize(source->mNumMaterials);
        for (unsigned i = 0; i < source->mNumMaterials; ++i) {
            auto& m = *source->mMaterials[i];
            aiString name;
            m.Get(AI_MATKEY_NAME, name);
            aiColor4D color(.72f, .77f, .8f, 1.f);
            if (m.Get(AI_MATKEY_BASE_COLOR, color) != AI_SUCCESS)
                m.Get(AI_MATKEY_COLOR_DIFFUSE, color);
            float opacity = 1;
            m.Get(AI_MATKEY_OPACITY, opacity);
            color.a = std::min(color.a, opacity);
            auto& material = s.materials[i];
            material = Cy3D_DefaultMaterial();
            material.name = s.text(name.C_Str());
            std::copy_n(&color.r, 4, material.color);
            m.Get(AI_MATKEY_METALLIC_FACTOR, material.metallic);
            if (m.Get(AI_MATKEY_ROUGHNESS_FACTOR, material.roughness) != AI_SUCCESS) {
                float shininess = 0;
                if (m.Get(AI_MATKEY_SHININESS, shininess) == AI_SUCCESS && shininess > 0)
                    material.roughness = std::sqrt(2.f / (shininess + 2.f));
            }
            aiColor3D emission(0, 0, 0);
            m.Get(AI_MATKEY_COLOR_EMISSIVE, emission);
            float emissiveStrength = 1;
            m.Get(AI_MATKEY_EMISSIVE_INTENSITY, emissiveStrength);
            material.emissive[0] = emission.r * emissiveStrength;
            material.emissive[1] = emission.g * emissiveStrength;
            material.emissive[2] = emission.b * emissiveStrength;
            m.Get(AI_MATKEY_GLTF_TEXTURE_SCALE(aiTextureType_NORMALS, 0), material.normal_scale);
            // Assimp's glTF importer stores occlusion strength under $tex.file.strength.
            m.Get("$tex.file.strength", aiTextureType_LIGHTMAP, 0, material.occlusion_strength);
            int twoSided = 1;
            m.Get(AI_MATKEY_TWOSIDED, twoSided);
            material.double_sided = twoSided != 0;
            aiShadingMode shading = aiShadingMode_PBR_BRDF;
            m.Get(AI_MATKEY_SHADING_MODEL, shading);
            material.unlit = shading == aiShadingMode_Unlit;
            aiString alpha;
            if (m.Get(AI_MATKEY_GLTF_ALPHAMODE, alpha) == AI_SUCCESS)
                material.alpha_mode = std::strcmp(alpha.C_Str(), "MASK") == 0    ? CY3D_MASK
                                      : std::strcmp(alpha.C_Str(), "BLEND") == 0 ? CY3D_BLEND
                                                                                 : CY3D_OPAQUE;
            else if (color.a < .999f)
                material.alpha_mode = CY3D_BLEND;
            m.Get(AI_MATKEY_GLTF_ALPHACUTOFF, material.alpha_cutoff);
            auto bind = [&](unsigned slot, aiTextureType type, unsigned channel = 0) {
                aiString texture;
                unsigned uv = 0;
                aiTextureMapMode wrap[2]{aiTextureMapMode_Wrap, aiTextureMapMode_Wrap};
                if (m.GetTexture(type, 0, &texture, nullptr, &uv, nullptr, nullptr, wrap) != AI_SUCCESS)
                    return false;
                if (uv > 1)
                    throw std::runtime_error("Jeu UV au-dela de UV1 non pris en charge");
                auto& map = material.maps[slot];
                map.uv_set = uv;
                map.channel = channel;
                auto wrapping = [](aiTextureMapMode w) -> uint32_t {
                    return w == aiTextureMapMode_Clamp || w == aiTextureMapMode_Decal ? CY3D_CLAMP
                           : w == aiTextureMapMode_Mirror                             ? CY3D_MIRROR
                                                                                      : CY3D_REPEAT;
                };
                map.wrap_u = wrapping(wrap[0]);
                map.wrap_v = wrapping(wrap[1]);
                aiUVTransform t;
                if (m.Get(AI_MATKEY_UVTRANSFORM(type, 0), t) == AI_SUCCESS) {
                    float c = std::cos(t.mRotation), sn = std::sin(t.mRotation);
                    float sx = t.mScaling.x, sy = t.mScaling.y;
                    float tx = t.mTranslation.x, ty = t.mTranslation.y;
                    if (gltf) {
                        // Undo Assimp's centered-UV conversion, then conjugate by the V flip.
                        float ox = tx - .5f * sx * (-c - sn + 1);
                        float oy = .5f * sy * (-sn + c - 1) + 1 - sy - ty;
                        float values[9]{c * sx,       sn * sx,         0, -sn * sy, c * sy, 0,
                                        ox + sn * sy, 1 - oy - c * sy, 1};
                        std::copy_n(values, 9, map.transform);
                    } else {
                        float values[9]{c * sx,
                                        sn * sx,
                                        0,
                                        -sn * sy,
                                        c * sy,
                                        0,
                                        tx + .5f - .5f * (c * sx - sn * sy),
                                        ty + .5f - .5f * (sn * sx + c * sy),
                                        1};
                        std::copy_n(values, 9, map.transform);
                    }
                }
                auto key = std::string(texture.C_Str());
                if (auto found = texture_map.find(key); found != texture_map.end()) {
                    map.texture = found->second;
                    return true;
                }
                map.texture = static_cast<int32_t>(s.textures.size());
                texture_map[key] = map.texture;
                Cy3DTexture out{};
                if (const aiTexture* embedded = source->GetEmbeddedTexture(texture.C_Str())) {
                    uint64_t size = embedded->mHeight
                                        ? static_cast<uint64_t>(embedded->mWidth) * embedded->mHeight * 4
                                        : embedded->mWidth;
                    checkBudget(s, host, size);
                    s.texture_bytes.emplace_back(static_cast<size_t>(size));
                    auto& data = s.texture_bytes.back();
                    if (embedded->mHeight) {
                        for (size_t j = 0; j < size / 4; ++j) {
                            auto p = embedded->pcData[j];
                            data[j * 4] = p.r;
                            data[j * 4 + 1] = p.g;
                            data[j * 4 + 2] = p.b;
                            data[j * 4 + 3] = p.a;
                        }
                        out.width = embedded->mWidth;
                        out.height = embedded->mHeight;
                    } else
                        std::memcpy(data.data(), embedded->pcData, data.size());
                    out.bytes = data.data();
                    out.byte_count = size;
                } else
                    out.path = s.text(utf8((directory / native(texture.C_Str())).lexically_normal()));
                s.textures.push_back(out);
                return true;
            };
            if (!bind(CY3D_BASE_COLOR, aiTextureType_BASE_COLOR))
                bind(CY3D_BASE_COLOR, aiTextureType_DIFFUSE);
            bind(CY3D_METALLIC, aiTextureType_METALNESS, gltf ? 2 : 0);
            bind(CY3D_ROUGHNESS, aiTextureType_DIFFUSE_ROUGHNESS, gltf ? 1 : 0);
            bind(CY3D_NORMAL, aiTextureType_NORMALS);
            if (!bind(CY3D_OCCLUSION, aiTextureType_AMBIENT_OCCLUSION))
                bind(CY3D_OCCLUSION, aiTextureType_LIGHTMAP);
            if (!bind(CY3D_EMISSIVE, aiTextureType_EMISSION_COLOR))
                bind(CY3D_EMISSIVE, aiTextureType_EMISSIVE);
        }
        for (int j = 0; j < 3; ++j) {
            s.scene.bounds_min[j] = std::numeric_limits<float>::max();
            s.scene.bounds_max[j] = -std::numeric_limits<float>::max();
        }
        // Explicit stack prevents deep scene graphs from overflowing the C++ call stack.
        struct Node {
            const aiNode* node;
            aiMatrix4x4 parent;
        };
        std::vector<Node> stack{{source->mRootNode, aiMatrix4x4()}};
        while (!stack.empty()) {
            auto item = stack.back();
            stack.pop_back();
            auto transform = item.parent * item.node->mTransformation;
            if (cancelled(host))
                return -2;
            for (unsigned i = 0; i < item.node->mNumMeshes; ++i) {
                checkBudget(s, host, sizeof(Cy3DInstance));
                auto mesh = item.node->mMeshes[i];
                Cy3DInstance instance{};
                instance.mesh = mesh;
                instance.node = animated ? static_cast<int32_t>(nodeIndex.at(item.node)) : -1;
                copyMatrix(instance.transform, transform);
                s.instances.push_back(instance);
                auto box = source->mMeshes[mesh]->mAABB;
                for (int k = 0; k < 8; ++k) {
                    auto p = transform * aiVector3D(k & 1 ? box.mMax.x : box.mMin.x,
                                                    k & 2 ? box.mMax.y : box.mMin.y,
                                                    k & 4 ? box.mMax.z : box.mMin.z);
                    float v[3] = {p.x, p.y, p.z};
                    for (int axis = 0; axis < 3; ++axis) {
                        if (!std::isfinite(v[axis]))
                            throw std::runtime_error("Coordonnees non finies");
                        s.scene.bounds_min[axis] = std::min(s.scene.bounds_min[axis], v[axis]);
                        s.scene.bounds_max[axis] = std::max(s.scene.bounds_max[axis], v[axis]);
                    }
                }
            }
            for (unsigned i = 0; i < item.node->mNumChildren; ++i)
                stack.push_back({item.node->mChildren[i], transform});
        }
        s.scene.meshes = s.meshes.data();
        s.scene.mesh_count = static_cast<uint32_t>(s.meshes.size());
        s.scene.instances = s.instances.data();
        s.scene.instance_count = static_cast<uint32_t>(s.instances.size());
        s.scene.materials = s.materials.data();
        s.scene.material_count = static_cast<uint32_t>(s.materials.size());
        s.scene.textures = s.textures.data();
        s.scene.texture_count = static_cast<uint32_t>(s.textures.size());
        for (unsigned a = 0; a < source->mNumAnimations; ++a) {
            const auto& animation = *source->mAnimations[a];
            double rate = animation.mTicksPerSecond > 0 ? animation.mTicksPerSecond : 25;
            s.tracks.emplace_back();
            auto& tracks = s.tracks.back();
            tracks.reserve(animation.mNumChannels);
            double duration = animation.mDuration / rate;
            for (unsigned j = 0; j < animation.mNumChannels; ++j) {
                const auto& channel = *animation.mChannels[j];
                Cy3DTrack track{};
                track.node = namedNodes.at(channel.mNodeName.C_Str());
                auto vectors = [&](const aiVectorKey* keys, unsigned count, const Cy3DKey*& output,
                                   uint32_t& size) {
                    s.keys.emplace_back(count);
                    auto& target = s.keys.back();
                    checkBudget(s, host, static_cast<uint64_t>(count) * sizeof(Cy3DKey));
                    for (unsigned k = 0; k < count; ++k) {
                        target[k].time = keys[k].mTime / rate;
                        std::copy_n(&keys[k].mValue.x, 3, target[k].value);
                        target[k].step = keys[k].mInterpolation == aiAnimInterpolation_Step;
                        duration = std::max(duration, target[k].time);
                        if (k == 0 && keys[k].mInterpolation == aiAnimInterpolation_Cubic_Spline)
                            s.warnings.push_back(s.text("Animation CUBICSPLINE : tangentes non conservees "
                                                        "par Assimp, interpolation lineaire utilisee."));
                    }
                    output = target.data();
                    size = count;
                };
                vectors(channel.mPositionKeys, channel.mNumPositionKeys, track.positions,
                        track.position_count);
                vectors(channel.mScalingKeys, channel.mNumScalingKeys, track.scales, track.scale_count);
                s.keys.emplace_back(channel.mNumRotationKeys);
                auto& target = s.keys.back();
                checkBudget(s, host, static_cast<uint64_t>(target.size()) * sizeof(Cy3DKey));
                for (unsigned k = 0; k < channel.mNumRotationKeys; ++k) {
                    auto q = channel.mRotationKeys[k].mValue;
                    target[k].time = channel.mRotationKeys[k].mTime / rate;
                    target[k].value[0] = q.x;
                    target[k].value[1] = q.y;
                    target[k].value[2] = q.z;
                    target[k].value[3] = q.w;
                    target[k].step = channel.mRotationKeys[k].mInterpolation == aiAnimInterpolation_Step;
                    duration = std::max(duration, target[k].time);
                }
                if (channel.mNumRotationKeys &&
                    channel.mRotationKeys[0].mInterpolation == aiAnimInterpolation_Cubic_Spline)
                    s.warnings.push_back(s.text("Animation CUBICSPLINE : tangentes non conservees par "
                                                "Assimp, interpolation spherique utilisee."));
                track.rotations = target.data();
                track.rotation_count = channel.mNumRotationKeys;
                tracks.push_back(track);
            }
            checkBudget(s, host, tracks.size() * sizeof(Cy3DTrack));
            s.animations.push_back({s.text(animation.mName.length ? animation.mName.C_Str()
                                                                  : "Animation " + std::to_string(a + 1)),
                                    duration, tracks.data(), static_cast<uint32_t>(tracks.size())});
            if (animation.mNumMorphMeshChannels || animation.mNumMeshChannels)
                s.warnings.push_back(s.text(
                    "Animation de morph/maillage detectee : seules les transformations et les os sont lus."));
        }
        s.scene.animation_count = static_cast<uint32_t>(s.animations.size());
        s.scene.animations = s.animations.data();
        s.scene.nodes = s.nodes.data();
        s.scene.node_count = static_cast<uint32_t>(s.nodes.size());
        s.scene.warnings = s.warnings.data();
        s.scene.warning_count = static_cast<uint32_t>(s.warnings.size());
        std::sort(opened.begin(), opened.end());
        opened.erase(std::unique(opened.begin(), opened.end()), opened.end());
        for (auto& file : opened)
            s.dependencies.push_back(s.text(std::move(file)));
        s.scene.dependencies = s.dependencies.data();
        s.scene.dependency_count = static_cast<uint32_t>(s.dependencies.size());
        s.scene.owner = storage.get();
        *output = &storage.release()->scene;
        if (host->progress)
            host->progress(host->context, 1.f);
        return 0;
    } catch (const std::exception& e) {
        if (error && error_size)
            std::snprintf(error, error_size, "%s", e.what());
    } catch (...) {
        if (error && error_size)
            std::snprintf(error, error_size, "Erreur interne d'import");
    }
    return host && cancelled(host) ? -2 : -1;
}
void release(Cy3DScene* scene) {
    if (scene)
        delete static_cast<Storage*>(scene->owner);
}
} // namespace
extern "C" CY3D_EXPORT const Cy3DPlugin* Cy3D_GetPlugin() {
    static const std::string extensions = [] {
        Assimp::Importer importer;
        aiString text;
        importer.GetExtensionList(text);
        std::string s = text.C_Str();
        size_t pos;
        while ((pos = s.find("*.")) != std::string::npos)
            s.erase(pos, 2);
        return s;
    }();
    static const Cy3DPlugin plugin{CY3D_API_VERSION, sizeof(Cy3DPlugin), "Assimp · formats 3D",
                                   "6.0.2",          extensions.c_str(), load,
                                   release};
    return &plugin;
}
