// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Cyberalien
#include "extensions.h"
#include <iostream>
#include <fstream>
#include <cmath>
#include <cstring>
#include <stdexcept>
using namespace cy;
namespace {
void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
std::string contents(const fs::path& f) {
    std::ifstream in(f, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}
} // namespace
int main(int argc, char** argv) {
    if (argc != 4)
        return 1;
    try {
        auto pluginDir = fs::absolute(path(argv[1])), fixtures = fs::absolute(path(argv[2]));
        auto dir = fs::absolute(path(argv[3])) / path("extension-tests-épreuve");
        fs::create_directories(dir);
        Plugins plugins(pluginDir);
        Extensions extensions(pluginDir);
        require(extensions.formats().size() == 7 && extensions.tools().size() == 1,
                "Extension discovery failed.");
        std::atomic<bool> cancel{false};
        std::atomic<float> progress{0};
        auto load = [&](const fs::path& f) { return importScene(plugins, f, cancel, progress); };
        auto action = [&](const char* id) {
            for (const auto& f : extensions.formats())
                if (f.id == id)
                    return f;
            throw std::runtime_error("Missing export action.");
        };
        auto cube = load(fixtures / "cube.obj");
        auto original = contents(fixtures / "cube.obj");
        auto convert = [&](const char* id, const std::shared_ptr<Scene>& input, const fs::path& source,
                           const char* name) {
            std::cout << "Export " << id << " " << name << std::endl;
            OperationControl control;
            auto a = action(id);
            auto file = dir / (std::string(name) + "-" + id + "." + a.extension);
            auto bundle = exportScene(a, input, file, source, control);
            bundle->commit(true);
            require(control.progress == 1, "Export progress incomplete.");
            auto result = load(file);
            require(result->triangles == input->triangles, "Round-trip triangle count changed.");
            for (int i = 0; i < 3; ++i)
                require(std::abs(result->data->bounds_min[i] - input->data->bounds_min[i]) < .005f &&
                            std::abs(result->data->bounds_max[i] - input->data->bounds_max[i]) < .005f,
                        "World transforms or bounds lost.");
            return result;
        };
        for (const auto& a : extensions.formats()) {
            auto result = convert(a.id.c_str(), cube, fixtures / "cube.obj", "cube");
            if (a.capabilities & CY3D_EXPORT_MATERIALS)
                require(!result->images.empty() && !result->images[0].rgba.empty(),
                        "Texture lost in round-trip.");
        }
        auto transformed = load(fixtures / "transformed.gltf");
        for (const char* id : {"glb2", "gltf2", "fbx", "obj", "stlb", "plyb"})
            convert(id, transformed, fixtures / "transformed.gltf", "transformed");
        auto pbr = load(fixtures / "pbr_mapped.gltf");
        for (const char* id : {"glb2", "gltf2"}) {
            auto result = convert(id, pbr, fixtures / "pbr_mapped.gltf", "pbr");
            const auto& src = pbr->data->materials[0];
            const auto& dst = result->data->materials[0];
            require(std::abs(dst.metallic - src.metallic) < .001 &&
                        std::abs(dst.roughness - src.roughness) < .001,
                    "PBR factors lost.");
            require(std::abs(dst.normal_scale - src.normal_scale) < .001 &&
                        std::abs(dst.occlusion_strength - src.occlusion_strength) < .001,
                    "PBR map strength lost.");
            require(dst.alpha_mode == src.alpha_mode && std::abs(dst.alpha_cutoff - src.alpha_cutoff) < .001,
                    "Alpha masking lost.");
            for (int slot = 0; slot < CY3D_MAP_COUNT; ++slot) {
                require(dst.maps[slot].texture >= 0 && dst.maps[slot].uv_set == src.maps[slot].uv_set,
                        "PBR map or UV channel lost.");
                for (int n = 0; n < 9; ++n)
                    if (std::abs(dst.maps[slot].transform[n] - src.maps[slot].transform[n]) >= .001)
                        throw std::runtime_error("UV texture transform lost: slot " + std::to_string(slot) +
                                                 " value " + std::to_string(n) + " src " +
                                                 std::to_string(src.maps[slot].transform[n]) + " dst " +
                                                 std::to_string(dst.maps[slot].transform[n]));
                require(dst.maps[slot].wrap_u == src.maps[slot].wrap_u &&
                            dst.maps[slot].wrap_v == src.maps[slot].wrap_v,
                        "Texture wrapping lost.");
            }
            auto& packed = result->images[dst.maps[CY3D_METALLIC].texture];
            auto& metal = pbr->images[src.maps[CY3D_METALLIC].texture];
            require(std::abs(int(packed.rgba[dst.maps[CY3D_METALLIC].channel]) -
                             int(metal.rgba[src.maps[CY3D_METALLIC].channel])) <= 1,
                    "Metallic image channel changed.");
        }
        auto animated = load(fixtures / "skinned.glb");
        for (const char* id : {"glb2", "gltf2", "fbx", "fbxa"}) {
            auto result = convert(id, animated, fixtures / "skinned.glb", "skinned");
            require(result->animation && result->animation->clips.size() == 3, "Animation clips lost.");
            require(result->data->meshes[0].skin && result->data->meshes[0].bone_count == 2,
                    "Bone weights lost.");
            for (const auto& clip : result->animation->clips)
                require(std::abs(clip.duration - 1) < .002, "Animation duration changed.");
            if (std::string(id) == "glb2" || std::string(id) == "gltf2") {
                int index = -1;
                for (size_t i = 0; i < result->animation->clips.size(); ++i)
                    if (result->animation->clips[i].name == "Step")
                        index = static_cast<int>(i);
                require(index >= 0, "STEP clip missing.");
                const auto& tracks = result->animation->clips[index].tracks;
                require(!tracks.empty() && tracks[0].positions.size() >= 2, "STEP keys lost.");
                auto node = tracks[0].node;
                auto pose = result->animation->evaluate(index, .5);
                auto endpoint = result->animation->evaluate(index, 1);
                require(std::abs(pose[node].v[12]) < .001 && std::abs(endpoint[node].v[12] - 2) < .001,
                        "STEP interpolation changed during export.");
            }
        }
        auto cloud = load(fixtures / "cloud_little.ply");
        auto cloudFile = dir / "cloud-export.ply";
        OperationControl cloudControl;
        auto cloudBundle =
            exportScene(action("plyb"), cloud, cloudFile, fixtures / "cloud_little.ply", cloudControl);
        cloudBundle->commit(true);
        auto cloudRoundTrip = load(cloudFile);
        require(cloudRoundTrip->points == cloud->points && !cloudRoundTrip->triangles,
                "Point cloud conversion lost points.");
        for (uint32_t v = 0; v < cloud->data->meshes[0].vertex_count; ++v)
            for (int c = 0; c < 4; ++c)
                require(std::abs(cloud->data->meshes[0].colors[v * 4 + c] -
                                 cloudRoundTrip->data->meshes[0].colors[v * 4 + c]) < .009,
                        "Point colors changed.");
        auto tool = extensions.tools()[0];
        auto module = std::make_shared<ExtensionModule>(tool.library);
        auto state = std::shared_ptr<void>(module->api->create_tool(tool.id.c_str()),
                                           [module](void* p) { module->api->destroy_tool(p); });
        require(bool(state), "Tool creation failed.");
        OperationControl control;
        auto edited = runTool(module, tool, state.get(), animated, control);
        require(edited && edited->data != animated->data && edited->triangles == animated->triangles,
                "UV tool did not create an independent scene.");
        require(edited->animation && edited->animation->clips.size() == 3 && edited->data->meshes[0].skin,
                "UV generation lost animation or skin.");
        bool area = false;
        for (uint32_t i = 0; i < edited->data->mesh_count; ++i) {
            const auto& mesh = edited->data->meshes[i];
            for (uint32_t v = 0; v < mesh.vertex_count; ++v) {
                const auto& vertex = mesh.vertices[v];
                require(vertex.uv1[0] >= 0 && vertex.uv1[0] <= 1 && vertex.uv1[1] >= 0 && vertex.uv1[1] <= 1,
                        "UV outside atlas.");
                bool found = false;
                for (uint32_t old = 0; old < animated->data->meshes[i].vertex_count; ++old) {
                    const auto& originalVertex = animated->data->meshes[i].vertices[old];
                    if (!std::memcmp(vertex.position, originalVertex.position, sizeof(vertex.position)) &&
                        !std::memcmp(vertex.uv, originalVertex.uv, sizeof(vertex.uv))) {
                        found = true;
                        break;
                    }
                }
                require(found, "UV tool changed geometry or original UV0.");
            }
            for (uint32_t f = 0; f < mesh.index_count; f += 3) {
                const auto* a = mesh.vertices[mesh.indices[f]].uv1;
                const auto* b = mesh.vertices[mesh.indices[f + 1]].uv1;
                const auto* c = mesh.vertices[mesh.indices[f + 2]].uv1;
                if (std::abs((b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0])) > 1e-6)
                    area = true;
            }
        }
        require(area, "Atlas contains no UV area.");
        convert("glb2", edited, fixtures / "skinned.glb", "edited");
        control.cancel = true;
        bool cancelled = false;
        try {
            runTool(module, tool, state.get(), cube, control);
        } catch (...) {
            cancelled = true;
        }
        require(cancelled, "Tool ignored cancellation.");
        try {
            exportScene(action("glb2"), cube, dir / "cancelled.glb", fixtures / "cube.obj", control);
            cancelled = false;
        } catch (...) {
        }
        require(cancelled && !fs::exists(dir / "cancelled.glb"), "Cancelled export produced a final file.");
        OperationControl valid;
        bool protectedSource = false;
        try {
            exportScene(action("obj"), cube, fixtures / "cube.obj", fixtures / "cube.obj", valid);
        } catch (...) {
            protectedSource = true;
        }
        require(protectedSource && contents(fixtures / "cube.obj") == original, "Source protection failed.");
        auto output = dir / "conflict.obj";
        {
            std::ofstream file(output);
            file << "keep";
        }
        auto bundle = exportScene(action("obj"), cube, output, fixtures / "cube.obj", valid);
        require(!bundle->conflicts().empty(), "Output conflict was not discovered.");
        bool blocked = false;
        try {
            bundle->commit(false);
        } catch (...) {
            blocked = true;
        }
        require(blocked && contents(output) == "keep", "Unconfirmed export replaced an existing file.");
        bundle->commit(true);
        require(load(output)->triangles == cube->triangles, "Confirmed replacement failed.");
        std::cout << "Export, UV tool, ownership, source protection and cancellation contracts passed.\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
    return 0;
}
