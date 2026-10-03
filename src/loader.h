// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Cyberalien
#pragma once
#include "plugins.h"
#include "animation.h"
#include <atomic>
#include <condition_variable>
#include <functional>
#include <list>
#include <thread>
namespace cy {
struct Image {
    int width = 0, height = 0;
    std::vector<uint8_t> rgba;
};
struct Scene {
    struct Stamp {
        fs::path file;
        fs::file_time_type modified{};
        uintmax_t size = 0;
        bool exists = false;
    };
    std::shared_ptr<Module> module;
    Cy3DScene* data = nullptr;
    std::vector<Image> images;
    std::vector<uint8_t> texture_usage; // bit 0: linear data, bit 1: sRGB color
    std::shared_ptr<AnimationData> animation;
    std::vector<std::string> warnings;
    std::vector<Stamp> dependencies;
    double import_ms = 0;
    uint64_t memory = 0;
    uint64_t vertices = 0, triangles = 0, points = 0, splats = 0, gpu_bytes = 0;
    bool reusable = true;
    ~Scene() {
        if (data)
            module->api->release(data);
    }
};
std::shared_ptr<Scene> importScene(Plugins&, const fs::path&, std::atomic<bool>&, std::atomic<float>&);
void decodeImages(Scene&, const fs::path&, const std::atomic<bool>&);
struct Result {
    uint64_t generation = 0;
    fs::path file;
    std::shared_ptr<Scene> scene;
    std::string error;
    bool cached = false;
};
class Loader {
    Plugins& plugins_;
    std::function<void()> wake_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::thread worker_;
    bool stop_ = false, pending_ = false;
    bool prefetch_pending_ = false;
    std::vector<fs::path> prefetch_files_;
    fs::path file_;
    std::vector<fs::path> neighbors_;
    uint64_t generation_ = 0;
    std::shared_ptr<std::atomic<bool>> cancel_;
    std::unique_ptr<Result> result_;
    struct Cached {
        fs::path file;
        fs::file_time_type modified;
        std::shared_ptr<Scene> scene;
    };
    std::list<Cached> cache_;
    uint64_t cache_bytes_ = 0;
    void run();

  public:
    static constexpr uint64_t cache_limit = 256ull * 1024 * 1024;
    std::atomic<float> progress{0};
    std::atomic<uint64_t> cache_usage{0};
    Loader(Plugins&, std::function<void()> wake);
    ~Loader();
    uint64_t request(const fs::path&, std::vector<fs::path> neighbors = {});
    void prefetch(std::vector<fs::path> neighbors);
    uint64_t cancel();
    std::unique_ptr<Result> take();
};
} // namespace cy
