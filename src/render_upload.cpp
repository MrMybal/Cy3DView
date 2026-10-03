// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Cyberalien
#include "renderer.h"
#include <chrono>
#include <stdexcept>
namespace cy {
Renderer::GpuScene::~GpuScene() {
    for (const auto& mesh : meshes) {
        if (mesh.vao)
            glDeleteVertexArrays(1, &mesh.vao);
        if (mesh.vbo)
            glDeleteBuffers(1, &mesh.vbo);
        if (mesh.ebo)
            glDeleteBuffers(1, &mesh.ebo);
        if (mesh.palette_buffer)
            glDeleteBuffers(1, &mesh.palette_buffer);
        if (mesh.palette_texture)
            glDeleteTextures(1, &mesh.palette_texture);
    }
    for (auto texture : textures)
        if (texture)
            glDeleteTextures(1, &texture);
}
void Renderer::cancelUpload() {
    pending_.reset();
    glBindVertexArray(0);
}
void Renderer::setGpuCacheLimit(uint64_t bytes) {
    cache_limit_ = bytes;
    while (!cache_.empty() && cache_bytes_ > bytes) {
        cache_bytes_ -= cache_.back()->cacheBytes();
        cache_.pop_back();
    }
}
void Renderer::remember(const std::shared_ptr<GpuScene>& scene) {
    if (scene->cacheBytes() > cache_limit_)
        return;
    while (!cache_.empty() && cache_bytes_ + scene->cacheBytes() > cache_limit_) {
        cache_bytes_ -= cache_.back()->cacheBytes();
        cache_.pop_back();
    }
    cache_.push_front(scene);
    cache_bytes_ += scene->cacheBytes();
}
void Renderer::activate(std::shared_ptr<GpuScene> scene) {
    active_ = std::move(scene);
    gpu_bytes = active_->bytes;
    animation_time = 0;
    animation_clip = 0;
    active_->pose_time = -1e300;
    active_->pose_clip = -999;
    camera.fit(active_->minimum.data(), active_->maximum.data(),
               width_ > 0 ? static_cast<float>(width_) / height_ : 1.5f);
}
bool Renderer::beginUpload(std::shared_ptr<Scene> scene) {
    cancelUpload();
    last_upload_cached = false;
    if (!scene || !scene->data)
        throw std::runtime_error("Scene GPU absente");
    // Scene identity changes after a file or one of its dependencies changes.
    // Weak references avoid retaining the CPU geometry just for the GPU cache.
    for (auto it = cache_.begin(); it != cache_.end();) {
        auto source = (*it)->source.lock();
        if (!source) {
            cache_bytes_ -= (*it)->cacheBytes();
            it = cache_.erase(it);
            continue;
        }
        if (source == scene) {
            auto asset = *it;
            cache_.splice(cache_.begin(), cache_, it);
            activate(std::move(asset));
            last_upload_cached = true;
            ++cache_hits;
            return true;
        }
        ++it;
    }
    if (active_ && active_->source.lock() == scene) {
        last_upload_cached = true;
        ++cache_hits;
        return true;
    }
    auto upload = std::make_unique<Upload>();
    upload->source = std::move(scene);
    upload->target = std::make_shared<GpuScene>();
    auto& data = *upload->source->data;
    auto& target = *upload->target;
    target.source = upload->source;
    target.animation = upload->source->animation;
    target.meshes.resize(data.mesh_count);
    target.textures.resize(static_cast<size_t>(data.texture_count) * 2);
    target.materials.reserve(data.material_count);
    target.instances.reserve(data.instance_count);
    std::copy_n(data.bounds_min, 3, target.minimum.begin());
    std::copy_n(data.bounds_max, 3, target.maximum.begin());
    upload->total = upload->source->gpu_bytes +
                    static_cast<uint64_t>(data.instance_count) * sizeof(Instance) +
                    static_cast<uint64_t>(data.material_count) * sizeof(Cy3DMaterial);
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &upload->max_texture_size);
    pending_ = std::move(upload);
    return false;
}
float Renderer::uploadProgress() const {
    return pending_ ? static_cast<float>(static_cast<double>(pending_->transferred) /
                                         std::max<uint64_t>(pending_->total, 1))
                    : 1.f;
}
uint64_t Renderer::uploadStep() {
    constexpr uint64_t chunk = 1024ull * 1024;
    auto& upload = *pending_;
    auto& source = *upload.source->data;
    auto& target = *upload.target;
    if (upload.phase == Upload::Phase::Meshes) {
        if (upload.mesh == source.mesh_count) {
            upload.phase = Upload::Phase::Textures;
            return 0;
        }
        const auto& mesh = source.meshes[upload.mesh];
        auto& gpu = target.meshes[upload.mesh];
        uint64_t basic = static_cast<uint64_t>(mesh.vertex_count) * sizeof(Cy3DVertex),
                 colors = mesh.colors ? static_cast<uint64_t>(mesh.vertex_count) * 16 : 0,
                 skin = mesh.skin ? static_cast<uint64_t>(mesh.vertex_count) * sizeof(Cy3DSkinVertex) : 0,
                 splats = mesh.splats ? static_cast<uint64_t>(mesh.vertex_count) * sizeof(Cy3DSplat) : 0,
                 vertices = basic + colors + skin + splats,
                 indices = static_cast<uint64_t>(mesh.index_count) * 4;
        if (!gpu.vao) {
            gpu.count = static_cast<GLsizei>(mesh.index_count);
            gpu.material = mesh.material;
            gpu.topology = mesh.topology;
            gpu.colors = mesh.colors != nullptr;
            gpu.center = {mesh.center[0], mesh.center[1], mesh.center[2]};
            if (mesh.bone_count) {
                GLint maximum;
                glGetIntegerv(GL_MAX_TEXTURE_BUFFER_SIZE, &maximum);
                if (static_cast<uint64_t>(mesh.bone_count) * 4 > static_cast<uint64_t>(maximum))
                    throw std::runtime_error("Squelette trop grand pour ce GPU");
                gpu.bones.assign(mesh.bones, mesh.bones + mesh.bone_count);
                glGenBuffers(1, &gpu.palette_buffer);
                glBindBuffer(GL_TEXTURE_BUFFER, gpu.palette_buffer);
                glBufferData(GL_TEXTURE_BUFFER, static_cast<GLsizeiptr>(mesh.bone_count) * 64, nullptr,
                             GL_DYNAMIC_DRAW);
                glGenTextures(1, &gpu.palette_texture);
                glBindTexture(GL_TEXTURE_BUFFER, gpu.palette_texture);
                glTexBuffer(GL_TEXTURE_BUFFER, GL_RGBA32F, gpu.palette_buffer);
                target.bytes += static_cast<uint64_t>(mesh.bone_count) * 64;
            }
            if (mesh.topology == CY3D_SPLATS) {
                gpu.splat_positions.reserve(mesh.vertex_count);
                target.metadata_bytes += static_cast<uint64_t>(mesh.vertex_count) * 20;
            }
            glGenVertexArrays(1, &gpu.vao);
            glGenBuffers(1, &gpu.vbo);
            glGenBuffers(1, &gpu.ebo);
            glBindVertexArray(gpu.vao);
            glBindBuffer(GL_ARRAY_BUFFER, gpu.vbo);
            glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices), nullptr, GL_STATIC_DRAW);
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gpu.ebo);
            glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(indices), nullptr, GL_STATIC_DRAW);
            for (GLuint attr = 0; attr < 4; ++attr) {
                glEnableVertexAttribArray(attr);
                glVertexAttribPointer(attr, attr >= 2 ? 2 : 3, GL_FLOAT, GL_FALSE, sizeof(Cy3DVertex),
                                      reinterpret_cast<void*>(static_cast<uintptr_t>(attr == 0   ? 0
                                                                                     : attr == 1 ? 12
                                                                                     : attr == 2 ? 24
                                                                                                 : 32)));
            }
            if (colors) {
                glEnableVertexAttribArray(4);
                glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, 16,
                                      reinterpret_cast<void*>(static_cast<uintptr_t>(basic)));
            }
            if (skin) {
                glEnableVertexAttribArray(5);
                glVertexAttribIPointer(5, 4, GL_UNSIGNED_INT, sizeof(Cy3DSkinVertex),
                                       reinterpret_cast<void*>(static_cast<uintptr_t>(basic + colors)));
                glEnableVertexAttribArray(6);
                glVertexAttribPointer(6, 4, GL_FLOAT, GL_FALSE, sizeof(Cy3DSkinVertex),
                                      reinterpret_cast<void*>(static_cast<uintptr_t>(basic + colors + 16)));
            }
            if (splats) {
                glEnableVertexAttribArray(7);
                glVertexAttribPointer(7, 3, GL_FLOAT, GL_FALSE, sizeof(Cy3DSplat),
                                      reinterpret_cast<void*>(static_cast<uintptr_t>(basic + colors + skin)));
                glEnableVertexAttribArray(8);
                glVertexAttribPointer(
                    8, 4, GL_FLOAT, GL_FALSE, sizeof(Cy3DSplat),
                    reinterpret_cast<void*>(static_cast<uintptr_t>(basic + colors + skin + 12)));
            }
            target.bytes += vertices + indices;
            return 0; // yieldable allocation step, separate from the first transfer
        }
        glBindVertexArray(gpu.vao);
        bool vertex = upload.offset < vertices;
        uint64_t offset = vertex ? upload.offset : upload.offset - vertices;
        uint64_t boundary = indices;
        const uint8_t* data = reinterpret_cast<const uint8_t*>(mesh.indices);
        uint64_t local = offset;
        if (vertex) {
            if (offset < basic) {
                boundary = basic;
                data = reinterpret_cast<const uint8_t*>(mesh.vertices);
            } else if (offset < basic + colors) {
                local = offset - basic;
                boundary = colors;
                data = reinterpret_cast<const uint8_t*>(mesh.colors);
            } else if (offset < basic + colors + skin) {
                local = offset - basic - colors;
                boundary = skin;
                data = reinterpret_cast<const uint8_t*>(mesh.skin);
            } else {
                local = offset - basic - colors - skin;
                boundary = splats;
                data = reinterpret_cast<const uint8_t*>(mesh.splats);
            }
        }
        uint64_t bytes = std::min(chunk, boundary - local);
        if (vertex && offset < basic && mesh.topology == CY3D_SPLATS) {
            uint64_t first = offset / sizeof(Cy3DVertex), end = (offset + bytes) / sizeof(Cy3DVertex);
            for (uint64_t i = first; i < end; ++i)
                gpu.splat_positions.push_back({mesh.vertices[i].position[0], mesh.vertices[i].position[1],
                                               mesh.vertices[i].position[2]});
        }
        GLenum buffer = vertex ? GL_ARRAY_BUFFER : GL_ELEMENT_ARRAY_BUFFER;
        glBindBuffer(buffer, vertex ? gpu.vbo : gpu.ebo);
        glBufferSubData(buffer, static_cast<GLintptr>(offset), static_cast<GLsizeiptr>(bytes), data + local);
        upload.offset += bytes;
        if (upload.offset == vertices + indices) {
            ++upload.mesh;
            upload.offset = 0;
        }
        return bytes;
    }
    if (upload.phase == Upload::Phase::Textures) {
        if (upload.texture == target.textures.size()) {
            upload.phase = Upload::Phase::Materials;
            return 0;
        }
        auto index = upload.texture % source.texture_count;
        bool srgb = upload.texture >= source.texture_count;
        const auto& image = upload.source->images[index];
        auto& texture = target.textures[upload.texture];
        if (!(upload.source->texture_usage[index] & (srgb ? 2 : 1))) {
            ++upload.texture;
            return 0;
        }
        if (image.rgba.empty() || image.width > upload.max_texture_size ||
            image.height > upload.max_texture_size) {
            ++upload.texture;
            return image.rgba.size();
        }
        if (!texture) {
            glGenTextures(1, &texture);
            glBindTexture(GL_TEXTURE_2D, texture);
            glTexImage2D(GL_TEXTURE_2D, 0, srgb ? GL_SRGB8_ALPHA8 : GL_RGBA8, image.width, image.height, 0,
                         GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            target.bytes += image.rgba.size();
            return 0;
        }
        glBindTexture(GL_TEXTURE_2D, texture);
        if (upload.texture_row == image.height) {
            glGenerateMipmap(GL_TEXTURE_2D);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
            target.bytes += image.rgba.size() / 3;
            ++upload.texture;
            upload.texture_row = 0;
            return 0;
        }
        uint64_t stride = static_cast<uint64_t>(image.width) * 4;
        int rows = std::min(image.height - upload.texture_row,
                            static_cast<int>(std::max<uint64_t>(1, chunk / stride)));
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, upload.texture_row, image.width, rows, GL_RGBA, GL_UNSIGNED_BYTE,
                        image.rgba.data() + stride * upload.texture_row);
        upload.texture_row += rows;
        return stride * rows;
    }
    if (upload.phase == Upload::Phase::Materials) {
        uint32_t end = std::min(upload.material + 1024, source.material_count), count = end - upload.material;
        for (; upload.material < end; ++upload.material) {
            auto material = source.materials[upload.material];
            material.name = nullptr;
            target.materials.push_back(material);
        }
        if (upload.material == source.material_count)
            upload.phase = Upload::Phase::Instances;
        return static_cast<uint64_t>(count) * sizeof(Cy3DMaterial);
    }
    if (upload.phase == Upload::Phase::Instances) {
        uint32_t end = std::min(upload.instance + 256, source.instance_count), count = end - upload.instance;
        for (; upload.instance < end; ++upload.instance) {
            Instance instance;
            instance.data = source.instances[upload.instance];
            const float* m = instance.data.transform;
            Vec3 a{m[0], m[1], m[2]}, b{m[4], m[5], m[6]}, c{m[8], m[9], m[10]};
            auto x = cross(b, c), y = cross(c, a), z = cross(a, b);
            float determinant = dot(a, x);
            if (std::abs(determinant) > 1e-20f) {
                x = x * (1 / determinant);
                y = y * (1 / determinant);
                z = z * (1 / determinant);
                instance.normals = {x.x, x.y, x.z, y.x, y.y, y.z, z.x, z.y, z.z};
            } else
                instance.normals = {1, 0, 0, 0, 1, 0, 0, 0, 1};
            instance.winding = determinant < 0 ? GL_CW : GL_CCW;
            const auto& mesh = source.meshes[instance.data.mesh];
            instance.center =
                a * mesh.center[0] + b * mesh.center[1] + c * mesh.center[2] + Vec3{m[12], m[13], m[14]};
            if (source.materials[mesh.material].alpha_mode == CY3D_BLEND || mesh.topology == CY3D_SPLATS)
                target.transparent.push_back(upload.instance);
            else
                target.opaque.push_back(upload.instance);
            target.instances.push_back(instance);
        }
        if (upload.instance == source.instance_count)
            upload.phase = Upload::Phase::Done;
        return static_cast<uint64_t>(count) * sizeof(Instance);
    }
    return 0;
}
bool Renderer::advanceUpload(double milliseconds, uint64_t byte_budget) {
    if (!pending_)
        return true;
    auto start = std::chrono::steady_clock::now();
    uint64_t bytes = 0;
    unsigned steps = 0;
    try {
        do {
            auto transferred = uploadStep();
            pending_->transferred += transferred;
            bytes += transferred;
            ++steps;
            if (glGetError() != GL_NO_ERROR)
                throw std::runtime_error("Le GPU n'a pas pu charger le modele");
            if (pending_->phase == Upload::Phase::Done) {
                auto target = pending_->target;
                pending_.reset();
                remember(target);
                activate(std::move(target));
                glBindVertexArray(0);
                return true;
            }
        } while (bytes < std::max<uint64_t>(byte_budget, 1) && steps < 256 &&
                 std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count() <
                     milliseconds);
        glBindVertexArray(0);
        return false;
    } catch (...) {
        cancelUpload();
        throw;
    }
}
void Renderer::upload(std::shared_ptr<Scene> scene) {
    beginUpload(std::move(scene));
    while (!advanceUpload()) {
    }
}
} // namespace cy
