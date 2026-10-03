// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Cyberalien
#include "loader.h"
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#include <stb_image.h>
#include <fstream>
#include <limits>
namespace cy {
void decodeImages(Scene& scene, const fs::path&, const std::atomic<bool>& cancel) {
    constexpr uint64_t budget = 256ull * 1024 * 1024;
    uint64_t decoded = 0;
    scene.images.resize(scene.data->texture_count);
    for (uint32_t i = 0; i < scene.data->texture_count && !cancel; ++i) {
        const auto& t = scene.data->textures[i];
        auto& image = scene.images[i];
        std::vector<uint8_t> file;
        const uint8_t* data = t.bytes;
        uint64_t size = t.byte_count;
        if (t.path && *t.path) {
            std::ifstream input(path(t.path), std::ios::binary | std::ios::ate);
            auto n = input.tellg();
            if (n > 0 && static_cast<uint64_t>(n) <= budget) {
                file.resize(static_cast<size_t>(n));
                input.seekg(0);
                input.read(reinterpret_cast<char*>(file.data()), n);
                if (input) {
                    data = file.data();
                    size = file.size();
                }
            }
        }
        int w = 0, h = 0, channels = 0;
        uint8_t* pixels = nullptr;
        if (t.width && t.height) {
            w = static_cast<int>(t.width);
            h = static_cast<int>(t.height);
        } else if (data && size <= static_cast<uint64_t>(std::numeric_limits<int>::max())) {
            if (!stbi_info_from_memory(data, static_cast<int>(size), &w, &h, &channels))
                w = 0;
        }
        uint64_t bytes = w > 0 && h > 0 ? static_cast<uint64_t>(w) * h * 4 : 0;
        if (bytes && w <= 16384 && h <= 16384 && decoded + bytes <= budget) {
            if (t.width) {
                if (data && size >= bytes)
                    image.rgba.assign(data, data + bytes);
            } else if ((pixels = stbi_load_from_memory(data, static_cast<int>(size), &w, &h, &channels, 4))) {
                image.rgba.assign(pixels, pixels + bytes);
                stbi_image_free(pixels);
            }
            if (!image.rgba.empty()) {
                image.width = w;
                image.height = h;
                decoded += bytes;
                continue;
            }
        }
        scene.warnings.push_back("Texture absente, non lisible ou trop grande : " +
                                 std::string(t.path ? t.path : "texture embarquee"));
    }
    scene.memory += decoded;
}
} // namespace cy
