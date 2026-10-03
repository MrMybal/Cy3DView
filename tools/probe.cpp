// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Cyberalien
#include "loader.h"
#include <fstream>
#include <iostream>
#include <chrono>
#include <stdexcept>
#ifdef _WIN32
#include <windows.h>
#endif
using namespace cy;
void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
int main(int argc, char** argv) {
    try {
        fs::path executable = fs::absolute(path(argv[0]));
#ifdef _WIN32
        wchar_t buffer[32768]{};
        GetModuleFileNameW(nullptr, buffer, 32768);
        executable = buffer;
#endif
        if (argc >= 3 && std::string(argv[1]) == "--manifest") {
            auto library = fs::absolute(path(argv[2]));
            Module module(library);
            std::ofstream output(library.string() + ".cy3d");
            output << module.api->name << '\n'
                   << utf8(library.filename()) << '\n'
                   << module.api->extensions << '\n'
                   << (argc > 3 ? std::stoi(argv[3]) : 0) << '\n';
            require(output.good(), "Cannot write manifest");
            return 0;
        }
        Plugins plugins(executable.parent_path() / "plugins");
        if (argc == 2 && std::string(argv[1]) == "--formats") {
            for (const auto& entry : plugins.entries())
                std::cout << entry.name << "\n" << entry.extensions << "\n";
            return 0;
        }
        if (argc == 3 && std::string(argv[1]) == "--corpus") {
            auto directory = fs::absolute(path(argv[2]));
            std::atomic<bool> cancel{false};
            std::atomic<float> progress{0};
            for (const auto* file :
                 {"FBX/box.fbx", "FBX/embedded_ascii/box.FBX", "FBX/animation_with_skeleton.fbx",
                  "COLLADA/box_nested_animation.dae", "COLLADA/teapot_instancenodes.DAE", "3MF/box.3mf"}) {
                auto scene = importScene(plugins, directory / path(file), cancel, progress);
                require(scene->triangles > 0 && scene->vertices > 0, "Corpus model has no geometry");
                if (std::string(file) == "FBX/embedded_ascii/box.FBX")
                    require(!scene->images.empty() && !scene->images.front().rgba.empty(),
                            "Embedded FBX texture missing");
                if (std::string(file) == "COLLADA/teapot_instancenodes.DAE")
                    require(scene->data->instance_count > scene->data->mesh_count, "COLLADA instancing lost");
                if (std::string(file) == "FBX/animation_with_skeleton.fbx" ||
                    std::string(file) == "COLLADA/box_nested_animation.dae")
                    require(scene->animation && !scene->animation->clips.empty(), "Corpus animation lost");
                if (std::string(file) == "FBX/animation_with_skeleton.fbx") {
                    bool skin = false;
                    for (uint32_t i = 0; i < scene->data->mesh_count; ++i)
                        skin = skin || scene->data->meshes[i].skin;
                    require(skin, "FBX skeleton lost");
                }
                std::cout << file << ": " << scene->triangles << " triangles, " << scene->data->instance_count
                          << " instances, " << scene->import_ms << " ms\n";
            }
            return 0;
        }
        if (argc == 3 && std::string(argv[1]) == "--test") {
            auto directory = fs::absolute(path(argv[2]));
            std::atomic<bool> cancel{false};
            std::atomic<float> progress{0};
            int loaded = 0;
            for (const auto* file : {"cube.obj", "triangle.stl", "binary.stl", "triangle.ply",
                                     "transformed.gltf", "embedded.glb", "unicode/épreuve.obj"}) {
                auto scene = importScene(plugins, directory / path(file), cancel, progress);
                require(scene->data->mesh_count > 0, "No mesh");
                require(scene->data->instance_count > 0, "No instance");
                if (std::string(file) == "transformed.gltf") {
                    require(scene->data->instance_count == 2, "Lost instances");
                    require(scene->data->mesh_count == 1, "Geometry duplicated");
                    require(scene->data->bounds_max[0] > 4.9f, "Node transform lost");
                }
                if (std::string(file) == "embedded.glb")
                    require(!scene->images.empty() && !scene->images.front().rgba.empty(),
                            "Embedded GLB texture lost");
                if (std::string(file) == "cube.obj")
                    require(!scene->images.empty() && !scene->images.front().rgba.empty(),
                            "External texture lost");
                std::cout << file << ": " << scene->data->mesh_count << " mesh(es), " << scene->import_ms
                          << " ms\n";
                ++loaded;
            }
            bool rejected = false;
            try {
                importScene(plugins, directory / "broken.obj", cancel, progress);
            } catch (...) {
                rejected = true;
            }
            require(rejected, "Malformed input accepted");
            rejected = false;
            try {
                importScene(plugins, directory / "truncated.stl", cancel, progress);
            } catch (...) {
                rejected = true;
            }
            require(rejected, "Truncated STL accepted");
            rejected = false;
            try {
                importScene(plugins, directory / "missing.obj", cancel, progress);
            } catch (...) {
                rejected = true;
            }
            require(rejected, "Missing file accepted");
            cancel = true;
            rejected = false;
            try {
                importScene(plugins, directory / "cube.obj", cancel, progress);
            } catch (...) {
                rejected = true;
            }
            require(rejected, "Cancellation ignored");
            require(naturalLess(path("model2.obj"), path("model10.obj")), "Natural sorting failed");
            std::atomic<int> wakes{0};
            Loader loader(plugins, [&] { ++wakes; });
            auto first = loader.request(directory / "cube.obj");
            (void)first;
            auto latest = loader.request(directory / "triangle.ply");
            auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
            std::unique_ptr<Result> result;
            while (std::chrono::steady_clock::now() < deadline) {
                result = loader.take();
                if (result)
                    break;
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            require(result && result->generation == latest && result->scene, "Latest request did not win");
            auto again = loader.request(directory / "triangle.ply");
            result.reset();
            while (std::chrono::steady_clock::now() < deadline) {
                result = loader.take();
                if (result)
                    break;
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            require(result && result->generation == again && result->cached, "Scene cache missed");
            auto before = loader.cache_usage.load();
            loader.prefetch({directory / "triangle.stl"});
            auto prefetchLimit = std::chrono::steady_clock::now() + std::chrono::seconds(10);
            while (loader.cache_usage.load() <= before && std::chrono::steady_clock::now() < prefetchLimit)
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            loader.request(directory / "triangle.stl");
            result.reset();
            while (std::chrono::steady_clock::now() < prefetchLimit) {
                result = loader.take();
                if (result)
                    break;
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            require(result && result->scene && result->cached, "Standalone neighbor prefetch missed");
            loader.request(directory / "triangle.ply");
            auto cancelledGeneration = loader.cancel();
            auto nextGeneration = loader.request(directory / "triangle.ply");
            require(nextGeneration > cancelledGeneration, "Cancellation did not invalidate the generation");
            result.reset();
            while (std::chrono::steady_clock::now() < prefetchLimit) {
                result = loader.take();
                if (result)
                    break;
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            require(result && result->scene && result->generation == nextGeneration,
                    "Loader did not recover after cancel");
            auto testDirectory = executable.parent_path() / "cache-test";
            fs::create_directories(testDirectory);
            for (const auto* file : {"cube.obj", "cube.mtl", "checker.png"})
                fs::copy_file(directory / file, testDirectory / file, fs::copy_options::overwrite_existing);
            auto waitResult = [&] {
                std::unique_ptr<Result> value;
                auto limit = std::chrono::steady_clock::now() + std::chrono::seconds(10);
                while (std::chrono::steady_clock::now() < limit) {
                    value = loader.take();
                    if (value)
                        return value;
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                }
                throw std::runtime_error("Cache test timeout");
            };
            loader.request(testDirectory / "cube.obj");
            auto materialResult = waitResult();
            require(materialResult->scene != nullptr, "Cache fixture failed");
            {
                std::ofstream material(testDirectory / "cube.mtl");
                material << "newmtl Surface\nKd 0.25 0.5 0.75\n";
            }
            loader.request(testDirectory / "cube.obj");
            auto updated = waitResult();
            require(updated->scene && !updated->cached, "Changed MTL did not invalidate cache");
            const auto& firstMesh = updated->scene->data->meshes[0];
            require(updated->scene->data->materials[firstMesh.material].color[0] < .3f,
                    "Changed MTL was not reloaded");
            for (const auto* file : {"cube.obj", "cube.mtl", "checker.png"})
                fs::remove(testDirectory / file);
            fs::remove(testDirectory);
            std::cout
                << "PASS " << loaded
                << " formats/paths + transforms, textures, errors, cancellation, cache, request ordering\n";
            return 0;
        }
        if (argc == 4 && std::string(argv[1]) == "--plugin") {
            Module module(fs::absolute(path(argv[2])));
            Cy3DHost host{CY3D_API_VERSION, sizeof(Cy3DHost), nullptr,
                          nullptr,          nullptr,          2ull * 1024 * 1024 * 1024};
            for (int i = 0; i < 5; ++i) {
                Cy3DScene* scene = nullptr;
                char error[2048]{};
                auto start = std::chrono::steady_clock::now();
                int result = module.api->load(utf8(fs::absolute(path(argv[3]))).c_str(), &host, &scene, error,
                                              sizeof(error));
                if (result)
                    throw std::runtime_error(error);
                auto elapsed =
                    std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
                        .count();
                std::cout << module.api->name << ": " << elapsed << " ms, " << scene->memory_bytes / 1048576.
                          << " MiB\n";
                module.api->release(scene);
            }
            return 0;
        }
        if (argc < 2) {
            std::cout << "cy3d_probe --formats | --test fixtures | model [iterations]\n";
            return 0;
        }
        std::atomic<bool> cancel{false};
        std::atomic<float> progress{0};
        int iterations = argc > 2 ? std::max(1, std::stoi(argv[2])) : 1;
        for (int i = 0; i < iterations; ++i) {
            auto scene = importScene(plugins, fs::absolute(path(argv[1])), cancel, progress);
            std::cout << scene->import_ms << " ms, " << scene->memory / 1048576. << " MiB, "
                      << scene->data->mesh_count << " meshes\n";
        }
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
