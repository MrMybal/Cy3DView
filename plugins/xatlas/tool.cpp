// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Cyberalien
#include "cy3d_extension.h"
#include "cy3d_scene_copy.hpp"
#include <xatlas.h>
#include <memory>
#include <cstring>
#include <cstdio>
#include <cmath>
namespace {
struct Settings {
    int resolution = 1024, padding = 4, channel = 1, quality = 2;
};
const Cy3DTool tools[]{{"unwrap", "UV atlas (xatlas)", "Atlas UV (xatlas)"}};
void* create(const char* id) {
    try {
        return id && std::strcmp(id, "unwrap") == 0 ? new Settings : nullptr;
    } catch (...) {
        return nullptr;
    }
}
void destroy(void* state) {
    delete static_cast<Settings*>(state);
}
void draw(const char*, void* p, const Cy3DToolUI* ui, const Cy3DScene* scene) {
    auto& s = *static_cast<Settings*>(p);
    bool fr = ui->language == 1;
    ui->text(ui->context,
             fr ? "Crée un atlas UV sans modifier la géométrie. UV1 conserve les textures utilisant UV0."
                : "Build a UV atlas without changing geometry. UV1 preserves textures using UV0.");
    const char* resolutions[]{"256", "512", "1024", "2048", "4096"};
    int index = 0;
    for (int n = s.resolution; n > 256; n /= 2)
        ++index;
    ui->combo(ui->context, fr ? "Résolution" : "Resolution", &index, resolutions, 5);
    s.resolution = 256 << index;
    ui->slider_int(ui->context, fr ? "Marge (pixels)" : "Padding (pixels)", &s.padding, 0, 32);
    ui->slider_int(ui->context, fr ? "Qualité" : "Quality", &s.quality, 1, 5);
    const char* channels[]{"UV0", "UV1"};
    ui->combo(ui->context, fr ? "Canal UV" : "UV channel", &s.channel, channels, 2);
    if (scene) {
        bool mapped = false;
        for (uint32_t m = 0; m < scene->material_count; ++m)
            for (const auto& map : scene->materials[m].maps)
                if (map.texture >= 0 && map.uv_set == static_cast<uint32_t>(s.channel))
                    mapped = true;
        if (mapped)
            ui->text(ui->context, fr ? "Attention : ce canal est utilisé par des textures. Choisir l'autre "
                                       "canal évite de changer leur apparence."
                                     : "Warning: textures use this channel. Choose the other channel to "
                                       "preserve their appearance.");
        ui->uv_preview(ui->context, scene, static_cast<uint32_t>(s.channel));
    }
}
void cancelled(const Cy3DHost* host) {
    if (host->is_cancelled && host->is_cancelled(host->context))
        throw std::runtime_error("Operation cancelled.");
}
int run(const char* id, const void* p, const Cy3DScene* input, const Cy3DHost* host, Cy3DScene** output,
        char* error, size_t size) {
    if (output)
        *output = nullptr;
    try {
        if (!id || std::strcmp(id, "unwrap") || !p || !input || !host || !output ||
            host->api_version != CY3D_API_VERSION || host->struct_size < sizeof(Cy3DHost))
            throw std::runtime_error("Invalid xatlas arguments.");
        const auto& s = *static_cast<const Settings*>(p);
        if (s.channel < 0 || s.channel > 1 || s.resolution < 256 || s.resolution > 4096 || s.padding < 0 ||
            s.padding > 32)
            throw std::runtime_error("Invalid UV atlas settings.");
        if (input->memory_bytes > host->max_output_bytes / 3)
            throw std::runtime_error("UV atlas needs more working memory than the current budget permits.");
        std::unique_ptr<xatlas::Atlas, decltype(&xatlas::Destroy)> atlas(xatlas::Create(), xatlas::Destroy);
        if (!atlas)
            throw std::bad_alloc();
        xatlas::SetProgressCallback(
            atlas.get(),
            [](xatlas::ProgressCategory category, int percent, void* context) {
                auto* h = static_cast<const Cy3DHost*>(context);
                if (h->progress)
                    h->progress(h->context,
                                .05f + .75f * (static_cast<float>(category) * 100 + percent) / 400.f);
                return !h->is_cancelled || !h->is_cancelled(h->context);
            },
            const_cast<Cy3DHost*>(host));
        for (uint32_t i = 0; i < input->mesh_count; ++i) {
            cancelled(host);
            const auto& mesh = input->meshes[i];
            if (mesh.topology != CY3D_TRIANGLES)
                throw std::runtime_error("UV atlas generation requires triangle meshes.");
            xatlas::MeshDecl decl;
            decl.vertexCount = mesh.vertex_count;
            decl.vertexPositionData = mesh.vertices[0].position;
            decl.vertexPositionStride = sizeof(Cy3DVertex);
            decl.vertexNormalData = mesh.vertices[0].normal;
            decl.vertexNormalStride = sizeof(Cy3DVertex);
            decl.indexData = mesh.indices;
            decl.indexCount = mesh.index_count;
            decl.indexFormat = xatlas::IndexFormat::UInt32;
            auto result = xatlas::AddMesh(atlas.get(), decl, input->mesh_count);
            if (result != xatlas::AddMeshError::Success)
                throw std::runtime_error(std::string("xatlas: ") + xatlas::StringForEnum(result));
        }
        xatlas::ChartOptions charts;
        charts.maxIterations = static_cast<uint32_t>(std::clamp(s.quality, 1, 5));
        charts.fixWinding = true;
        xatlas::PackOptions pack;
        pack.resolution = static_cast<uint32_t>(s.resolution);
        pack.padding = static_cast<uint32_t>(s.padding);
        xatlas::Generate(atlas.get(), charts, pack);
        cancelled(host);
        if (atlas->meshCount != input->mesh_count || atlas->atlasCount != 1 || !atlas->width ||
            !atlas->height)
            throw std::runtime_error(
                "xatlas could not pack a single atlas. Try a larger resolution or smaller padding.");
        uint64_t bytes = input->memory_bytes;
        for (uint32_t i = 0; i < input->mesh_count; ++i)
            bytes +=
                uint64_t(atlas->meshes[i].vertexCount) * (sizeof(Cy3DVertex) + sizeof(Cy3DSkinVertex) + 16);
        if (bytes > host->max_output_bytes)
            throw std::runtime_error("UV atlas result exceeds the scene memory budget.");
        auto result = std::make_unique<cy3d::SceneCopy>(*input);
        for (uint32_t i = 0; i < input->mesh_count; ++i) {
            cancelled(host);
            const auto& mesh = input->meshes[i];
            const auto& generated = atlas->meshes[i];
            if (generated.indexCount != mesh.index_count)
                throw std::runtime_error("xatlas changed the face count.");
            auto& verts = result->vertices[i];
            verts.resize(generated.vertexCount);
            if (mesh.colors)
                result->colors[i].resize(size_t(generated.vertexCount) * 4);
            if (mesh.skin)
                result->skin[i].resize(generated.vertexCount);
            for (uint32_t j = 0; j < generated.vertexCount; ++j) {
                if ((j & 65535) == 0)
                    cancelled(host);
                const auto& v = generated.vertexArray[j];
                if (v.xref >= mesh.vertex_count || v.atlasIndex != 0 || !std::isfinite(v.uv[0]) ||
                    !std::isfinite(v.uv[1]))
                    throw std::runtime_error(
                        "xatlas returned an uncharted or invalid vertex (check degenerate faces).");
                verts[j] = mesh.vertices[v.xref];
                auto* uv = s.channel ? verts[j].uv1 : verts[j].uv;
                uv[0] = v.uv[0] / atlas->width;
                uv[1] = v.uv[1] / atlas->height;
                if (mesh.colors)
                    std::copy_n(mesh.colors + size_t(v.xref) * 4, 4,
                                result->colors[i].data() + size_t(j) * 4);
                if (mesh.skin)
                    result->skin[i][j] = mesh.skin[v.xref];
            }
            result->indices[i].assign(generated.indexArray, generated.indexArray + generated.indexCount);
        }
        result->bind();
        if (result->scene.memory_bytes > host->max_output_bytes)
            throw std::runtime_error("UV result exceeds memory budget.");
        *output = &result.release()->scene;
        if (host->progress)
            host->progress(host->context, 1);
        return 0;
    } catch (const std::exception& e) {
        if (size)
            std::snprintf(error, size, "%s", e.what());
        return host && host->is_cancelled && host->is_cancelled(host->context) ? -2 : -1;
    } catch (...) {
        if (size)
            std::snprintf(error, size, "Unexpected xatlas failure.");
        return -1;
    }
}
void release(Cy3DScene* s) {
    if (s)
        delete static_cast<cy3d::SceneCopy*>(s->owner);
}
const Cy3DExtension api{CY3D_EXTENSION_VERSION,
                        sizeof(Cy3DExtension),
                        "xatlas UV editor",
                        "1.0",
                        nullptr,
                        0,
                        nullptr,
                        tools,
                        1,
                        create,
                        destroy,
                        draw,
                        run,
                        release};
} // namespace
extern "C" CY3D_EXPORT const Cy3DExtension* Cy3D_GetExtension() {
    return &api;
}
