// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Cyberalien
#include "cy3d_plugin.h"
#include "cy3d_mapped_file.hpp"
#include <algorithm>
#include <bit>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <limits>
#include <memory>
#include <string_view>
#include <vector>
namespace {
struct Model {
    Cy3DScene scene{};
    Cy3DMesh mesh{};
    Cy3DInstance instance{};
    Cy3DMaterial material = Cy3D_DefaultMaterial();
    std::vector<Cy3DVertex> vertices;
    std::vector<uint32_t> indices;
    Model() {
        material.name = "Surface";
        instance.node = -1;
        for (int i = 0; i < 3; ++i) {
            scene.bounds_min[i] = std::numeric_limits<float>::max();
            scene.bounds_max[i] = -std::numeric_limits<float>::max();
        }
        for (int i = 0; i < 4; ++i)
            instance.transform[i * 5] = 1;
    }
};
uint32_t u32(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
float number(const uint8_t* p) {
    return std::bit_cast<float>(u32(p));
}
bool cancelled(const Cy3DHost* host) {
    return host->is_cancelled && host->is_cancelled(host->context);
}
void triangle(Model& m, float points[3][3], const Cy3DHost* host) {
    if ((m.vertices.size() + 3) * (sizeof(Cy3DVertex) + 4) > host->max_output_bytes ||
        m.vertices.size() > 0x7fffffffu - 3)
        throw std::runtime_error("STL trop volumineux");
    float a[3], b[3], n[3];
    for (int k = 0; k < 3; ++k) {
        a[k] = points[1][k] - points[0][k];
        b[k] = points[2][k] - points[0][k];
    }
    n[0] = a[1] * b[2] - a[2] * b[1];
    n[1] = a[2] * b[0] - a[0] * b[2];
    n[2] = a[0] * b[1] - a[1] * b[0];
    float len = std::hypot(n[0], n[1], n[2]);
    for (float& value : n)
        value = len > 0 ? value / len : 0;
    for (int i = 0; i < 3; ++i) {
        Cy3DVertex v{};
        for (int k = 0; k < 3; ++k) {
            float p = points[i][k];
            if (!std::isfinite(p))
                throw std::runtime_error("Coordonnee STL invalide");
            v.position[k] = p;
            v.normal[k] = n[k];
            m.scene.bounds_min[k] = std::min(m.scene.bounds_min[k], p);
            m.scene.bounds_max[k] = std::max(m.scene.bounds_max[k], p);
        }
        m.indices.push_back(static_cast<uint32_t>(m.vertices.size()));
        m.vertices.push_back(v);
    }
}
std::string_view token(const char*& p, const char* end) {
    while (p < end && static_cast<unsigned char>(*p) <= 32)
        ++p;
    auto start = p;
    while (p < end && static_cast<unsigned char>(*p) > 32)
        ++p;
    return {start, static_cast<size_t>(p - start)};
}
int load(const char* filename, const Cy3DHost* host, Cy3DScene** output, char* error, size_t error_size) {
    if (output)
        *output = nullptr;
    try {
        if (!host || !output || host->api_version != CY3D_API_VERSION || host->struct_size < sizeof(Cy3DHost))
            throw std::runtime_error("ABI hote invalide");
        if (cancelled(host))
            return -2;
        std::string_view name(filename);
        cy::MappedFile file(std::filesystem::path(
            std::u8string_view(reinterpret_cast<const char8_t*>(name.data()), name.size())));
        auto model = std::make_unique<Model>();
        auto& m = *model;
        auto data = file.data();
        auto size = file.size();
        uint32_t count = size >= 84 ? u32(data + 80) : 0;
        if (size >= 84 && 84ull + static_cast<uint64_t>(count) * 50 == size) {
            uint64_t bytes = static_cast<uint64_t>(count) * 3 * (sizeof(Cy3DVertex) + 4);
            if (!count || bytes > host->max_output_bytes || static_cast<uint64_t>(count) * 3 > 0x7fffffffu)
                throw std::runtime_error("Nombre de triangles STL invalide");
            m.vertices.reserve(static_cast<size_t>(count) * 3);
            m.indices.reserve(static_cast<size_t>(count) * 3);
            for (uint32_t i = 0; i < count; ++i) {
                if ((i & 4095) == 0) {
                    if (cancelled(host))
                        return -2;
                    if (host->progress)
                        host->progress(host->context, static_cast<float>(i) / count);
                }
                float points[3][3];
                auto p = data + 84 + static_cast<uint64_t>(i) * 50 + 12;
                for (int j = 0; j < 3; ++j)
                    for (int k = 0; k < 3; ++k)
                        points[j][k] = number(p + (j * 3 + k) * 4);
                triangle(m, points, host);
            }
        } else {
            const char *p = reinterpret_cast<const char*>(data), *end = p + size;
            if (token(p, end) != "solid")
                throw std::runtime_error("STL binaire tronque ou invalide");
            float points[3][3];
            int index = 0;
            while (p < end) {
                auto word = token(p, end);
                if (word == "vertex") {
                    for (int k = 0; k < 3; ++k) {
                        auto value = token(p, end);
                        if (!value.empty() && value.front() == '+')
                            value.remove_prefix(1);
                        auto result =
                            std::from_chars(value.data(), value.data() + value.size(), points[index][k]);
                        if (result.ec != std::errc{} || result.ptr != value.data() + value.size())
                            throw std::runtime_error("Sommet STL invalide");
                    }
                    if (++index == 3) {
                        triangle(m, points, host);
                        index = 0;
                        if ((m.indices.size() % 12288) == 0) {
                            if (cancelled(host))
                                return -2;
                            if (host->progress)
                                host->progress(host->context,
                                               static_cast<float>(p - reinterpret_cast<const char*>(data)) /
                                                   static_cast<float>(size));
                        }
                    }
                }
            }
            if (index || m.vertices.empty())
                throw std::runtime_error("STL ASCII sans triangles complets");
        }
        m.mesh = {"STL",
                  m.vertices.data(),
                  m.indices.data(),
                  static_cast<uint32_t>(m.vertices.size()),
                  static_cast<uint32_t>(m.indices.size()),
                  0,
                  {}};
        m.scene.struct_size = sizeof(Cy3DScene);
        m.scene.meshes = &m.mesh;
        for (int i = 0; i < 3; ++i)
            m.mesh.center[i] = (m.scene.bounds_min[i] + m.scene.bounds_max[i]) * .5f;
        m.scene.mesh_count = 1;
        m.scene.instances = &m.instance;
        m.scene.instance_count = 1;
        m.scene.materials = &m.material;
        m.scene.material_count = 1;
        m.scene.memory_bytes =
            m.vertices.capacity() * sizeof(Cy3DVertex) + m.indices.capacity() * 4 + sizeof(Model);
        m.scene.owner = model.get();
        *output = &model.release()->scene;
        if (host->progress)
            host->progress(host->context, 1);
        return 0;
    } catch (const std::exception& e) {
        if (error && error_size)
            std::snprintf(error, error_size, "%s", e.what());
    } catch (...) {
        if (error && error_size)
            std::snprintf(error, error_size, "Erreur STL");
    }
    return host && cancelled(host) ? -2 : -1;
}
void release(Cy3DScene* scene) {
    if (scene)
        delete static_cast<Model*>(scene->owner);
}
} // namespace
extern "C" CY3D_EXPORT const Cy3DPlugin* Cy3D_GetPlugin() {
    static const Cy3DPlugin plugin{
        CY3D_API_VERSION, sizeof(Cy3DPlugin), "STL natif rapide", "1.0.0", "stl", load, release};
    return &plugin;
}
