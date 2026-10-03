// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Cyberalien
#pragma once
#include "cy3d_plugin.h"
#include <algorithm>
#include <deque>
#include <string>
#include <vector>
#include <stdexcept>
namespace cy3d {
// Plugin-local owning copy; storage and destruction stay inside the creating module.
struct SceneCopy {
    Cy3DScene scene{};
    std::deque<std::string> strings;
    std::vector<Cy3DMesh> meshes;
    std::vector<std::vector<Cy3DVertex>> vertices;
    std::vector<std::vector<uint32_t>> indices;
    std::vector<std::vector<float>> colors;
    std::vector<std::vector<Cy3DSkinVertex>> skin;
    std::vector<std::vector<Cy3DBone>> bones;
    std::vector<std::vector<Cy3DSplat>> splats;
    std::vector<Cy3DInstance> instances;
    std::vector<Cy3DMaterial> materials;
    std::vector<Cy3DTexture> textures;
    std::vector<std::vector<uint8_t>> texture_bytes;
    std::vector<Cy3DNode> nodes;
    std::vector<Cy3DAnimation> animations;
    std::deque<std::vector<Cy3DTrack>> tracks;
    std::deque<std::vector<Cy3DKey>> keys;
    std::vector<const char*> warnings;
    const char* text(const char* value) {
        if (!value)
            return nullptr;
        strings.emplace_back(value);
        return strings.back().c_str();
    }
    explicit SceneCopy(const Cy3DScene& input) {
        scene = input;
        scene.owner = this;
        scene.dependencies = nullptr;
        scene.dependency_count = 0;
        meshes.resize(input.mesh_count);
        vertices.resize(input.mesh_count);
        indices.resize(input.mesh_count);
        colors.resize(input.mesh_count);
        skin.resize(input.mesh_count);
        bones.resize(input.mesh_count);
        splats.resize(input.mesh_count);
        for (uint32_t i = 0; i < input.mesh_count; ++i) {
            const auto& m = input.meshes[i];
            meshes[i] = m;
            meshes[i].name = text(m.name);
            vertices[i].assign(m.vertices, m.vertices + m.vertex_count);
            indices[i].assign(m.indices, m.indices + m.index_count);
            if (m.colors)
                colors[i].assign(m.colors, m.colors + size_t(m.vertex_count) * 4);
            if (m.skin)
                skin[i].assign(m.skin, m.skin + m.vertex_count);
            if (m.bones)
                bones[i].assign(m.bones, m.bones + m.bone_count);
            if (m.splats)
                splats[i].assign(m.splats, m.splats + m.vertex_count);
        }
        instances.assign(input.instances, input.instances + input.instance_count);
        materials.assign(input.materials, input.materials + input.material_count);
        for (auto& m : materials)
            m.name = text(m.name);
        textures.resize(input.texture_count);
        texture_bytes.resize(input.texture_count);
        for (uint32_t i = 0; i < input.texture_count; ++i) {
            textures[i] = input.textures[i];
            textures[i].path = text(textures[i].path);
            if (textures[i].bytes && textures[i].byte_count)
                texture_bytes[i].assign(textures[i].bytes, textures[i].bytes + textures[i].byte_count);
        }
        if (input.node_count)
            nodes.assign(input.nodes, input.nodes + input.node_count);
        for (uint32_t i = 0; i < input.animation_count; ++i) {
            auto animation = input.animations[i];
            animation.name = text(animation.name);
            tracks.emplace_back();
            if (animation.track_count)
                tracks.back().assign(animation.tracks, animation.tracks + animation.track_count);
            for (auto& track : tracks.back()) {
                auto copy = [&](const Cy3DKey*& data, uint32_t count) {
                    if (!count) {
                        data = nullptr;
                        return;
                    }
                    keys.emplace_back(data, data + count);
                    data = keys.back().data();
                };
                copy(track.positions, track.position_count);
                copy(track.rotations, track.rotation_count);
                copy(track.scales, track.scale_count);
            }
            animation.tracks = tracks.back().data();
            animations.push_back(animation);
        }
        for (uint32_t i = 0; i < input.warning_count; ++i)
            if (input.warnings && input.warnings[i])
                warnings.push_back(text(input.warnings[i]));
        bind();
    }
    void bind() {
        uint64_t bytes = sizeof(SceneCopy);
        for (size_t i = 0; i < meshes.size(); ++i) {
            auto& m = meshes[i];
            m.vertices = vertices[i].data();
            m.indices = indices[i].data();
            m.vertex_count = static_cast<uint32_t>(vertices[i].size());
            m.index_count = static_cast<uint32_t>(indices[i].size());
            m.colors = colors[i].empty() ? nullptr : colors[i].data();
            m.skin = skin[i].empty() ? nullptr : skin[i].data();
            m.bones = bones[i].empty() ? nullptr : bones[i].data();
            m.splats = splats[i].empty() ? nullptr : splats[i].data();
            bytes += vertices[i].size() * sizeof(Cy3DVertex) + indices[i].size() * 4 + colors[i].size() * 4 +
                     skin[i].size() * sizeof(Cy3DSkinVertex) + bones[i].size() * sizeof(Cy3DBone) +
                     splats[i].size() * sizeof(Cy3DSplat);
        }
        for (size_t i = 0; i < textures.size(); ++i) {
            textures[i].bytes = texture_bytes[i].empty() ? nullptr : texture_bytes[i].data();
            bytes += texture_bytes[i].size();
        }
        for (auto& list : keys)
            bytes += list.size() * sizeof(Cy3DKey);
        for (auto& list : tracks)
            bytes += list.size() * sizeof(Cy3DTrack);
        for (auto& value : strings)
            bytes += value.size() + 1;
        bytes += meshes.size() * sizeof(Cy3DMesh) + instances.size() * sizeof(Cy3DInstance) +
                 materials.size() * sizeof(Cy3DMaterial) + nodes.size() * sizeof(Cy3DNode) +
                 textures.size() * sizeof(Cy3DTexture) + animations.size() * sizeof(Cy3DAnimation);
        scene.meshes = meshes.data();
        scene.instances = instances.data();
        scene.materials = materials.data();
        scene.textures = textures.data();
        scene.nodes = nodes.data();
        scene.animations = animations.data();
        scene.warnings = warnings.data();
        scene.warning_count = static_cast<uint32_t>(warnings.size());
        scene.memory_bytes = bytes;
    }
};
} // namespace cy3d
