// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Cyberalien
#include "loader.h"
#include <chrono>
#include <cmath>
#include <stdexcept>
namespace cy {
namespace {
Scene::Stamp stamp(const fs::path& file) {
    Scene::Stamp result;
    result.file = file;
    std::error_code ec;
    result.modified = fs::last_write_time(file, ec);
    result.exists = !ec;
    if (result.exists) {
        result.size = fs::file_size(file, ec);
        if (ec)
            result.exists = false;
    }
    return result;
}
bool fresh(const Scene& scene) {
    if (!scene.reusable)
        return false;
    for (const auto& previous : scene.dependencies) {
        auto current = stamp(previous.file);
        if (current.exists != previous.exists || current.modified != previous.modified ||
            current.size != previous.size)
            return false;
    }
    return true;
}
} // namespace
void prepareScene(Scene& targetScene, const fs::path& file, const std::atomic<bool>& cancel,
                  uint64_t budget) {
    auto* scene = &targetScene;
    auto& s = *scene->data;
    if (s.struct_size < sizeof(Cy3DScene) || !s.mesh_count || !s.meshes || !s.instance_count ||
        !s.instances || !s.material_count || !s.materials || (s.texture_count && !s.textures))
        throw std::runtime_error("Scene de plugin invalide");
    if (s.memory_bytes > budget)
        throw std::runtime_error("Scene trop volumineuse (limite de 2 Gio)");
    if (s.node_count || s.animation_count)
        scene->animation = std::make_shared<AnimationData>(s);
    for (uint32_t i = 0; i < s.warning_count; ++i)
        if (s.warnings && s.warnings[i])
            scene->warnings.emplace_back(s.warnings[i]);
    scene->texture_usage.resize(s.texture_count);
    for (uint32_t i = 0; i < s.material_count; ++i) {
        const auto& m = s.materials[i];
        const float values[]{m.color[0],    m.color[1],     m.color[2],           m.color[3],
                             m.metallic,    m.roughness,    m.emissive[0],        m.emissive[1],
                             m.emissive[2], m.normal_scale, m.occlusion_strength, m.alpha_cutoff};
        for (float value : values)
            if (!std::isfinite(value))
                throw std::runtime_error("Materiau non fini");
        if (m.alpha_mode > CY3D_BLEND)
            throw std::runtime_error("Mode alpha invalide");
        for (unsigned slot = 0; slot < CY3D_MAP_COUNT; ++slot) {
            const auto& map = m.maps[slot];
            if (map.texture < -1 ||
                (map.texture >= 0 && static_cast<uint32_t>(map.texture) >= s.texture_count) ||
                map.uv_set > 1 || map.channel > 3 || map.wrap_u > CY3D_MIRROR || map.wrap_v > CY3D_MIRROR)
                throw std::runtime_error("Texture de materiau invalide");
            for (float value : map.transform)
                if (!std::isfinite(value))
                    throw std::runtime_error("Transformation UV non finie");
            if (map.texture >= 0)
                scene->texture_usage[map.texture] |= slot == CY3D_BASE_COLOR || slot == CY3D_EMISSIVE ? 2 : 1;
        }
    }
    for (uint32_t i = 0; i < s.mesh_count; ++i) {
        const auto& m = s.meshes[i];
        if (!m.vertices || !m.indices || !m.vertex_count || !m.index_count ||
            (m.topology == CY3D_TRIANGLES && m.index_count % 3) || m.topology > CY3D_SPLATS ||
            (m.topology == CY3D_SPLATS && (!m.splats || !m.colors)) || m.index_count > 0x7fffffff ||
            m.material >= s.material_count)
            throw std::runtime_error("Maillage de plugin invalide");
        for (uint32_t j = 0; j < m.index_count; ++j) {
            if ((j & 65535) == 0 && cancel)
                throw std::runtime_error("Chargement annule");
            if (m.indices[j] >= m.vertex_count)
                throw std::runtime_error("Indice de sommet invalide");
        }
        for (uint32_t j = 0; j < m.vertex_count; ++j) {
            if ((j & 65535) == 0 && cancel)
                throw std::runtime_error("Operation cancelled.");
            for (float coordinate : m.vertices[j].position)
                if (!std::isfinite(coordinate))
                    throw std::runtime_error("Non-finite vertex position");
            for (float coordinate : m.vertices[j].normal)
                if (!std::isfinite(coordinate))
                    throw std::runtime_error("Non-finite vertex normal");
            for (float coordinate : m.vertices[j].uv)
                if (!std::isfinite(coordinate))
                    throw std::runtime_error("Non-finite UV coordinate");
            for (float coordinate : m.vertices[j].uv1)
                if (!std::isfinite(coordinate))
                    throw std::runtime_error("Non-finite UV coordinate");
        }
        scene->vertices += m.vertex_count;
        scene->gpu_bytes += static_cast<uint64_t>(m.vertex_count) * sizeof(Cy3DVertex) +
                            static_cast<uint64_t>(m.index_count) * sizeof(uint32_t);
        if (m.colors)
            scene->gpu_bytes += static_cast<uint64_t>(m.vertex_count) * 16;
        if (m.skin) {
            if (!m.bones || !m.bone_count || !scene->animation)
                throw std::runtime_error("Squelette absent");
            for (uint32_t j = 0; j < m.bone_count; ++j) {
                if (m.bones[j].node >= s.node_count)
                    throw std::runtime_error("Os invalide");
                for (float value : m.bones[j].inverse_bind)
                    if (!std::isfinite(value))
                        throw std::runtime_error("Liaison d'os non finie");
            }
            for (uint32_t j = 0; j < m.vertex_count; ++j)
                for (int k = 0; k < 4; ++k)
                    if (m.skin[j].joints[k] >= m.bone_count || !std::isfinite(m.skin[j].weights[k]) ||
                        m.skin[j].weights[k] < 0)
                        throw std::runtime_error("Poids de squelette invalide");
            scene->gpu_bytes += static_cast<uint64_t>(m.vertex_count) * sizeof(Cy3DSkinVertex);
        }
        if (m.splats)
            scene->gpu_bytes += static_cast<uint64_t>(m.vertex_count) * sizeof(Cy3DSplat);
    }
    for (uint32_t i = 0; i < s.instance_count; ++i) {
        for (float coordinate : s.instances[i].transform)
            if (!std::isfinite(coordinate))
                throw std::runtime_error("Non-finite instance transform");
        if (s.instances[i].mesh >= s.mesh_count)
            throw std::runtime_error("Instance de plugin invalide");
        const auto& mesh = s.meshes[s.instances[i].mesh];
        if (s.instances[i].node < -1 ||
            (s.instances[i].node >= 0 && static_cast<uint32_t>(s.instances[i].node) >= s.node_count))
            throw std::runtime_error("Noeud d'instance invalide");
        if (mesh.topology == CY3D_TRIANGLES)
            scene->triangles += mesh.index_count / 3;
        else {
            scene->points += mesh.index_count;
            if (mesh.topology == CY3D_SPLATS)
                scene->splats += mesh.index_count;
        }
    }
    for (int i = 0; i < 3; ++i)
        if (!std::isfinite(s.bounds_min[i]) || !std::isfinite(s.bounds_max[i]) ||
            s.bounds_min[i] > s.bounds_max[i])
            throw std::runtime_error("Dimensions invalides");
    scene->memory = s.memory_bytes;
    if (scene->animation) {
        scene->memory += scene->animation->nodes.size() * sizeof(Cy3DNode);
        for (const auto& clip : scene->animation->clips) {
            scene->memory += sizeof(AnimationData::Clip) + clip.name.size();
            for (const auto& track : clip.tracks)
                scene->memory +=
                    sizeof(AnimationData::Track) +
                    (track.positions.size() + track.rotations.size() + track.scales.size()) * sizeof(Cy3DKey);
        }
    }
    decodeImages(*scene, file, cancel);
    if (cancel)
        throw std::runtime_error("Operation cancelled.");
    if (scene->memory > budget)
        throw std::runtime_error("Decoded scene exceeds its memory budget.");
    for (size_t i = 0; i < scene->images.size(); ++i)
        scene->gpu_bytes += scene->images[i].rgba.size() * (scene->texture_usage[i] == 3 ? 2 : 1);
}
std::shared_ptr<Scene> importScene(Plugins& plugins, const fs::path& file, std::atomic<bool>& cancel,
                                   std::atomic<float>& progress) {
    auto start = std::chrono::steady_clock::now();
    auto scene = std::make_shared<Scene>();
    scene->module = plugins.forFile(file);
    struct Context {
        std::atomic<bool>& cancel;
        std::atomic<float>& progress;
    } context{cancel, progress};
    Cy3DHost host{CY3D_API_VERSION,
                  sizeof(Cy3DHost),
                  &context,
                  [](void* p) { return static_cast<Context*>(p)->cancel.load() ? 1 : 0; },
                  [](void* p, float f) { static_cast<Context*>(p)->progress.store(std::clamp(f, 0.f, 1.f)); },
                  2ull * 1024 * 1024 * 1024};
    char error[2048]{};
    int code;
    std::vector<Module*> skipped;
    for (;;) {
        code = scene->module->api->load(utf8(file).c_str(), &host, &scene->data, error, sizeof(error));
        if (code != -3)
            break;
        if (scene->data) {
            scene->module->api->release(scene->data);
            scene->data = nullptr;
        }
        skipped.push_back(scene->module.get());
        scene->module = plugins.forFile(file, skipped);
    }
    if (code || !scene->data)
        throw std::runtime_error(cancel ? "Chargement annule" : (error[0] ? error : "Echec de l'import"));
    prepareScene(*scene, file, cancel, host.max_output_bytes);
    auto extension = file.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (extension == ".usd" || extension == ".usda" || extension == ".usdc" || extension == ".usdz")
        scene->reusable = false;
    auto& s = *scene->data;
    std::vector<fs::path> dependencies{file};
    for (uint32_t i = 0; i < s.dependency_count; ++i)
        if (s.dependencies && s.dependencies[i])
            dependencies.push_back(path(s.dependencies[i]));
    for (uint32_t i = 0; i < s.texture_count; ++i)
        if (s.textures[i].path)
            dependencies.push_back(path(s.textures[i].path));
    std::sort(dependencies.begin(), dependencies.end());
    dependencies.erase(std::unique(dependencies.begin(), dependencies.end()), dependencies.end());
    for (const auto& dependency : dependencies)
        scene->dependencies.push_back(stamp(dependency));
    scene->import_ms =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    return scene;
}
Loader::Loader(Plugins& p, std::function<void()> wake) : plugins_(p), wake_(std::move(wake)) {
    worker_ = std::thread([this] { run(); });
}
Loader::~Loader() {
    {
        std::lock_guard lock(mutex_);
        stop_ = true;
        if (cancel_)
            *cancel_ = true;
    }
    cv_.notify_one();
    worker_.join();
}
uint64_t Loader::request(const fs::path& file, std::vector<fs::path> neighbors) {
    std::lock_guard lock(mutex_);
    if (cancel_)
        *cancel_ = true;
    file_ = file;
    neighbors_ = std::move(neighbors);
    pending_ = true;
    result_.reset();
    progress = 0;
    prefetch_pending_ = false;
    prefetch_files_.clear();
    ++generation_;
    cv_.notify_one();
    return generation_;
}
void Loader::prefetch(std::vector<fs::path> neighbors) {
    std::lock_guard lock(mutex_);
    if (pending_) {
        neighbors_ = std::move(neighbors);
        return;
    }
    prefetch_files_ = std::move(neighbors);
    prefetch_pending_ = !prefetch_files_.empty();
    cv_.notify_one();
}
uint64_t Loader::cancel() {
    std::lock_guard lock(mutex_);
    if (cancel_)
        *cancel_ = true;
    pending_ = false;
    prefetch_pending_ = false;
    prefetch_files_.clear();
    result_.reset();
    progress = 0;
    return ++generation_;
}
std::unique_ptr<Result> Loader::take() {
    std::lock_guard lock(mutex_);
    return std::move(result_);
}
void Loader::run() {
    while (true) {
        fs::path file;
        std::vector<fs::path> neighbors;
        uint64_t generation;
        std::shared_ptr<std::atomic<bool>> cancel;
        bool foreground;
        {
            std::unique_lock lock(mutex_);
            cv_.wait(lock, [this] { return stop_ || pending_ || prefetch_pending_; });
            if (stop_)
                return;
            foreground = pending_;
            generation = generation_;
            if (foreground) {
                file = file_;
                neighbors = neighbors_;
                pending_ = false;
                prefetch_pending_ = false;
                prefetch_files_.clear();
            } else {
                neighbors = std::move(prefetch_files_);
                prefetch_pending_ = false;
            }
            cancel_ = std::make_shared<std::atomic<bool>>(false);
            cancel = cancel_;
        }
        auto load = [&](const fs::path& candidate, bool& cached) {
            std::error_code ec;
            auto modified = fs::last_write_time(candidate, ec);
            for (auto it = cache_.begin(); it != cache_.end();) {
                if (it->file == candidate) {
                    if (!ec && it->modified == modified && fresh(*it->scene)) {
                        cached = true;
                        auto scene = it->scene;
                        cache_.splice(cache_.begin(), cache_, it);
                        return scene;
                    }
                    cache_bytes_ -= it->scene->memory;
                    it = cache_.erase(it);
                    cache_usage = cache_bytes_;
                } else
                    ++it;
            }
            auto scene = importScene(plugins_, candidate, *cancel, progress);
            if (!*cancel && scene->reusable && scene->memory <= cache_limit) {
                while (!cache_.empty() && cache_bytes_ + scene->memory > cache_limit) {
                    cache_bytes_ -= cache_.back().scene->memory;
                    cache_.pop_back();
                }
                cache_.push_front({candidate, modified, scene});
                cache_bytes_ += scene->memory;
            }
            cache_usage = cache_bytes_;
            return scene;
        };
        if (foreground) {
            auto result = std::make_unique<Result>();
            result->file = file;
            result->generation = generation;
            try {
                result->scene = load(file, result->cached);
            } catch (const std::exception& e) {
                result->error = e.what();
            }
            if (*cancel)
                continue;
            {
                std::lock_guard lock(mutex_);
                if (stop_)
                    return;
                if (generation != generation_)
                    continue;
                result_ = std::move(result);
            }
            wake_();
        }
        // One worker: foreground always cancels speculative work. Never enqueue an entire folder.
        for (const auto& neighbor : neighbors) {
            if (*cancel)
                break;
            try {
                bool cached = false;
                load(neighbor, cached);
            } catch (...) {
            }
        }
    }
}
} // namespace cy
